#include "CatalogueClient.h"
#include "domain/ContentCompatibility.h"
#include <QJsonArray>
#include <QNetworkReply>
#include <QUrlQuery>
#include <memory>

namespace studio {
static QStringList strings(const QJsonArray &list) {
    QStringList result;
    for (const auto &item : list)
        result << item.toString();
    return result;
}
static QString jsonArray(const QString &item) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{item}).toJson(QJsonDocument::Compact));
}
static int loaderNumber(const QString &loader) {
    return QMap<QString, int>{{"forge", 1}, {"fabric", 4}, {"quilt", 5}, {"neoforge", 6}}.value(loader);
}
CatalogueClient::CatalogueClient(QObject *parent) : QObject(parent), m_network(this) {}
void CatalogueClient::get(const QUrl &url, bool cf, JsonCallback callback, bool allowMissing) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
    request.setRawHeader("Accept", "application/json");
    if (cf)
        request.setRawHeader("x-api-key", m_curseforgeKey);
    request.setTransferTimeout(20000);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, allowMissing, callback = std::move(callback)] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (allowMissing && status == 404) {
            callback({}, {});
            reply->deleteLater();
            return;
        }
        auto error = reply->error() == QNetworkReply::NoError ? QString{} : reply->errorString();
        if (status == 429)
            error = "Rate limit reached. Wait a moment before searching again.";
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (error.isEmpty() && parseError.error != QJsonParseError::NoError)
            error = "The provider returned invalid JSON.";
        callback(document, error);
        reply->deleteLater();
    });
}
static QString compactJson(const QJsonArray &values) {
    return QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));
}
QUrl CatalogueClient::searchUrl(const CatalogueQuery &query, int offset, int limit, int classId) {
    const bool cf = query.provider == "curseforge";
    QUrl url(cf ? "https://api.curseforge.com/v1/mods/search" : "https://api.modrinth.com/v2/search");
    QUrlQuery params;
    if (!cf) {
        QJsonArray filters{
            QJsonArray{(query.type == "datapack" ? "all_project_types:" : "project_type:") + query.type}};
        auto group = [&filters](const QString &key, const QStringList &values) {
            QJsonArray options;
            for (const auto &value : values)
                if (!value.isEmpty())
                    options << key + ':' + value;
            if (!options.isEmpty())
                filters << options;
        };
        group("versions", query.versions);
        group("project_id", query.projectIds);
        if (query.type == "mod")
            group("categories", query.loaders);
        QStringList tags;
        for (const auto &tag : query.tags)
            if (tag.startsWith("mr:"))
                tags << tag.mid(3);
        group("categories", tags);
        params.addQueryItem("facets", compactJson(filters));
        params.addQueryItem("query", query.text);
        params.addQueryItem("index", query.sort);
        params.addQueryItem("offset", QString::number(qMax(0, offset)));
        params.addQueryItem("limit", QString::number(qBound(1, limit, 100)));
    } else {
        params.addQueryItem("gameId", "432");
        params.addQueryItem("classId", QString::number(classId));
        params.addQueryItem("searchFilter", query.text);
        params.addQueryItem("index", QString::number(qMax(0, offset)));
        params.addQueryItem("pageSize", QString::number(qBound(1, limit, 50)));
        params.addQueryItem("sortOrder", "desc");
        params.addQueryItem(
            "sortField",
            QString::number(QMap<QString, int>{
                {"relevance", 1}, {"downloads", 6}, {"follows", 2}, {"newest", 11}, {"updated", 3}}
                                .value(query.sort, 1)));
        if (!query.versions.isEmpty())
            params.addQueryItem("gameVersions",
                                compactJson(QJsonArray::fromStringList(query.versions.mid(0, 4))));
        QJsonArray loaders;
        if (query.type == "mod")
            for (const auto &loader : query.loaders)
                if (loaderNumber(loader))
                    loaders << loaderNumber(loader);
        if (!loaders.isEmpty())
            params.addQueryItem("modLoaderTypes", compactJson(loaders));
        QJsonArray tags;
        for (const auto &tag : query.tags)
            if (tag.startsWith("cf:"))
                tags << tag.mid(3).toInt();
        if (!tags.isEmpty())
            params.addQueryItem("categoryIds", compactJson(tags));
    }
    url.setQuery(params);
    return url;
}
void CatalogueClient::fetch(const CatalogueQuery &query, int offset, int limit, Callback callback) {
    const bool cf = query.provider == "curseforge";
    if (cf && !hasCurseforgeKey()) {
        callback({{}, 0, "CurseForge needs an approved API key."});
        return;
    }
    // Each provider uses its own taxonomy. Selections across providers form a union.
    bool ownTag = false;
    for (const auto &tag : query.tags)
        if (tag.startsWith(cf ? "cf:" : "mr:"))
            ownTag = true;
    if (!query.tags.isEmpty() && !ownTag) {
        callback({{}, 0, {}});
        return;
    }
    if (cf && (query.versions.size() > 4 || query.tags.size() > 10)) {
        callback({{}, 0, "CurseForge supports at most four selected versions and ten tags per request."});
        return;
    }
    auto search = [this, query, offset, limit, callback, cf](int classId) {
        get(searchUrl(query, offset, limit, classId), cf,
            [query, callback, cf](const QJsonDocument &doc, const QString &error) {
                CataloguePage result;
                result.error = error;
                result.total =
                    cf ? doc.object().value("pagination").toObject().value("totalCount").toInteger()
                       : doc.object().value("total_hits").toInteger();
                if (error.isEmpty())
                    for (const auto &value :
                         (cf ? doc.object().value("data").toArray() : doc.object().value("hits").toArray())) {
                        const auto row = value.toObject();
                        Project p;
                        p.provider = query.provider;
                        p.type = query.type;
                        p.id = cf ? QString::number(row.value("id").toInteger())
                                  : row.value("project_id").toString();
                        p.title = row.value(cf ? "name" : "title").toString();
                        p.summary = row.value(cf ? "summary" : "description").toString();
                        p.updated = row.value(cf ? "dateModified" : "date_modified").toString();
                        p.created = row.value(cf ? "dateCreated" : "date_created").toString();
                        p.downloads = row.value(cf ? "downloadCount" : "downloads").toInteger();
                        p.followers = row.value("follows").toInteger();
                        p.icon = cf ? row.value("logo").toObject().value("url").toString()
                                    : row.value("icon_url").toString();
                        p.url = cf ? row.value("links").toObject().value("websiteUrl").toString()
                                   : "https://modrinth.com/" + query.type + "/" + p.id;
                        p.author = cf ? (row.value("authors").toArray().isEmpty() ? QString{}
                                                                                  : row.value("authors")
                                                                                        .toArray()
                                                                                        .first()
                                                                                        .toObject()
                                                                                        .value("name")
                                                                                        .toString())
                                      : row.value("author").toString();
                        p.sourceIds[p.provider] = p.id;
                        p.sourceDownloads[p.provider] = p.downloads;
                        if (cf) {
                            for (const auto &category : row.value("categories").toArray())
                                p.categories << category.toObject().value("name").toString();
                            for (const auto &version : row.value("latestFilesIndexes").toArray()) {
                                const auto loader =
                                    QMap<int, QString>{
                                        {1, "forge"}, {4, "fabric"}, {5, "quilt"}, {6, "neoforge"}}
                                        .value(version.toObject().value("modLoader").toInt());
                                if (!loader.isEmpty() && !p.loaders.contains(loader))
                                    p.loaders << loader;
                            }
                        } else {
                            p.categories = strings(row.value("display_categories").toArray());
                            for (const auto &tag : strings(row.value("categories").toArray()))
                                if (QStringList{"fabric", "forge", "neoforge", "quilt", "iris", "optifine",
                                                "datapack"}
                                        .contains(tag))
                                    p.loaders << tag;
                            const auto environment = row.value("environment");
                            if (environment.isArray()) {
                                const auto sides = strings(environment.toArray());
                                p.environment =
                                    sides.contains("client_and_server") || sides.contains("client_or_server")
                                        ? "both"
                                    : sides.contains("client_only") ? "client"
                                    : sides.contains("server_only") ? "server"
                                                                    : "unknown";
                            } else
                                p.environment = environment.toString();
                        }
                        result.projects << p;
                    }
                callback(std::move(result));
            });
    };
    if (cf && query.type == "datapack")
        get(QUrl("https://api.curseforge.com/v1/categories?gameId=432&classesOnly=true"), true,
            [search, callback](QJsonDocument doc, QString error) {
                for (const auto &v : doc.object().value("data").toArray())
                    if (v.toObject().value("slug") == "data-packs") {
                        search(v.toObject().value("id").toInt());
                        return;
                    }
                callback({{}, 0, error.isEmpty() ? "CurseForge did not expose a Data Packs class." : error});
            });
    else
        search(query.type == "resourcepack" ? 12 : query.type == "shader" ? 6552 : 6);
}
void CatalogueClient::search(const QString &text, const QString &source, const QString &game,
                             const QString &loader, int generation, const QString &type) {
    struct Batch {
        int pending = 0;
        QVector<Project> rows;
        QStringList errors;
    };
    auto batch = std::make_shared<Batch>();
    QStringList providers;
    if (source != "curseforge")
        providers << "modrinth";
    if (source != "modrinth" && hasCurseforgeKey())
        providers << "curseforge";
    if (source != "modrinth" && !hasCurseforgeKey())
        batch->errors << "CurseForge needs an approved API key. Available results are from Modrinth.";
    batch->pending = providers.size();
    if (!batch->pending) {
        emit searchFinished(generation, {}, batch->errors);
        return;
    }
    for (const auto &provider : providers) {
        CatalogueQuery query;
        query.text = text;
        query.provider = provider;
        query.type = type;
        query.sort = text.isEmpty() ? "downloads" : "relevance";
        if (!game.isEmpty())
            query.versions << game;
        if (!loader.isEmpty() && loader != "vanilla")
            query.loaders << loader;
        fetch(query, 0, 40, [this, batch, generation](CataloguePage page) {
            batch->rows += page.projects;
            if (!page.error.isEmpty())
                batch->errors << page.error;
            if (--batch->pending == 0)
                emit searchFinished(generation, batch->rows, batch->errors);
        });
    }
}
void CatalogueClient::facets(std::function<void(QVariantMap, QString)> callback) {
    struct Batch {
        int pending = 2;
        QVariantMap options;
        QStringList errors;
    };
    auto batch = std::make_shared<Batch>();
    if (hasCurseforgeKey())
        ++batch->pending;
    auto done = [batch, callback] {
        if (--batch->pending == 0)
            callback(batch->options, batch->errors.join('\n'));
    };
    if (hasCurseforgeKey())
        get(QUrl("https://api.curseforge.com/v1/categories?gameId=432"), true,
            [batch, done](QJsonDocument doc, QString error) {
                QMap<int, QString> classes{{6, "mod"}, {12, "resourcepack"}, {6552, "shader"}};
                for (const auto &value : doc.object().value("data").toArray())
                    if (value.toObject().value("slug") == "data-packs")
                        classes[value.toObject().value("id").toInt()] = "datapack";
                QVariantList tags;
                for (const auto &value : doc.object().value("data").toArray()) {
                    const auto row = value.toObject();
                    const auto type = classes.value(row.value("classId").toInt());
                    if (!row.value("isClass").toBool() && !type.isEmpty())
                        tags << QVariantMap{{"key", "cf:" + QString::number(row.value("id").toInt())},
                                            {"title", row.value("name").toString().toLower()},
                                            {"type", type},
                                            {"provider", "curseforge"}};
                }
                batch->options["curseforgeTags"] = tags;
                if (!error.isEmpty())
                    batch->errors << "CurseForge tags: " + error;
                done();
            });
    get(QUrl("https://api.modrinth.com/v2/tag/game_version"), false,
        [batch, done](QJsonDocument doc, QString error) {
            QStringList versions;
            for (const auto &value : doc.array())
                versions << value.toObject().value("version").toString();
            batch->options["versions"] = versions;
            if (!error.isEmpty())
                batch->errors << error;
            done();
        });
    get(QUrl("https://api.modrinth.com/v2/tag/category"), false,
        [batch, done](QJsonDocument doc, QString error) {
            QVariantList tags;
            for (const auto &value : doc.array()) {
                const auto row = value.toObject();
                if (row.value("header") == "categories" || row.value("header") == "features" ||
                    row.value("header") == "resolutions" || row.value("header") == "performance impact")
                    tags << QVariantMap{{"key", "mr:" + row.value("name").toString()},
                                        {"title", row.value("name").toString()},
                                        {"type", row.value("project_type").toString()},
                                        {"provider", "modrinth"}};
            }
            batch->options["tags"] = tags;
            if (!error.isEmpty())
                batch->errors << error;
            done();
        });
}

