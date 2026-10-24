#include "ContentMetadataSource.h"
#include "CurseforgeFingerprint.h"
#include "PackFileStore.h"
#include "ProjectAuthors.h"
#include "domain/Catalogue.h"
#include "domain/ContentTypes.h"
#include "domain/LicensePolicy.h"
#include "services/JsonResponse.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <QtConcurrent>
#include <algorithm>
namespace studio {
struct ContentMetadataSource::Batch {
    QVariantMap pack;
    QVariantList rows;
    Callback callback;
    Progress progress;
    QStringList errors;
    QHash<QString, QVariantMap> projects;
    QHash<QString, QString> projectTeams;
    QHash<QString, QList<int>> fileRows;
    QHash<QString, Release> fingerprintMatches;
    QSet<QString> ambiguousFingerprints;
    int pending = 0;
    bool dirty = false;
};
namespace {
QStringList strings(const QJsonArray &array) {
    QStringList result;
    for (const auto &value : array)
        if (value.isString())
            result << value.toString();
    return result;
}
QString primary(const QVariantMap &row) {
    const auto ids = row.value("sourceIds").toMap();
    return !ids.value("modrinth").toString().isEmpty()     ? "modrinth"
           : !ids.value("curseforge").toString().isEmpty() ? "curseforge"
                                                           : "";
}
bool knownHash(const QString &hash) {
    static const QRegularExpression pattern("^[a-fA-F0-9]{40}$");
    return pattern.match(hash).hasMatch();
}
bool verifiedInstalledRelease(const QVariantMap &row) {
    const auto release = releaseFromVariant(row.value("installedRelease").toMap());
    return (release.provider == "modrinth" || release.provider == "curseforge") && !release.id.isEmpty() &&
           !release.projectId.isEmpty() && knownHash(row.value("sha1").toString()) &&
           release.sha1.compare(row.value("sha1").toString(), Qt::CaseInsensitive) == 0 &&
           row.value("sourceIds").toMap().value(release.provider).toString() == release.projectId;
}
bool needsFingerprint(const QVariantMap &row) {
    return !row.value("directory").toBool() && primary(row) != "modrinth" &&
           knownHash(row.value("sha1").toString()) && !verifiedInstalledRelease(row);
}
void authors(QVariantMap &project, const QVariantList &members) {
    project["authors"] = members;
    QStringList names;
    for (const auto &member : members)
        names << member.toMap().value("name").toString();
    project["author"] = names.join(", ");
    project["authorIcon"] = members.isEmpty() ? QVariant("") : members.first().toMap().value("icon");
    project["authorIds"] = members.isEmpty() ? QVariantMap{} : members.first().toMap().value("ids").toMap();
}
void acceptRelease(QVariantMap &row, const Release &release) {
    if (release.projectId.isEmpty() || release.id.isEmpty() ||
        release.sha1.compare(row.value("sha1").toString(), Qt::CaseInsensitive))
        return;
    auto ids = row.value("sourceIds").toMap();
    if (!ids.value(release.provider).toString().isEmpty() &&
        ids.value(release.provider).toString() != release.projectId)
        return;
    ids[release.provider] = release.projectId;
    row["sourceIds"] = ids;
    if (release.provider == "modrinth" || ids.value("modrinth").toString().isEmpty()) {
        row["provider"] = release.provider;
        row["projectId"] = release.projectId;
        row["installedRelease"] = release.toVariant();
        row["version"] = release.version;
    }
    row["identityResolved"] = true;
    row["identitySha1"] = row.value("sha1");
    row["external"] = false;
    if (release.provider == "modrinth")
        row["projectUrl"] = "https://modrinth.com/" + categoryType(row.value("category").toString()) + '/' +
                            release.projectId;
}
QString environment(const QJsonObject &project) {
    const auto values = project.value("environment").toArray();
    for (const auto &value : values) {
        const auto text = value.toString();
        if (text == "client_only" || text == "client_only_server_optional" || text == "singleplayer_only")
            return "client";
        if (text == "server_only" || text == "dedicated_server_only" || text == "server_only_client_optional")
            return "server";
        if (text == "client_and_server" || text == "client_or_server" ||
            text == "client_or_server_prefers_both")
            return "both";
    }
    if (!project.value("client_side").toString().isEmpty() &&
        !project.value("server_side").toString().isEmpty()) {
        const bool client = project.value("client_side") != "unsupported",
                   server = project.value("server_side") != "unsupported";
        return client && server ? "both" : client ? "client" : server ? "server" : "unknown";
    }
    return "unknown";
}
QString idsQuery(const QStringList &ids) {
    return QString::fromLatin1(QUrl::toPercentEncoding(
        QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(ids)).toJson(QJsonDocument::Compact))));
}
} // namespace
ContentMetadataSource::ContentMetadataSource(std::shared_ptr<const IContentIndex> index, QObject *parent,
                                             QUrl mr, QUrl cf, QHash<QString, QString> reviewed)
    : QObject(parent), m_index(std::move(index)), m_network(this), m_modrinthApi(std::move(mr)),
      m_curseforgeApi(std::move(cf)), m_key(qgetenv("CURSEFORGE_API_KEY")) {
    QHash<QString, QVariantMap> groups;
    QSet<QString> ambiguous;
    for (auto it = reviewed.cbegin(); it != reviewed.cend(); ++it) {
        const auto provider = it.key().section(':', 0, 0), id = it.key().section(':', 1);
        if ((provider != "modrinth" && provider != "curseforge") || id.isEmpty() || it.value().isEmpty())
            continue;
        auto &group = groups[it.value()];
        if (group.contains(provider) && group.value(provider).toString() != id)
            ambiguous.insert(it.value());
        group[provider] = id;
    }
    for (auto it = reviewed.cbegin(); it != reviewed.cend(); ++it)
        if (!ambiguous.contains(it.value()))
            m_aliases[it.key()] = groups.value(it.value());
}
void ContentMetadataSource::setCurseforgeKey(const QByteArray &key) {
    if (m_key == key.trimmed())
        return;
    m_key = key.trimmed();
    ++m_authGeneration;
}
void ContentMetadataSource::json(const QString &provider, const QString &path, const QByteArray &body,
                                 JsonCallback callback) {
    const bool cf = provider == "curseforge";
    if (cf && m_key.isEmpty()) {
        callback({}, "Connect CurseForge in Settings to identify this content.");
        return;
    }
    const auto url = (cf ? m_curseforgeApi : m_modrinthApi).resolved(QUrl(path));
    const auto key = QString::number(cf ? m_authGeneration : 0) + ':' + url.toString() + ':' +
                     QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex());
    if (m_pendingReads.contains(key)) {
        m_pendingReads[key] << std::move(callback);
        return;
    }
    m_pendingReads[key] = {std::move(callback)};
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
    request.setRawHeader("Accept", "application/json");
    if (cf)
        request.setRawHeader("x-api-key", m_key);
    if (!body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = body.isEmpty() ? m_network.get(request) : m_network.post(request, body);
    const int generation = m_authGeneration;
    readJsonResponse(reply, this, {.maximumBytes = 16 * 1024 * 1024, .workerThreshold = 0},
                     [this, key, provider, cf, generation](JsonResponse response) {
                         QString error;
                         if (cf && generation != m_authGeneration)
                             error = "The CurseForge connection changed during identification.";
                         else if (response.failure == JsonResponseFailure::InvalidJson)
                             error = provider + " returned invalid metadata.";
                         else if (response.failure == JsonResponseFailure::TooLarge)
                             error = provider + " metadata exceeded the 16 MiB size limit.";
                         else if (response.failure != JsonResponseFailure::None)
                             error = provider + " metadata request failed (HTTP " +
                                     QString::number(response.status) + ").";
                         deliver(key, std::move(response.document), error, cf, generation);
                     });
}
void ContentMetadataSource::deliver(const QString &key, QJsonDocument document, QString error, bool cf,
                                    int generation) {
    if (cf && generation != m_authGeneration) {
        document = {};
        error = "The CurseForge connection changed during identification.";
    }
    const auto callbacks = m_pendingReads.take(key);
    const QPointer<ContentMetadataSource> guard(this);
    for (const auto &callback : callbacks) {
        if (!guard)
            return;
        callback(document, error);
    }
}
void ContentMetadataSource::resolve(const QVariantMap &pack, const QVariantList &rows, Callback callback,
                                    Progress progress) {
    auto batch = std::make_shared<Batch>();
    batch->pack = pack;
    batch->rows = rows;
    for (auto &value : batch->rows) {
        auto row = value.toMap();
        const auto hash = row.value("sha1").toString();
        if (row.value("metadataResolved").toBool() &&
            (!knownHash(hash) || row.value("metadataSha1").toString().compare(hash, Qt::CaseInsensitive)))
            row["metadataResolved"] = false;
        if (row.value("identityResolved").toBool() &&
            (!knownHash(hash) || row.value("identitySha1").toString().compare(hash, Qt::CaseInsensitive)))
            row["identityResolved"] = false;
        value = row;
    }
    batch->callback = std::move(callback);
    batch->progress = std::move(progress);
    if (!m_index) {
        identify(batch);
        return;
    }
    const auto index = m_index;
    const auto snapshot = index->beginSnapshot(pack);
    auto *watcher = new QFutureWatcher<ContentIndexResult>(this);
    connect(watcher, &QFutureWatcher<ContentIndexResult>::finished, this, [this, watcher, batch] {
        auto cached = watcher->result();
        watcher->deleteLater();
        batch->rows = std::move(cached.rows);
        if (!cached.error.isEmpty())
            batch->errors << "Local content index: " + cached.error;
        const QPointer<ContentMetadataSource> guard(this);
        publish(batch);
        if (!guard)
            return;
        const bool complete =
            std::all_of(batch->rows.cbegin(), batch->rows.cend(), [this](const QVariant &value) {
                const auto row = value.toMap(), ids = row.value("sourceIds").toMap(),
                           urls = row.value("sourceUrls").toMap();
                const auto hash = row.value("sha1").toString();
                if (!row.value("metadataResolved").toBool() || !knownHash(hash) ||
                    row.value("metadataSha1").toString().compare(hash, Qt::CaseInsensitive) ||
                    row.value("provider").toString() != primary(row) || !verifiedInstalledRelease(row))
                    return false;
                if (!m_key.isEmpty() && ids.contains("curseforge") &&
                    urls.value("curseforge").toString().isEmpty())
                    return false;
                for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
                    const auto aliases = m_aliases.value(it.key() + ':' + it.value().toString());
                    for (auto alias = aliases.cbegin(); alias != aliases.cend(); ++alias)
                        if (!ids.contains(alias.key()))
                            return false;
                }
                return true;
            });
        if (complete) {
            batch->callback(batch->rows, batch->errors.join('\n'));
            return;
        }
        identify(batch);
    });
    const auto actual = batch->rows;
    watcher->setFuture(QtConcurrent::run([index, pack, actual, snapshot] {
        try {
            return index->lookup(pack, actual, snapshot);
        } catch (...) {
            return ContentIndexResult{actual, "Could not read cached content metadata."};
        }
    }));
}
void ContentMetadataSource::publish(const std::shared_ptr<Batch> &batch) {
    if (batch->progress)
        batch->progress(batch->rows);
}
void ContentMetadataSource::identify(const std::shared_ptr<Batch> &batch) {
    QStringList hashes;
    QSet<QString> unique;
    for (int index = 0; index < batch->rows.size(); ++index) {
        auto &value = batch->rows[index];
        auto row = value.toMap();
        if ((row.value("metadataResolved").toBool() && verifiedInstalledRelease(row)) ||
            row.value("directory").toBool())
            continue;
        const auto hash = row.value("sha1").toString().toLower();
        batch->fileRows[hash] << index;
        if (!knownHash(hash))
            continue;
        const auto release = releaseFromVariant(row.value("installedRelease").toMap());
        if (release.provider == "modrinth" && !release.projectId.isEmpty() && !release.id.isEmpty() &&
            release.sha1.compare(hash, Qt::CaseInsensitive) == 0) {
            acceptRelease(row, release);
            value = row;
            continue;
        }
        if ((!row.value("identityResolved").toBool() || !verifiedInstalledRelease(row)) &&
            !unique.contains(hash)) {
            hashes << hash;
            unique.insert(hash);
        }
    }
    if (hashes.isEmpty()) {
        fingerprints(batch);
        return;
    }
    batch->pending = (hashes.size() + 499) / 500;
    for (int start = 0; start < hashes.size(); start += 500) {
        const auto requested = hashes.mid(start, 500);
        const auto body = QJsonDocument(QJsonObject{{"hashes", QJsonArray::fromStringList(requested)},
                                                    {"algorithm", "sha1"}})
                              .toJson(QJsonDocument::Compact);
        json("modrinth", "version_files", body, [this, batch, requested](QJsonDocument doc, QString error) {
            if (!error.isEmpty())
                batch->errors << "Identification: " + error;
            const auto installed = doc.object();
            for (const auto &hash : requested) {
                const auto release = modrinthRelease(installed.value(hash).toObject(), hash);
                for (const auto index : batch->fileRows.value(hash)) {
                    auto row = batch->rows.at(index).toMap();
                    acceptRelease(row, release);
                    batch->dirty |= row != batch->rows.at(index).toMap();
                    batch->rows[index] = row;
                }
            }
            if (--batch->pending == 0)
                fingerprints(batch);
        });
    }
}
void ContentMetadataSource::fingerprints(const std::shared_ptr<Batch> &batch) {
    bool needed = false;
    for (const auto &value : batch->rows) {
        const auto row = value.toMap();
        needed |= needsFingerprint(row) && !row.contains("curseforgeFingerprint");
    }
    if (m_key.isEmpty() || !needed || batch->pack.value("path").toString().isEmpty()) {
        matchFingerprints(batch);
        return;
    }
    const auto root = batch->pack.value("path").toString();
    const auto original = batch->rows;
    auto *watcher = new QFutureWatcher<ContentIndexResult>(this);
    connect(watcher, &QFutureWatcher<ContentIndexResult>::finished, this, [this, watcher, batch] {
        auto result = watcher->result();
        watcher->deleteLater();
        batch->dirty |= result.rows != batch->rows;
        batch->rows = std::move(result.rows);
        if (!result.error.isEmpty())
            batch->errors << result.error;
        matchFingerprints(batch);
    });
    watcher->setFuture(QtConcurrent::run([root, original] {
        ContentIndexResult result{original, {}};
        for (auto &value : result.rows) {
            auto row = value.toMap();
            if (!needsFingerprint(row) || row.contains("curseforgeFingerprint"))
                continue;
            const auto path = NativePackFileStore::resolve(root, row.value("relativePath").toString());
            const auto fingerprint = curseforgeFingerprint(path, row.value("sha1").toString());
            if (fingerprint.error.isEmpty())
                row["curseforgeFingerprint"] = QVariant::fromValue(quint64(fingerprint.value));
            else if (result.error.isEmpty())
                result.error = fingerprint.error;
            value = row;
        }
        return result;
    }));
}
void ContentMetadataSource::matchFingerprints(const std::shared_ptr<Batch> &batch) {
    QJsonArray fingerprints;
    QSet<quint64> unique;
    if (!m_key.isEmpty())
        for (const auto &value : batch->rows) {
            const auto row = value.toMap();
            if (!needsFingerprint(row) || !row.contains("curseforgeFingerprint"))
                continue;
            const auto fingerprint = row.value("curseforgeFingerprint").toULongLong();
            if (fingerprint <= 0xffffffffULL && !unique.contains(fingerprint)) {
                unique.insert(fingerprint);
                fingerprints << qint64(fingerprint);
            }
        }
    if (fingerprints.isEmpty()) {
        projects(batch);
        return;
    }
    batch->pending = (fingerprints.size() + 499) / 500;
    for (int start = 0; start < fingerprints.size(); start += 500) {
        QJsonArray chunk;
        for (int i = start; i < qMin(start + 500, int(fingerprints.size())); ++i)
            chunk << fingerprints[i];
        const auto body = QJsonDocument(QJsonObject{{"fingerprints", chunk}}).toJson(QJsonDocument::Compact);
        json("curseforge", "fingerprints/432", body, [this, batch](QJsonDocument doc, QString error) {
            if (!error.isEmpty())
                batch->errors << "CurseForge identification: " + error;
            for (const auto &value : doc.object().value("data").toObject().value("exactMatches").toArray()) {
                const auto match = value.toObject();
                const auto file = match.value("file").toObject();
                const auto release = curseforgeRelease(file);
                if (release.projectId.isEmpty() || release.id.isEmpty() || !knownHash(release.sha1) ||
                    (file.contains("gameId") && file.value("gameId").toInt() != 432) ||
                    (match.contains("id") &&
                     QString::number(match.value("id").toInteger()) != release.projectId))
                    continue;
                const auto hash = release.sha1.toLower();
                if (batch->fingerprintMatches.contains(hash) &&
                    batch->fingerprintMatches.value(hash).projectId != release.projectId)
                    batch->ambiguousFingerprints.insert(hash);
                batch->fingerprintMatches[hash] = release;
            }
            if (--batch->pending != 0)
                return;
            for (auto &value : batch->rows) {
                auto row = value.toMap();
                const auto hash = row.value("sha1").toString().toLower();
                if (needsFingerprint(row) && !batch->ambiguousFingerprints.contains(hash) &&
                    batch->fingerprintMatches.contains(hash)) {
                    acceptRelease(row, batch->fingerprintMatches.value(hash));
                    batch->dirty |= row != value.toMap();
                    value = row;
                }
            }
            projects(batch);
        });
    }
}
void ContentMetadataSource::projects(const std::shared_ptr<Batch> &batch) {
    QStringList mr, cf;
    QSet<QString> seen;
    for (auto &value : batch->rows) {
        auto row = value.toMap();
        auto ids = row.value("sourceIds").toMap();
        const auto supplied = ids;
        for (auto it = supplied.cbegin(); it != supplied.cend(); ++it) {
            const auto aliases = m_aliases.value(it.key() + ':' + it.value().toString());
            for (auto alias = aliases.cbegin(); alias != aliases.cend(); ++alias)
                if (!ids.contains(alias.key()))
                    ids[alias.key()] = alias.value();
        }
        row["sourceIds"] = ids;
        const auto provider = primary(row);
        if (!row.value("provider").toString().isEmpty() && row.value("provider").toString() != provider)
            row["metadataResolved"] = false;
        const auto id = ids.value(provider).toString();
        if (!row.value("metadataResolved").toBool())
            row["metadataAttemptedAt"] = QDateTime::currentMSecsSinceEpoch();
        if (provider.isEmpty())
            continue;
        auto urls = row.value("sourceUrls").toMap();
        const auto oldProvider = row.value("provider").toString();
        if (!row.value("projectUrl").toString().isEmpty() && !oldProvider.isEmpty())
            urls[oldProvider] = row.value("projectUrl");
        if (!ids.value("modrinth").toString().isEmpty())
            urls["modrinth"] = "https://modrinth.com/" + categoryType(row.value("category").toString()) +
                               '/' + ids.value("modrinth").toString();
        row["sourceUrls"] = urls;
        row["provider"] = provider;
        row["projectId"] = id;
        row["external"] = false;
        if (!urls.value(provider).toString().isEmpty())
            row["projectUrl"] = urls.value(provider);
        const auto installed = releaseFromVariant(row.value("installedRelease").toMap());
        if (!installed.id.isEmpty() && !installed.sha1.isEmpty() &&
            installed.sha1.compare(row.value("sha1").toString(), Qt::CaseInsensitive) == 0)
            row["identityResolved"] = true;
        batch->dirty |= row != value.toMap();
        value = row;
        for (const auto &platform : {QString("modrinth"), QString("curseforge")}) {
            const auto projectId = ids.value(platform).toString();
            if (projectId.isEmpty() ||
                (row.value("metadataResolved").toBool() &&
                 (platform == "modrinth" || !urls.value(platform).toString().isEmpty())))
                continue;
            const auto key = platform + ':' + projectId;
            if (!seen.contains(key)) {
                seen.insert(key);
                (platform == "modrinth" ? mr : cf) << projectId;
            }
        }
    }
    const QPointer<ContentMetadataSource> guard(this);
    publish(batch);
    if (!guard)
        return;
    if (!cf.isEmpty() && m_key.isEmpty() &&
        std::any_of(batch->rows.cbegin(), batch->rows.cend(), [](const QVariant &value) {
            const auto row = value.toMap();
            return primary(row) == "curseforge" && !row.value("metadataResolved").toBool();
        }))
        batch->errors << "Connect CurseForge in Settings to load this content's authors.";
    batch->pending = (mr.size() + 99) / 100 + (m_key.isEmpty() ? 0 : (cf.size() + 99) / 100);
    if (!batch->pending) {
        finish(batch);
        return;
    }
    for (const auto &provider : {QString("modrinth"), QString("curseforge")}) {
        const auto ids = provider == "modrinth" ? mr : cf;
        if (provider == "curseforge" && m_key.isEmpty())
            continue;
        for (int start = 0; start < ids.size(); start += 100) {
            const auto requested = ids.mid(start, 100);
            QJsonArray numbers;
            if (provider == "curseforge")
                for (const auto &id : requested) {
                    bool valid = false;
                    const auto number = id.toLongLong(&valid);
                    if (valid && number > 0)
                        numbers << number;
                }
            const auto body =
                provider == "curseforge"
                    ? QJsonDocument(QJsonObject{{"modIds", numbers}}).toJson(QJsonDocument::Compact)
                    : QByteArray{};
            const auto path =
                provider == "modrinth" ? "projects?ids=" + idsQuery(requested) : QString("mods");
            json(provider, path, body, [this, batch, requested, provider](QJsonDocument doc, QString error) {
                if (!error.isEmpty())
                    batch->errors << "Project metadata: " + error;
                const auto values =
                    provider == "modrinth" ? doc.array() : doc.object().value("data").toArray();
                for (const auto &value : values) {
                    const auto data = value.toObject();
                    const auto id = provider == "modrinth" ? data.value("id").toString()
                                                           : QString::number(data.value("id").toInteger());
                    if (!requested.contains(id) ||
                        (provider == "curseforge" && data.value("gameId").toInt() != 432))
                        continue;
                    QVariantMap project{
                        {"title", data.value(provider == "modrinth" ? "title" : "name").toString()}};
                    if (project.value("title").toString().isEmpty())
                        continue;
                    if (provider == "modrinth") {
                        if (!data.value("icon_url").toString().isEmpty())
                            project["icon"] = data.value("icon_url").toString();
                        project["environment"] = environment(data);
                        project["openSource"] =
                            isKnownOpenSourceLicense(data.value("license").toObject().value("id").toString());
                        project["categories"] = strings(data.value("categories").toArray());
                        QStringList tags;
                        for (const auto &category : project.value("categories").toStringList())
                            tags << "mr:" + category;
                        project["tags"] = tags;
                        project["projectUrl"] =
                            "https://modrinth.com/" + data.value("project_type").toString("mod") + '/' + id;
                        batch->projectTeams[id] = data.value("team").toString();
                        if (batch->projectTeams.value(id).isEmpty())
                            batch->errors << "The project response omitted its author team.";
                    } else {
                        if (!data.value("logo").toObject().value("url").toString().isEmpty())
                            project["icon"] = data.value("logo").toObject().value("url").toString();
                        project["projectUrl"] = data.value("links").toObject().value("websiteUrl").toString();
                        QStringList categories, tags;
                        for (const auto &entry : data.value("categories").toArray()) {
                            const auto category = entry.toObject();
                            categories << category.value("name").toString();
                            tags << "cf:" + QString::number(category.value("id").toInteger());
                        }
                        project["categories"] = categories;
                        project["tags"] = tags;
                        if (data.value("authors").isArray()) {
                            authors(project, ProjectAuthors::curseforge(data.value("authors").toArray()));
                            project["_complete"] = true;
                        } else
                            batch->errors << "The CurseForge project response omitted its authors.";
                    }
                    batch->projects[provider + ':' + id] = project;
                }
                if (--batch->pending == 0)
                    teams(batch);
            });
        }
    }
}
void ContentMetadataSource::teams(const std::shared_ptr<Batch> &batch) {
    QStringList ids;
    QSet<QString> seen;
    for (const auto &team : batch->projectTeams)
        if (!team.isEmpty() && !seen.contains(team)) {
            ids << team;
            seen.insert(team);
        }
    if (ids.isEmpty()) {
        finish(batch);
        return;
    }
    batch->pending = (ids.size() + 99) / 100;
    for (int start = 0; start < ids.size(); start += 100) {
        const auto requested = ids.mid(start, 100);
        json("modrinth", "teams?ids=" + idsQuery(requested), {},
             [this, batch, requested](QJsonDocument doc, QString error) {
                 if (!error.isEmpty())
                     batch->errors << "Authors: " + error;
                 QHash<QString, QJsonArray> rosters;
                 if (error.isEmpty()) {
                     for (const auto &roster : doc.array())
                         for (const auto &value : roster.toArray()) {
                             const auto member = value.toObject();
                             const auto team = member.value("team_id").toString();
                             if (requested.contains(team))
                                 rosters[team] << member;
                         }
                     if (requested.size() == 1 && doc.array().size() == 1 && doc.array().first().isArray() &&
                         doc.array().first().toArray().isEmpty())
                         rosters[requested.first()] = {};
                     if (rosters.size() != requested.size())
                         batch->errors << "Some requested author teams were not returned.";
                     for (auto it = batch->projectTeams.cbegin(); it != batch->projectTeams.cend(); ++it)
                         if (rosters.contains(it.value())) {
                             auto &project = batch->projects["modrinth:" + it.key()];
                             authors(project, ProjectAuthors::modrinthTeam(rosters.value(it.value())));
                             project["_complete"] = true;
                         }
                 }
                 if (--batch->pending == 0)
                     finish(batch);
             });
    }
}
void ContentMetadataSource::applyProjects(const std::shared_ptr<Batch> &batch) {
    for (auto &value : batch->rows) {
        auto row = value.toMap();
        const auto provider = primary(row);
        const auto ids = row.value("sourceIds").toMap();
        const auto id = ids.value(provider).toString();
        auto urls = row.value("sourceUrls").toMap();
        for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
            const auto url =
                batch->projects.value(it.key() + ':' + it.value().toString()).value("projectUrl").toString();
            if (!url.isEmpty())
                urls[it.key()] = url;
        }
        if (!urls.isEmpty())
            row["sourceUrls"] = urls;
        const auto project = batch->projects.value(provider + ':' + id);
        if (!project.isEmpty()) {
            for (auto it = project.cbegin(); it != project.cend(); ++it)
                if (it.key() != "_complete")
                    row[it.key()] = it.value();
            row["metadataResolved"] = project.value("_complete").toBool();
            if (row.value("metadataResolved").toBool())
                row["metadataSha1"] = row.value("sha1");
            row["identityResolved"] = true;
            row["identitySha1"] = row.value("sha1");
        }
        batch->dirty |= row != value.toMap();
        value = row;
    }
}
void ContentMetadataSource::finish(const std::shared_ptr<Batch> &batch) {
    applyProjects(batch);
    const QPointer<ContentMetadataSource> guard(this);
    publish(batch);
    if (!guard)
        return;
    batch->errors.removeDuplicates();
    if (!m_index || !batch->dirty) {
        batch->callback(batch->rows, batch->errors.join('\n'));
        return;
    }
    const auto index = m_index;
    const auto rows = batch->rows;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [watcher, batch] {
        const auto error = watcher->result();
        watcher->deleteLater();
        if (!error.isEmpty())
            batch->errors << "Local content index: " + error;
        batch->callback(batch->rows, batch->errors.join('\n'));
    });
    watcher->setFuture(QtConcurrent::run([index, rows] {
        try {
            // Identity completion may arrive after a newer filesystem snapshot.
            // Instance membership belongs to lookup, never to this late provider response.
            return index->save({}, rows);
        } catch (...) {
            return QString("Could not save cached content metadata.");
        }
    }));
}
} // namespace studio
