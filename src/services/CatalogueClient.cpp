#include "CatalogueClient.h"
#include <QJsonArray>
#include <QNetworkReply>
#include <QUrlQuery>
#include <memory>

namespace studio {
static QStringList strings(const QJsonArray& list) {
    QStringList result;
    for (const auto& item : list) result << item.toString();
    return result;
}
static QString jsonArray(const QString& item) { return QString::fromUtf8(QJsonDocument(QJsonArray{item}).toJson(QJsonDocument::Compact)); }
static int loaderNumber(const QString& loader) {
    return QMap<QString, int>{{"forge", 1}, {"fabric", 4}, {"quilt", 5}, {"neoforge", 6}}.value(loader);
}
CatalogueClient::CatalogueClient(QObject* parent) : QObject(parent), m_network(this) {}
void CatalogueClient::get(const QUrl& url, bool cf, JsonCallback callback) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "ModpackStudio/0.1.0 (desktop prototype)");
    request.setRawHeader("Accept", "application/json");
    if (cf) request.setRawHeader("x-api-key", m_curseforgeKey);
    request.setTransferTimeout(20000);
    auto* reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto error = reply->error() == QNetworkReply::NoError ? QString{} : reply->errorString();
        if (status == 429) error = "Rate limit reached. Wait a moment before searching again.";
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (error.isEmpty() && parseError.error != QJsonParseError::NoError) error = "The provider returned invalid JSON.";
        callback(document, error);
        reply->deleteLater();
    });
}
void CatalogueClient::search(const QString& query, const QString& source, const QString& game, const QString& loader, int generation) {
    struct Batch { int pending = 0; QVector<Project> projects; QStringList errors; };
    auto batch = std::make_shared<Batch>();
    const bool mr = source != "curseforge", cf = source != "modrinth" && hasCurseforgeKey();
    batch->pending = int(mr) + int(cf);
    if (source != "modrinth" && !hasCurseforgeKey()) batch->errors << "CurseForge needs an approved API key. Available results are from Modrinth.";
    auto done = [this, batch, generation] { if (--batch->pending == 0) emit searchFinished(generation, batch->projects, batch->errors); };
    if (batch->pending == 0) { emit searchFinished(generation, {}, batch->errors); return; }
    if (mr) {
        QUrl url("https://api.modrinth.com/v2/search");
        QJsonArray facets{QJsonArray{"project_type:mod"}};
        if (!game.isEmpty()) facets.append(QJsonArray{"versions:" + game});
        if (!loader.isEmpty() && loader != "vanilla") facets.append(QJsonArray{"categories:" + loader});
        QUrlQuery params; params.addQueryItem("query", query); params.addQueryItem("limit", "40");
        params.addQueryItem("index", query.isEmpty() ? "downloads" : "relevance");
        params.addQueryItem("facets", QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
        url.setQuery(params);
        get(url, false, [batch, done](const QJsonDocument& doc, const QString& error) {
            if (!error.isEmpty()) batch->errors << "Modrinth: " + error;
            else for (const auto& value : doc.object().value("hits").toArray()) {
                const auto row = value.toObject();
                Project p; p.provider = "modrinth"; p.id = row.value("project_id").toString();
                p.title = row.value("title").toString(); p.summary = row.value("description").toString();
                p.author = row.value("author").toString(); p.icon = row.value("icon_url").toString();
                p.url = "https://modrinth.com/mod/" + p.id; p.downloads = row.value("downloads").toInteger();
                p.categories = strings(row.value("display_categories").toArray()); batch->projects.append(p);
            }
            done();
        });
    }
    if (cf) {
        QUrl url("https://api.curseforge.com/v1/mods/search");
        QUrlQuery params; params.addQueryItem("gameId", "432"); params.addQueryItem("classId", "6");
        params.addQueryItem("searchFilter", query); params.addQueryItem("pageSize", "40");
        params.addQueryItem("sortField", "2"); params.addQueryItem("sortOrder", "desc");
        if (!game.isEmpty()) params.addQueryItem("gameVersion", game);
        if (loaderNumber(loader)) params.addQueryItem("modLoaderType", QString::number(loaderNumber(loader)));
        url.setQuery(params);
        get(url, true, [batch, done](const QJsonDocument& doc, const QString& error) {
            if (!error.isEmpty()) batch->errors << "CurseForge: " + error;
            else for (const auto& value : doc.object().value("data").toArray()) {
                const auto row = value.toObject();
                Project p; p.provider = "curseforge"; p.id = QString::number(row.value("id").toInteger());
                p.title = row.value("name").toString(); p.summary = row.value("summary").toString();
                p.icon = row.value("logo").toObject().value("url").toString();
                p.url = row.value("links").toObject().value("websiteUrl").toString();
                p.downloads = row.value("downloadCount").toInteger();
                const auto authors = row.value("authors").toArray();
                if (!authors.isEmpty()) p.author = authors.first().toObject().value("name").toString();
                for (const auto& category : row.value("categories").toArray()) p.categories << category.toObject().value("name").toString();
                batch->projects.append(p);
            }
            done();
        });
    }
}
void CatalogueClient::loadProject(const Project& project, const QString& game, const QString& loader, int generation) {
    struct Batch { int pending = 0; ProjectDetail detail; };
    auto batch = std::make_shared<Batch>();
    batch->pending = project.sourceIds.size() * 2;
    auto done = [this, batch, generation] {
        if (--batch->pending == 0) {
            batch->detail.releases = mergeReleases(batch->detail.releases);
            batch->detail.images.removeDuplicates();
            emit detailFinished(generation, batch->detail);
        }
    };
    if (!batch->pending) { emit detailFinished(generation, {}); return; }
    for (auto it = project.sourceIds.cbegin(); it != project.sourceIds.cend(); ++it) {
        const bool cf = it.key() == "curseforge";
        const auto id = it.value();
        if (cf && !hasCurseforgeKey()) { done(); done(); continue; }
        const auto base = cf ? "https://api.curseforge.com/v1/mods/" + id : "https://api.modrinth.com/v2/project/" + id;
        get(QUrl(base + (cf ? "/description" : "")), cf, [batch, done, cf, project](const QJsonDocument& doc, const QString& error) {
            if (!error.isEmpty()) batch->detail.errors << error;
            else if (cf) {
                if (!project.sourceIds.contains("modrinth")) batch->detail.description = doc.object().value("data").toString();
            } else {
                batch->detail.description = doc.object().value("body").toString();
                for (const auto& image : doc.object().value("gallery").toArray()) batch->detail.images << image.toObject().value("url").toString();
            }
            done();
        });
        QUrl versions(base + (cf ? "/files" : "/version")); QUrlQuery params;
        if (!game.isEmpty()) params.addQueryItem(cf ? "gameVersion" : "game_versions", cf ? game : jsonArray(game));
        if (!loader.isEmpty() && loader != "vanilla") {
            if (cf && loaderNumber(loader)) params.addQueryItem("modLoaderType", QString::number(loaderNumber(loader)));
            if (!cf) params.addQueryItem("loaders", jsonArray(loader));
        }
        if (cf) params.addQueryItem("pageSize", "50");
        versions.setQuery(params);
        get(versions, cf, [batch, done, cf, id, loader](const QJsonDocument& doc, const QString& error) {
            if (!error.isEmpty()) batch->detail.errors << error;
            else for (const auto& item : (cf ? doc.object().value("data").toArray() : doc.array())) {
                const auto row = item.toObject();
                Release r; r.provider = cf ? "curseforge" : "modrinth"; r.projectId = id;
                r.id = cf ? QString::number(row.value("id").toInteger()) : row.value("id").toString();
                r.version = row.value(cf ? "displayName" : "version_number").toString();
                r.title = row.value(cf ? "displayName" : "name").toString();
                r.published = row.value(cf ? "fileDate" : "date_published").toString();
                r.gameVersions = strings(row.value(cf ? "gameVersions" : "game_versions").toArray());
                if (cf) {
                    // CF gameVersions contains loader labels too; retain only Minecraft version strings.
                    QStringList games;
                    for (const auto& version : r.gameVersions) if (version.startsWith("1.")) games << version;
                    r.gameVersions = games;
                    for (const auto& label : strings(row.value("gameVersions").toArray())) {
                        const auto candidate = label.toLower();
                        if (QStringList{"fabric", "forge", "neoforge", "quilt"}.contains(candidate)) r.loaders << candidate;
                    }
                    r.filename = row.value("fileName").toString(); r.url = row.value("downloadUrl").toString();
                    for (const auto& hash : row.value("hashes").toArray()) if (hash.toObject().value("algo").toInt() == 1) r.sha1 = hash.toObject().value("value").toString();
                    for (const auto& dep : row.value("dependencies").toArray()) if (dep.toObject().value("relationType").toInt() == 3)
                        r.requiredDependencies << QString::number(dep.toObject().value("modId").toInteger());
                } else {
                    r.loaders = strings(row.value("loaders").toArray());
                    const auto files = row.value("files").toArray();
                    QJsonObject primary;
                    for (const auto& file : files) { if (primary.isEmpty() || file.toObject().value("primary").toBool()) primary = file.toObject(); }
                    r.filename = primary.value("filename").toString(); r.url = primary.value("url").toString();
                    r.sha1 = primary.value("hashes").toObject().value("sha1").toString();
                    r.sha512 = primary.value("hashes").toObject().value("sha512").toString();
                    for (const auto& dep : row.value("dependencies").toArray()) if (dep.toObject().value("dependency_type").toString() == "required") {
                        auto dependency = dep.toObject().value("project_id").toString();
                        if (dependency.isEmpty()) dependency = "version:" + dep.toObject().value("version_id").toString();
                        r.requiredDependencies << dependency;
                    }
                }
                batch->detail.releases.append(r);
            }
            done();
        });
    }
}
}