void CatalogueClient::loadProject(const Project &project, const QString &game, const QString &requestedLoader,
                                  int generation) {
    const auto loader = project.type == "mod"        ? requestedLoader
                        : project.type == "datapack" ? QString("datapack")
                                                     : QString{};
    struct Batch {
        int pending = 0;
        ProjectDetail detail;
        QMap<QString, QStringList> images;
    };
    auto batch = std::make_shared<Batch>();
    for (auto it = project.sourceIds.cbegin(); it != project.sourceIds.cend(); ++it)
        batch->pending += it.key() == "curseforge" ? 3 : 2;
    auto done = [this, batch, generation] {
        if (--batch->pending == 0) {
            batch->detail.releases = mergeReleases(batch->detail.releases);
            batch->detail.images = batch->images.value("modrinth") + batch->images.value("curseforge");
            batch->detail.images.removeDuplicates();
            emit detailFinished(generation, batch->detail);
        }
    };
    if (!batch->pending) {
        emit detailFinished(generation, {});
        return;
    }
    for (auto it = project.sourceIds.cbegin(); it != project.sourceIds.cend(); ++it) {
        const bool cf = it.key() == "curseforge";
        const auto id = it.value();
        if (cf && !hasCurseforgeKey()) {
            batch->detail.errors << "CurseForge content needs an approved API key in Settings.";
            done();
            done();
            done();
            continue;
        }
        const auto base =
            cf ? "https://api.curseforge.com/v1/mods/" + id : "https://api.modrinth.com/v2/project/" + id;
        get(QUrl(base + (cf ? "/description" : "")), cf,
            [batch, done, cf, project](const QJsonDocument &doc, const QString &error) {
                if (!error.isEmpty())
                    batch->detail.errors << error;
                else if (cf) {
                    if (!project.sourceIds.contains("modrinth"))
                        batch->detail.description = doc.object().value("data").toString();
                } else {
                    batch->detail.description = doc.object().value("body").toString();
                    for (const auto &image : doc.object().value("gallery").toArray())
                        batch->images["modrinth"] << image.toObject().value("url").toString();
                }
                done();
            });
        if (cf)
            get(QUrl(base), true, [batch, done](const QJsonDocument &doc, const QString &error) {
                if (!error.isEmpty())
                    batch->detail.errors << "CurseForge gallery: " + error;
                else
                    for (const auto &image :
                         doc.object().value("data").toObject().value("screenshots").toArray())
                        batch->images["curseforge"] << image.toObject().value("url").toString();
                done();
            });
        QUrl versions(base + (cf ? "/files" : "/version"));
        QUrlQuery params;
        if (!game.isEmpty())
            params.addQueryItem(cf ? "gameVersion" : "game_versions", cf ? game : jsonArray(game));
        if (!loader.isEmpty() && loader != "vanilla") {
            if (cf && loaderNumber(loader))
                params.addQueryItem("modLoaderType", QString::number(loaderNumber(loader)));
            if (!cf) {
                QJsonArray loaders = QJsonArray::fromStringList(loader.split('|', Qt::SkipEmptyParts));
                if (project.type == "datapack")
                    for (const auto &candidate : requestedLoader.split('|', Qt::SkipEmptyParts))
                        if (candidate != "vanilla")
                            loaders << candidate;
                params.addQueryItem("loaders", compactJson(loaders));
            }
        }
        if (cf)
            params.addQueryItem("pageSize", "50");
        versions.setQuery(params);
        get(versions, cf,
            [batch, done, cf, id, loader, type = project.type](const QJsonDocument &doc,
                                                               const QString &error) {
                if (!error.isEmpty())
                    batch->detail.errors << error;
                else
                    for (const auto &item : (cf ? doc.object().value("data").toArray() : doc.array())) {
                        const auto row = item.toObject();
                        Release r;
                        r.provider = cf ? "curseforge" : "modrinth";
                        r.projectId = id;
                        r.id = cf ? QString::number(row.value("id").toInteger()) : row.value("id").toString();
                        r.version = row.value(cf ? "displayName" : "version_number").toString();
                        r.title = row.value(cf ? "displayName" : "name").toString();
                        r.published = row.value(cf ? "fileDate" : "date_published").toString();
                        r.gameVersions = strings(row.value(cf ? "gameVersions" : "game_versions").toArray());
                        if (cf) {
                            const auto channel = row.value("releaseType").toInt();
                            r.channel = channel == 1   ? "release"
                                        : channel == 2 ? "beta"
                                        : channel == 3 ? "alpha"
                                                       : "unknown";
                            // CF gameVersions contains loader labels too; retain only Minecraft version
                            // strings.
                            QStringList games;
                            for (const auto &version : r.gameVersions)
                                if (version.startsWith("1."))
                                    games << version;
                            r.gameVersions = games;
                            for (const auto &label : strings(row.value("gameVersions").toArray())) {
                                const auto candidate = label.toLower();
                                if (QStringList{"fabric", "forge", "neoforge", "quilt"}.contains(candidate))
                                    r.loaders << candidate;
                            }
                            r.filename = row.value("fileName").toString();
                            r.url = row.value("downloadUrl").toString();
                            for (const auto &hash : row.value("hashes").toArray())
                                if (hash.toObject().value("algo").toInt() == 1)
                                    r.sha1 = hash.toObject().value("value").toString();
                            for (const auto &dep : row.value("dependencies").toArray())
                                if (dep.toObject().value("relationType").toInt() == 3)
                                    r.requiredDependencies
                                        << QString::number(dep.toObject().value("modId").toInteger());
                        } else {
                            r = modrinthRelease(row);
                        }
                        if (cf && type == "mod" && !loader.isEmpty() && loader != "vanilla" &&
                            !sharesLoader(r.loaders, loader.split('|', Qt::SkipEmptyParts)))
                            continue;
                        batch->detail.releases.append(r);
                    }
                done();
            });
    }
}
void CatalogueClient::resolveProject(const QString &sha1, QMap<QString, QString> ids, int generation) {
    auto fetch = [this, generation](QMap<QString, QString> sourceIds, QString lookupError) {
        struct Batch {
            int pending = 0;
            QVector<Project> projects;
            QMap<QString, QString> sourceIds;
            QStringList errors;
        };
        auto batch = std::make_shared<Batch>();
        batch->sourceIds = sourceIds;
        if (!lookupError.isEmpty())
            batch->errors << lookupError;
        if (sourceIds.contains("curseforge") && !hasCurseforgeKey()) {
            batch->errors << "CurseForge needs an approved API key in Settings.";
            sourceIds.remove("curseforge");
        }
        batch->pending = sourceIds.size();
        auto done = [this, generation, batch] {
            if (--batch->pending != 0)
                return;
            Project merged;
            for (const auto &p : batch->projects) {
                if (merged.id.isEmpty() || p.provider == "modrinth")
                    merged = p;
            }
            merged.sourceIds = batch->sourceIds;
            for (const auto &p : batch->projects) {
                merged.sourceIds.insert(p.provider, p.id);
                merged.sourceUrls.insert(p.provider, p.url);
            }
            emit projectResolved(generation, merged, batch->errors.join('\n'));
        };
        if (sourceIds.isEmpty()) {
            emit projectResolved(generation, {}, batch->errors.join('\n'));
            return;
        }
        for (auto it = sourceIds.cbegin(); it != sourceIds.cend(); ++it) {
            const auto provider = it.key(), id = it.value();
            const bool cf = provider == "curseforge";
            get(QUrl(cf ? "https://api.curseforge.com/v1/mods/" + id
                        : "https://api.modrinth.com/v2/project/" + id),
                cf, [batch, done, cf, provider](const QJsonDocument &doc, const QString &error) {
                    if (!error.isEmpty())
                        batch->errors << provider + ": " + error;
                    else {
                        const auto row = cf ? doc.object().value("data").toObject() : doc.object();
                        Project p;
                        p.provider = provider;
                        p.id = cf ? QString::number(row.value("id").toInteger()) : row.value("id").toString();
                        p.type = cf ? (row.value("classId").toInt() == 12     ? "resourcepack"
                                       : row.value("classId").toInt() == 6552 ? "shader"
                                                                              : "mod")
                                    : row.value("project_type").toString("mod");
                        p.title = row.value(cf ? "name" : "title").toString();
                        p.summary = row.value(cf ? "summary" : "description").toString();
                        p.icon = cf ? row.value("logo").toObject().value("url").toString()
                                    : row.value("icon_url").toString();
                        p.url = cf ? row.value("links").toObject().value("websiteUrl").toString()
                                   : "https://modrinth.com/" + p.type + "/" + p.id;
                        if (!p.id.isEmpty() && p.id != "0")
                            batch->projects << p;
                    }
                    done();
                });
        }
    };
    if (sha1.isEmpty()) {
        fetch(ids, {});
        return;
    }
    get(
        QUrl("https://api.modrinth.com/v2/version_file/" + sha1 + "?algorithm=sha1"), false,
        [ids, fetch](const QJsonDocument &doc, const QString &error) mutable {
            const auto project = doc.object().value("project_id").toString();
            if (!project.isEmpty())
                ids.insert("modrinth", project);
            fetch(ids, error.isEmpty() ? QString{} : "Modrinth identification: " + error);
        },
        true);
}
} // namespace studio
