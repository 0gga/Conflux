#include "ContentCatalogue.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include "domain/LicensePolicy.h"
#include "platform/ProjectAuthors.h"
#include <QJsonArray>
#include <QNetworkReply>
#include <QPointer>
#include <QUrlQuery>
#include <memory>
namespace studio {
ContentCatalogue::ContentCatalogue(QObject *parent, QUrl api, QUrl curseforgeApi)
    : QObject(parent), m_network(this), m_api(std::move(api)),
      m_client(this, m_api, std::move(curseforgeApi)) {
    m_client.setCurseforgeKey(qgetenv("CURSEFORGE_API_KEY"));
    connect(&m_client, &CatalogueClient::detailFinished, this,
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
    const IndexCallback callback = [this, game, loader, complete](QVariantList rows, QString error) {
        refreshCurseforge(std::move(rows), game, loader, complete, error);
    };
    struct Batch {
        QVariantList rows;
        QJsonObject installed, latest, projects;
        QStringList errors;
    };
    auto batch = std::make_shared<Batch>();
    batch->rows = input;
    QJsonArray hashes;
    for (const auto &value : input) {
        const auto row = value.toMap();
        if (!row.value("sha1").toString().isEmpty())
            hashes << row.value("sha1").toString();
    }
    if (hashes.isEmpty()) {
        callback(input, {});
        return;
    }
    QJsonObject body{{"hashes", hashes}, {"algorithm", "sha1"}};
    request("version_files", &body,
            [this, batch, game, loader, hashes, callback](const QJsonDocument &doc, const QString &error) {
                if (!error.isEmpty())
                    batch->errors << "Identification: " + error;
                batch->installed = doc.object();
                QJsonArray ids;
                for (const auto &version : batch->installed) {
                    const auto id = version.toObject().value("project_id");
                    if (!ids.contains(id))
                        ids << id;
                }
                const auto finish = [batch, callback](const QJsonObject &teams) {
                    for (int index = 0; index < batch->rows.size(); ++index) {
                        auto row = batch->rows.at(index).toMap();
                        row["updateAvailable"] = false;
                        row.remove("updateRelease");
                        const auto hash = row.value("sha1").toString();
                        const auto installed = batch->installed.value(hash).toObject();
                        if (!installed.isEmpty()) {
                            const auto id = installed.value("project_id").toString();
                            auto sources = row.value("sourceIds").toMap();
                            sources["modrinth"] = id;
                            row["sourceIds"] = sources;
                            row["provider"] = "modrinth";
                            row["projectId"] = id;
                            row["external"] = false;
                            row["installedRelease"] = modrinthRelease(installed, hash).toVariant();
                            row["version"] = installed.value("version_number").toString();
                            const auto project = batch->projects.value(id).toObject();
                            row["projectUrl"] = "https://modrinth.com/" +
                                                categoryType(row.value("category").toString()) + "/" + id;
                            if (!project.isEmpty()) {
                                row["title"] = project.value("title").toString(row.value("title").toString());
                                if (!project.value("icon_url").toString().isEmpty())
                                    row["icon"] = project.value("icon_url");
                                const auto license =
                                    project.value("license").toObject().value("id").toString();
                                row["openSource"] = isKnownOpenSourceLicense(license);
                                const auto environments = project.value("environment").toArray();
                                if (environments.contains("client_only") ||
                                    environments.contains("client_only_server_optional") ||
                                    environments.contains("singleplayer_only"))
                                    row["environment"] = "client";
                                else if (environments.contains("server_only") ||
                                         environments.contains("dedicated_server_only") ||
                                         environments.contains("server_only_client_optional"))
                                    row["environment"] = "server";
                                else if (environments.contains("client_and_server") ||
                                         environments.contains("client_or_server") ||
                                         environments.contains("client_or_server_prefers_both"))
                                    row["environment"] = "both";
                                else if (!project.value("client_side").toString().isEmpty() &&
                                         !project.value("server_side").toString().isEmpty()) {
                                    const bool client = project.value("client_side") != "unsupported",
                                               server = project.value("server_side") != "unsupported";
                                    row["environment"] = client && server ? "both"
                                                         : client         ? "client"
                                                         : server         ? "server"
                                                                          : "unknown";
                                } else if (!environments.isEmpty())
                                    row["environment"] = "unknown";
                                const auto authors = ProjectAuthors::modrinthTeam(
                                    teams.value(project.value("team").toString()).toArray());
                                if (!authors.isEmpty()) {
                                    QStringList names;
                                    for (const auto &author : authors)
                                        names << author.toMap().value("name").toString();
                                    row["authors"] = authors;
                                    row["author"] = names.join(", ");
                                    row["authorIcon"] = authors.first().toMap().value("icon");
                                    row["authorIds"] = authors.first().toMap().value("ids");
                                }
                            }
                            const auto latest =
                                batch->latest.value(hash + '|' + row.value("category").toString()).toObject();
                            if (!latest.isEmpty() &&
                                latest.value("project_id") == installed.value("project_id")) {
                                row["updateRelease"] = modrinthRelease(latest).toVariant();
                                row["updateAvailable"] =
                                    isNewerRelease(modrinthRelease(installed, hash), modrinthRelease(latest));
                            }
                        }
                        batch->rows[index] = row;
                    }
                    callback(batch->rows, batch->errors.join('\n'));
                };
                auto loadMetadata = [this, batch, ids, finish] {
                    if (ids.isEmpty()) {
                        finish({});
                        return;
                    }
                    const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(
                        QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact))));
                    request("projects?ids=" + encoded, nullptr,
                            [this, batch, finish](const QJsonDocument &projects, const QString &error) {
                                if (!error.isEmpty())
                                    batch->errors << "Project metadata: " + error;
                                QJsonArray teams;
                                for (const auto &value : projects.array()) {
                                    const auto project = value.toObject();
                                    batch->projects[project.value("id").toString()] = project;
                                    if (!teams.contains(project.value("team")))
                                        teams << project.value("team");
                                }
                                if (teams.isEmpty()) {
                                    finish({});
                                    return;
                                }
                                const auto encoded = QString::fromLatin1(QUrl::toPercentEncoding(
                                    QString::fromUtf8(QJsonDocument(teams).toJson(QJsonDocument::Compact))));
                                request("teams?ids=" + encoded, nullptr,
                                        [batch, finish](const QJsonDocument &members, const QString &error) {
                                            if (!error.isEmpty())
                                                batch->errors << "Authors: " + error;
                                            QJsonObject teams;
                                            for (const auto &team : members.array())
                                                for (const auto &value : team.toArray()) {
                                                    const auto member = value.toObject();
                                                    const auto id = member.value("team_id").toString();
                                                    auto roster = teams.value(id).toArray();
                                                    roster << member;
                                                    teams[id] = roster;
                                                }
                                            finish(teams);
                                        });
                            });
                };
                QMap<QString, QJsonArray> groups;
                QMap<QString, QStringList> permitted;
                if (!game.isEmpty())
                    for (const auto &value : batch->rows) {
                        const auto row = value.toMap();
                        const auto hash = row.value("sha1").toString();
                        const auto installed = batch->installed.value(hash).toObject();
                        if (installed.isEmpty() || row.value("directory").toBool())
                            continue;
                        const auto category = row.value("category").toString();
                        QStringList loaders;
                        if (category == "mods") {
                            const auto permitted = loader.split('|', Qt::SkipEmptyParts);
                            const auto installedLoaders = modrinthRelease(installed, hash).loaders;
                            // Automatic updates keep the installed edition. Connector expands discovery
                            // compatibility, but must not switch a native mod to its Fabric upload.
                            for (const auto &candidate : permitted)
                                if (candidate != "vanilla" && installedLoaders.contains(candidate))
                                    loaders << candidate;
                            if (!permitted.isEmpty() && loaders.contains(permitted.first()))
                                loaders = {permitted.first()};
                        } else if (category == "datapacks")
                            loaders = {"datapack"};
                        else if (category == "resourcepacks")
                            loaders = {"minecraft"};
                        else if (category == "shaderpacks")
                            loaders = modrinthRelease(installed, hash).loaders;
                        if (loaders.isEmpty())
                            continue;
                        loaders.sort();
                        const auto key = category + '|' + loaders.join('|');
                        if (!groups[key].contains(hash))
                            groups[key] << hash;
                        permitted[key] = loaders;
                    }
                if (groups.isEmpty()) {
                    loadMetadata();
                    return;
                }
                auto pending = std::make_shared<int>(groups.size());
                for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
                    const auto category = it.key().section('|', 0, 0);
                    const auto loaders = permitted.value(it.key());
                    QJsonObject updates{{"hashes", it.value()},
                                        {"algorithm", "sha1"},
                                        {"game_versions", QJsonArray{game}},
                                        {"loaders", QJsonArray::fromStringList(loaders)},
                                        {"version_types", QJsonArray{"release"}}};
                    request("version_files/update", &updates,
                            [batch, loadMetadata, pending, category, loaders, game](const QJsonDocument &doc,
                                                                                    const QString &error) {
                                if (!error.isEmpty())
                                    batch->errors << "Update check: " + error;
                                const auto values = doc.object();
                                for (auto it = values.begin(); it != values.end(); ++it) {
                                    const auto release = modrinthRelease(it.value().toObject());
                                    const auto extension = category == "mods" ? ".jar" : ".zip";
                                    if (release.channel == "release" && release.gameVersions.contains(game) &&
                                        sharesLoader(release.loaders, loaders) &&
                                        release.filename.endsWith(extension, Qt::CaseInsensitive))
                                        batch->latest[it.key() + '|' + category] = it.value();
                                }
                                if (--*pending == 0)
                                    loadMetadata();
                            });
                }
            });
}
void ContentCatalogue::refreshCurseforge(QVariantList rows, const QString &game, const QString &loader,
                                         IndexCallback callback, const QString &previousError) {
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
    *pump = [guard, batch, game, loader, callback, weak] {
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
                [guard, batch, index, installed, next](QVector<Release> releases, QString error) {
                    if (!guard)
                        return;
                    auto row = batch->rows[index].toMap();
                    row["updateAvailable"] = false;
                    row.remove("updateRelease");
                    Release newest;
                    for (const auto &release : releases)
                        if (release.channel == "release" && !release.url.isEmpty() &&
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
    m_client.loadProject(project, game, loader, generation);
}
} // namespace studio
