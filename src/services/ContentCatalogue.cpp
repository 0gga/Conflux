#include "ContentCatalogue.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include "domain/LicensePolicy.h"
#include "domain/ReleaseStages.h"
#include "platform/ProjectAuthors.h"
#include <QJsonArray>
#include <QNetworkReply>
#include <QPointer>
#include <QUrlQuery>
#include <memory>
namespace studio {

ContentCatalogue::ContentCatalogue(QObject *parent, QUrl api, QUrl curseforgeApi,
                                   std::shared_ptr<const IContentIndex> index,
                                   QHash<QString, QString> reviewedIdentities)
    : QObject(parent), m_network(this), m_api(std::move(api)),
      m_metadata(std::move(index), this, m_api, curseforgeApi, std::move(reviewedIdentities)),
      m_client(this, m_api, curseforgeApi), m_versionClient(this, m_api, curseforgeApi) {
    setCurseforgeKey(qgetenv("CURSEFORGE_API_KEY"));
    connect(&m_versionClient, &CatalogueClient::detailFinished, this,
            [this](int generation, const ProjectDetail &detail) {
                const auto callback = m_versions.take(generation);
                if (callback)
                    callback(detail.releases, detail.errors.join('\n'));
            });
}
void ContentCatalogue::request(const QString &path, const QJsonObject *body, JsonCallback callback) {
    QNetworkRequest request(m_api.resolved(QUrl(path)));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(20000);
    auto *reply = body ? m_network.post(request, QJsonDocument(*body).toJson(QJsonDocument::Compact))
                       : m_network.get(request);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 32 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
        QJsonParseError parse;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &parse);
        QString error;
        if (reply->error() != QNetworkReply::NoError)
            error = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429
                        ? "Modrinth rate limit reached. Retry Refresh later."
                        : reply->errorString();
        else if (parse.error != QJsonParseError::NoError)
            error = "Modrinth returned invalid JSON.";
        callback(document, error);
        reply->deleteLater();
    });
}
void ContentCatalogue::refresh(const QVariantList &input, const QString &game, const QString &loader,
                               IndexCallback complete) {
    refreshWithChannels(input, game, loader, {}, std::move(complete));
}
void ContentCatalogue::identify(const QVariantMap &pack, const QVariantList &rows, IndexCallback callback,
                                IndexProgress progress) {
    m_metadata.resolve(pack, rows, std::move(callback), std::move(progress));
}
void ContentCatalogue::refreshWithChannels(const QVariantList &input, const QString &game,
                                           const QString &loader, const QStringList &selection,
                                           IndexCallback callback) {
    const QPointer<ContentCatalogue> guard(this);
    identify({}, input, [guard, game, loader, selection, callback](QVariantList rows, QString metadataError) {
        if (!guard)
            return;
        guard->checkUpdates(rows, game, loader, selection,
                            [callback, metadataError](QVariantList updated, QString updateError) {
                                QStringList errors;
                                if (!metadataError.isEmpty())
                                    errors << metadataError;
                                if (!updateError.isEmpty())
                                    errors << updateError;
                                callback(std::move(updated), errors.join('\n'));
                            });
    });
}
void ContentCatalogue::checkUpdates(const QVariantList &input, const QString &game, const QString &loader,
                                    const QStringList &selection, IndexCallback callback) {
    struct Group {
        QString category;
        QStringList loaders, channels;
        QJsonArray hashes;
        QSet<QString> seen;
    };
    struct Batch {
        QVariantList rows;
        QStringList errors;
        int pending = 0;
    };
    auto batch = std::make_shared<Batch>();
    batch->rows = input;
    QMap<QString, Group> groups;
    const auto allowedChannels = effectiveReleaseStages(selection);
    for (auto &value : batch->rows) {
        auto row = value.toMap();
        row["updateAvailable"] = false;
        row.remove("updateRelease");
        value = row;
        const auto hash = row.value("sha1").toString();
        const auto installed = releaseFromVariant(row.value("installedRelease").toMap());
        if (game.isEmpty() || installed.provider != "modrinth" || installed.projectId.isEmpty() ||
            installed.sha1.compare(hash, Qt::CaseInsensitive) || row.value("directory").toBool())
            continue;
        const auto category = row.value("category").toString();
        QStringList loaders;
        if (category == "mods") {
            const auto permitted = loader.split('|', Qt::SkipEmptyParts);
            for (const auto &candidate : permitted)
                if (candidate != "vanilla" && installed.loaders.contains(candidate))
                    loaders << candidate;
            if (!permitted.isEmpty() && loaders.contains(permitted.first()))
                loaders = {permitted.first()};
        } else if (category == "datapacks")
            loaders = {"datapack"};
        else if (category == "resourcepacks")
            loaders = {"minecraft"};
        else if (category == "shaderpacks")
            loaders = installed.loaders;
        if (loaders.isEmpty())
            continue;
        loaders.sort();
        const auto key = category + '|' + loaders.join('|') + '|' + allowedChannels.join(',');
        auto &group = groups[key];
        group.category = category;
        group.loaders = loaders;
        group.channels = allowedChannels;
        if (!group.seen.contains(hash)) {
            group.seen.insert(hash);
            group.hashes << hash;
        }
    }
    const QPointer<ContentCatalogue> guard(this);
    auto finish = [guard, batch, game, loader, selection, callback] {
        if (guard)
            guard->refreshCurseforge(batch->rows, game, loader, callback, batch->errors.join('\n'),
                                     selection);
    };
    if (groups.isEmpty()) {
        finish();
        return;
    }
    for (const auto &group : groups)
        batch->pending += (group.hashes.size() + 499) / 500;
    for (const auto &group : groups)
        for (int start = 0; start < group.hashes.size(); start += 500) {
            QJsonArray hashes;
            for (int i = start; i < qMin(start + 500, int(group.hashes.size())); ++i)
                hashes << group.hashes[i];
            QJsonObject body{{"hashes", hashes},
                             {"algorithm", "sha1"},
                             {"game_versions", QJsonArray{game}},
                             {"loaders", QJsonArray::fromStringList(group.loaders)},
                             {"version_types", QJsonArray::fromStringList(group.channels)}};
            request("version_files/update", &body,
                    [batch, finish, hashes, group, game](QJsonDocument document, QString error) {
                        if (!error.isEmpty())
                            batch->errors << "Update check: " + error;
                        const auto values = document.object();
                        for (auto &value : batch->rows) {
                            auto row = value.toMap();
                            const auto hash = row.value("sha1").toString();
                            if (row.value("category") != group.category || !hashes.contains(hash))
                                continue;
                            const auto installed = releaseFromVariant(row.value("installedRelease").toMap());
                            const auto candidate = modrinthRelease(values.value(hash).toObject());
                            if (candidate.projectId == installed.projectId &&
                                group.channels.contains(candidate.channel) &&
                                candidate.gameVersions.contains(game) &&
                                sharesLoader(candidate.loaders, group.loaders) &&
                                candidate.filename.endsWith(group.category == "mods" ? ".jar" : ".zip",
                                                            Qt::CaseInsensitive) &&
                                !candidate.url.isEmpty() && !candidate.sha1.isEmpty() &&
                                isNewerRelease(installed, candidate)) {
                                row["updateAvailable"] = true;
                                row["updateRelease"] = candidate.toVariant();
                                value = row;
                            }
                        }
                        if (--batch->pending == 0)
                            finish();
                    });
        }
}
void ContentCatalogue::refreshCurseforge(QVariantList rows, const QString &game, const QString &loader,
                                         IndexCallback callback, const QString &previousError,
                                         const QStringList &selection) {
    struct Batch {
        QVariantList rows;
        QList<int> pending;
        int active = 0;
        QStringList errors;
    };
    auto batch = std::make_shared<Batch>();
    batch->rows = std::move(rows);
    if (!previousError.isEmpty())
        batch->errors << previousError;
    if (m_client.hasCurseforgeKey())
        for (int i = 0; i < batch->rows.size(); ++i) {
            const auto row = batch->rows[i].toMap();
            const auto installed = releaseFromVariant(row.value("installedRelease").toMap());
            if (installed.provider == "curseforge" && !installed.projectId.isEmpty() &&
                installed.sha1.compare(row.value("sha1").toString(), Qt::CaseInsensitive) == 0)
                batch->pending << i;
        }
    auto pump = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = pump;
    QPointer<ContentCatalogue> guard(this);
    *pump = [guard, batch, game, loader, callback, weak, selection] {
        if (!guard)
            return;
        while (batch->active < 2 && !batch->pending.isEmpty()) {
            const int index = batch->pending.takeFirst();
            auto row = batch->rows[index].toMap();
            const auto installed = releaseFromVariant(row.value("installedRelease").toMap());
            QStringList loaders = categoryType(row.value("category").toString()) == "mod"
                                      ? loader.split('|', Qt::SkipEmptyParts)
                                      : QStringList{};
            if (!installed.loaders.isEmpty()) {
                QStringList shared;
                for (const auto &value : loaders)
                    if (installed.loaders.contains(value))
                        shared << value;
                loaders = shared;
                if (categoryType(row.value("category").toString()) == "mod" && loaders.isEmpty())
                    continue;
            }
            ++batch->active;
            auto next = weak.lock();
            guard->m_client.curseforgeVersions(
                installed.projectId, game, loaders,
                [guard, batch, index, installed, next, selection](QVector<Release> releases, QString error) {
                    if (!guard)
                        return;
                    auto row = batch->rows[index].toMap();
                    row["updateAvailable"] = false;
                    row.remove("updateRelease");
                    Release newest;
                    const auto allowedChannels = effectiveReleaseStages(selection);
                    for (const auto &release : releases)
                        if (allowedChannels.contains(release.channel) && !release.url.isEmpty() &&
                            !release.sha1.isEmpty() && isNewerRelease(installed, release) &&
                            (newest.id.isEmpty() || isNewerRelease(newest, release)))
                            newest = release;
                    if (!newest.id.isEmpty()) {
                        row["updateAvailable"] = true;
                        row["updateRelease"] = newest.toVariant();
                    }
                    batch->rows[index] = row;
                    if (!error.isEmpty())
                        batch->errors << "CurseForge updates: " + error;
                    --batch->active;
                    if (next)
                        (*next)();
                });
        }
        if (!batch->active && batch->pending.isEmpty())
            callback(batch->rows, batch->errors.join('\n'));
    };
    (*pump)();
}
void ContentCatalogue::cancelVersions() {
    const auto callbacks = std::move(m_versions);
    m_versions.clear();
    m_versionClient.cancelPendingReads();
    for (const auto &callback : callbacks)
        if (callback)
            callback({}, "Version lookup cancelled.");
}
void ContentCatalogue::versions(const QVariantMap &row, const QString &game, const QString &loader,
                                VersionsCallback callback) {
    Project project;
    project.type = categoryType(row.value("category").toString());
    const auto ids = row.value("sourceIds").toMap();
    for (auto it = ids.begin(); it != ids.end(); ++it)
        project.sourceIds[it.key()] = it.value().toString();
    if (project.sourceIds.isEmpty()) {
        callback({}, "No verified provider project is available for this file.");
        return;
    }
    const int generation = ++m_generation;
    m_versions[generation] = std::move(callback);
    m_versionClient.loadProject(project, game, loader, generation);
}
} // namespace studio
