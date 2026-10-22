#include "CatalogueClient.h"
#include "domain/ContentCompatibility.h"
#include "platform/ProjectAuthors.h"
#include "platform/ReviewedAuthorIdentities.h"
#include <QFutureWatcher>
#include <QJsonArray>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent>
#include <algorithm>
#include <limits>
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
static bool matchesScopedCurseforgeProject(const QJsonObject &row, const CatalogueQuery &query, int classId) {
    if (!query.projectIds.contains(QString::number(row.value("id").toInteger())) ||
        row.value("gameId").toInt() != 432 ||
        (query.type != "all" && row.value("classId").toInt() != classId))
        return false;
    if (!query.text.isEmpty() && !row.value("name").toString().contains(query.text, Qt::CaseInsensitive) &&
        !row.value("summary").toString().contains(query.text, Qt::CaseInsensitive))
        return false;
    bool author = !query.authorIds.contains("curseforge");
    for (const auto &value : row.value("authors").toArray())
        author |=
            QString::number(value.toObject().value("id").toInteger()) == query.authorIds.value("curseforge");
    if (!author)
        return false;
    bool tags = query.tags.isEmpty();
    for (const auto &value : row.value("categories").toArray())
        tags |= query.tags.contains("cf:" + QString::number(value.toObject().value("id").toInteger()));
    if (!tags)
        return false;
    const auto loaders = query.type == "mod" ? query.loaders : QStringList{};
    if (query.versions.isEmpty() && loaders.isEmpty())
        return true;
    for (const auto &value : row.value("latestFilesIndexes").toArray()) {
        const auto file = value.toObject();
        bool loader = loaders.isEmpty();
        for (const auto &name : loaders)
            loader |= loaderNumber(name) == file.value("modLoader").toInt();
        if (loader &&
            (query.versions.isEmpty() || query.versions.contains(file.value("gameVersion").toString())))
            return true;
    }
    for (const auto &value : row.value("latestFiles").toArray()) {
        const auto release = curseforgeRelease(value.toObject());
        bool game = query.versions.isEmpty();
        for (const auto &version : query.versions)
            game |= release.gameVersions.contains(version);
        if (game && (loaders.isEmpty() || sharesLoader(release.loaders, loaders)))
            return true;
    }
    return false;
}
CatalogueClient::CatalogueClient(QObject *parent, QUrl modrinthApi, QUrl curseforgeApi)
    : QObject(parent), m_network(this), m_modrinthApi(std::move(modrinthApi)),
      m_curseforgeApi(std::move(curseforgeApi)) {
    m_cacheClock.start();
}
void CatalogueClient::setReadCacheLifetime(int milliseconds) {
    m_cacheLifetime = qBound(0, milliseconds, 300000);
    m_readCache.clear();
}
void CatalogueClient::setCurseforgeKey(const QByteArray &key) {
    if (key == m_curseforgeKey)
        return;
    m_curseforgeKey = key;
    ++m_authGeneration;
    m_readCache.clear();
}
void CatalogueClient::request(const QUrl &url, bool cf, JsonCallback callback, bool allowMissing,
                              const QByteArray &body) {
    const auto root = cf ? m_curseforgeApi : m_modrinthApi;
    auto relative = url.path();
    relative = relative.mid(4);
    QUrl endpoint = root.resolved(QUrl(relative));
    endpoint.setQuery(url.query());
    const int authGeneration = m_authGeneration;
    const QString cacheKey = QString::number(authGeneration) + ':' + endpoint.toString(QUrl::FullyEncoded) +
                             (allowMissing ? ":optional" : ":required");
    const bool cacheable = m_cacheLifetime > 0 && body.isEmpty();
    std::shared_ptr<PendingRead> pending;
    if (cacheable) {
        if (const auto *cached = m_readCache.object(cacheKey);
            cached && cached->expires > m_cacheClock.elapsed()) {
            QTimer::singleShot(
                0, this, [this, callback = std::move(callback), document = cached->document, authGeneration] {
                    if (authGeneration != m_authGeneration)
                        callback({}, "The provider connection changed. Reopen the content page.");
                    else
                        callback(document, {});
                });
            return;
        }
        if (auto existing = m_pendingReads.value(cacheKey)) {
            existing->callbacks << std::move(callback);
            return;
        }
        pending = std::make_shared<PendingRead>();
        pending->callbacks << std::move(callback);
        m_pendingReads.insert(cacheKey, pending);
    }
    auto deliver = [this, cacheable, cacheKey, pending, callback = std::move(callback),
                    authGeneration](QJsonDocument document, QString error, int bytes) {
        if (authGeneration != m_authGeneration) {
            document = {};
            error = "The provider connection changed. Reopen the content page.";
        }
        if (!cacheable) {
            callback(document, error);
            return;
        }
        m_pendingReads.remove(cacheKey);
        if (error.isEmpty() && !document.isNull() && authGeneration == m_authGeneration)
            m_readCache.insert(cacheKey,
                               new CachedResponse{document, m_cacheClock.elapsed() + m_cacheLifetime},
                               qMax(1024, bytes * 2));
        const auto callbacks = std::move(pending->callbacks);
        for (const auto &waiting : callbacks)
            waiting(document, error);
    };
    QNetworkRequest request(endpoint);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
    request.setRawHeader("Accept", "application/json");
    if (cf)
        request.setRawHeader("x-api-key", m_curseforgeKey);
    request.setTransferTimeout(20000);
    if (!body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto *reply = body.isEmpty() ? m_network.get(request) : m_network.post(request, body);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 32 * 1024 * 1024) {
            reply->setProperty("responseTooLarge", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, allowMissing, deliver] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (allowMissing && status == 404) {
            deliver({}, {}, 0);
            reply->deleteLater();
            return;
        }
        auto error = reply->error() == QNetworkReply::NoError ? QString{} : reply->errorString();
        if (reply->property("responseTooLarge").toBool())
            error = "The provider response exceeded the 32 MiB safety limit.";
        if (error.isEmpty() && status != 200)
            error = "Provider request failed (HTTP " + QString::number(status) + ").";
        if (status == 429)
            error = "Rate limit reached. Wait a moment before searching again.";
        if (!error.isEmpty()) {
            deliver({}, error, 0);
            reply->deleteLater();
            return;
        }
        const auto bytes = reply->readAll();
        auto parse = [bytes, error]() mutable {
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(bytes, &parseError);
            if (error.isEmpty() && parseError.error != QJsonParseError::NoError)
                error = "The provider returned invalid JSON.";
            return std::pair{document, error};
        };
        if (bytes.size() >= 64 * 1024) {
            auto *watcher = new QFutureWatcher<std::pair<QJsonDocument, QString>>(this);
            connect(watcher, &QFutureWatcherBase::finished, this, [watcher, deliver, size = bytes.size()] {
                const auto result = watcher->result();
                watcher->deleteLater();
                deliver(result.first, result.second, int(size));
            });
            watcher->setFuture(QtConcurrent::run(std::move(parse)));
        } else {
            const auto result = parse();
            deliver(result.first, result.second, int(bytes.size()));
        }
        reply->deleteLater();
    });
}
void CatalogueClient::linkCurseforgeFiles(CataloguePage page, QMap<QString, QStringList> hashes,
                                          Callback callback) {
    if (m_hashProjects.size() > 10000)
        m_hashProjects.clear();
    if (m_linkedProjects.size() > 10000)
        m_linkedProjects.clear();
    QStringList missing;
    for (const auto &files : hashes)
        for (const auto &hash : files)
            if (!m_hashProjects.contains(hash) && !missing.contains(hash) && missing.size() < 1000)
                missing << hash;
    const auto finish = [this, page, hashes, callback](QString error) {
        auto result = page;
        const auto pairs = verifiedProjectPairs(hashes, m_hashProjects);
        for (auto &project : result.projects) {
            const auto mr = pairs.value(project.id);
            if (mr.isEmpty() || (m_linkedProjects.contains(mr) && m_linkedProjects.value(mr) != project.id))
                continue;
            m_linkedProjects[mr] = project.id;
            project.sourceIds["modrinth"] = mr;
        }
        if (!error.isEmpty())
            result.error = "Cross-platform matching: " + error;
        callback(std::move(result));
    };
    if (missing.isEmpty()) {
        finish({});
        return;
    }
    const auto body =
        QJsonDocument(QJsonObject{{"hashes", QJsonArray::fromStringList(missing)}, {"algorithm", "sha1"}})
            .toJson(QJsonDocument::Compact);
    request(
        QUrl("https://api.modrinth.com/v2/version_files"), false,
        [this, missing, finish](QJsonDocument doc, QString error) {
            if (error.isEmpty()) {
                for (const auto &hash : missing) {
                    const auto version = doc.object().value(hash).toObject();
                    bool exact = false;
                    for (const auto &file : version.value("files").toArray())
                        exact |= file.toObject().value("hashes").toObject().value("sha1").toString().compare(
                                     hash, Qt::CaseInsensitive) == 0;
                    m_hashProjects[hash] = exact ? version.value("project_id").toString() : QString{};
                }
            }
            finish(error);
        },
        false, body);
}
void CatalogueClient::curseforgeVersions(const QString &id, const QString &game, const QStringList &loaders,
                                         std::function<void(QVector<Release>, QString)> callback) {
    if (!hasCurseforgeKey()) {
        // Keep the bounded update pump asynchronous if the key is removed mid-check.
        QTimer::singleShot(
            0, this, [callback] { callback({}, "Connect an approved CurseForge API key in Settings."); });
        return;
    }
    QUrl url("https://api.curseforge.com/v1/mods/" + id + "/files");
    QUrlQuery query;
    query.addQueryItem("gameVersion", game);
    query.addQueryItem("pageSize", "50");
    if (loaders.size() == 1 && loaderNumber(loaders.first()))
        query.addQueryItem("modLoaderType", QString::number(loaderNumber(loaders.first())));
    url.setQuery(query);
    request(url, true, [id, game, loaders, callback](QJsonDocument doc, QString error) {
        QVector<Release> releases;
        if (error.isEmpty())
            for (const auto &value : doc.object().value("data").toArray()) {
                const auto file = value.toObject();
                auto release = curseforgeRelease(file);
                if (release.projectId == id && release.gameVersions.contains(game) &&
                    (loaders.isEmpty() || sharesLoader(release.loaders, loaders)) &&
                    file.value("isAvailable") != false)
                    releases << release;
            }
        callback(releases, error);
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
        QJsonArray filters;
        if (query.type != "all")
            filters << QJsonArray{(query.type == "datapack" ? "all_project_types:" : "project_type:") +
                                  query.type};
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
        if (query.type != "all")
            params.addQueryItem("classId", QString::number(classId));
        if (query.authorIds.contains("curseforge"))
            params.addQueryItem("authorId", query.authorIds.value("curseforge"));
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
    if (!query.authorIds.isEmpty()) {
        const auto authors = ReviewedAuthorIdentities::expand(query.authorIds);
        const auto author = authors.value(query.provider);
        if (author.isEmpty()) {
            callback({{}, 0, {}});
            return;
        }
        if (cf && query.authorIds.value("curseforge") != author) {
            auto scoped = query;
            scoped.authorIds = authors;
            fetch(scoped, offset, limit, callback);
            return;
        }
        if (!cf) {
            request(QUrl("https://api.modrinth.com/v2/user/" +
                         QString::fromLatin1(QUrl::toPercentEncoding(author)) + "/projects"),
                    false,
                    [this, query, offset, limit, callback](const QJsonDocument &doc, const QString &error) {
                        if (!error.isEmpty()) {
                            callback({{}, 0, error});
                            return;
                        }
                        auto scoped = query;
                        scoped.authorIds.clear();
                        scoped.projectIds.clear();
                        for (const auto &value : doc.array()) {
                            const auto id = value.toObject().value("id").toString();
                            if (!id.isEmpty())
                                scoped.projectIds << id;
                        }
                        if (scoped.projectIds.isEmpty()) {
                            callback({{}, 0, {}});
                            return;
                        }
                        if (scoped.projectIds.size() > 1000) {
                            callback(
                                {{}, 0, "This author has more than1000 projects; narrow the content type."});
                            return;
                        }
                        fetch(scoped, offset, limit, callback);
                    });
            return;
        }
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
        auto url = searchUrl(query, offset, limit, classId);
        QByteArray body;
        if (cf && !query.projectIds.isEmpty()) {
            if (query.projectIds.size() > 1000) {
                callback({{}, 0, "Narrow the hidden-project filters to at most1000 projects."});
                return;
            }
            QJsonArray ids;
            for (const auto &id : query.projectIds) {
                bool valid = false;
                const auto number = id.toLongLong(&valid);
                if (!valid || number <= 0 || number > std::numeric_limits<int>::max()) {
                    callback({{}, 0, "This CurseForge project has no valid identity."});
                    return;
                }
                ids << number;
            }
            url = QUrl("https://api.curseforge.com/v1/mods");
            body = QJsonDocument(QJsonObject{{"modIds", ids}}).toJson(QJsonDocument::Compact);
        }
        request(
            url, cf,
            [this, query, offset, limit, classId, callback, cf](const QJsonDocument &doc,
                                                                const QString &error) {
                CataloguePage result;
                QMap<QString, QStringList> projectHashes;
                result.error = error;
                result.total =
                    cf ? doc.object().value("pagination").toObject().value("totalCount").toInteger()
                       : doc.object().value("total_hits").toInteger();
                if (error.isEmpty())
                    for (const auto &value :
                         (cf ? doc.object().value("data").toArray() : doc.object().value("hits").toArray())) {
                        const auto row = value.toObject();
                        if (cf && !query.projectIds.isEmpty() &&
                            !matchesScopedCurseforgeProject(row, query, classId))
                            continue;
                        Project p;
                        p.provider = query.provider;
                        p.type =
                            query.type == "all"
                                ? (cf ? QMap<int, QString>{{6, "mod"}, {12, "resourcepack"}, {6552, "shader"}}
                                            .value(row.value("classId").toInt(), "datapack")
                                      : row.value("project_type").toString("mod"))
                                : query.type;
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
                                   : "https://modrinth.com/" + p.type + "/" + p.id;
                        if (cf) {
                            p.authors = ProjectAuthors::curseforge(row.value("authors").toArray());
                            QStringList names;
                            for (const auto &author : p.authors)
                                names << author.toMap().value("name").toString();
                            p.author = names.join(", ");
                            if (!p.authors.isEmpty()) {
                                const auto ids = p.authors.first().toMap().value("ids").toMap();
                                for (auto it = ids.cbegin(); it != ids.cend(); ++it)
                                    p.authorIds[it.key()] = it.value().toString();
                            }
                        } else {
                            p.author = row.value("author").toString();
                            if (!p.author.isEmpty())
                                p.authorIds["modrinth"] = p.author;
                        }
                        p.sourceIds[p.provider] = p.id;
                        p.sourceDownloads[p.provider] = p.downloads;
                        if (cf) {
                            for (const auto &file : row.value("latestFiles").toArray())
                                for (const auto &value : file.toObject().value("hashes").toArray()) {
                                    const auto hash = value.toObject();
                                    if (hash.value("algo").toInt() == 1 &&
                                        QRegularExpression("^[a-fA-F0-9]{40}$")
                                            .match(hash.value("value").toString())
                                            .hasMatch())
                                        projectHashes[p.id] << hash.value("value").toString().toLower();
                                }
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
                if (cf && !query.projectIds.isEmpty()) {
                    std::stable_sort(result.projects.begin(), result.projects.end(),
                                     [query](const Project &a, const Project &b) {
                                         if (query.sort == "downloads")
                                             return a.totalDownloads() > b.totalDownloads();
                                         if (query.sort == "updated")
                                             return a.updated > b.updated;
                                         if (query.sort == "newest")
                                             return a.created > b.created;
                                         return query.projectIds.indexOf(a.id) <
                                                query.projectIds.indexOf(b.id);
                                     });
                    result.total = result.projects.size();
                    result.projects = result.projects.mid(qMax(0, offset), qBound(1, limit, 50));
                }
                if (cf && error.isEmpty())
                    linkCurseforgeFiles(std::move(result), projectHashes, callback);
                else
                    callback(std::move(result));
            },
            false, body);
    };
    if (cf && query.type == "datapack")
        request(QUrl("https://api.curseforge.com/v1/categories?gameId=432&classesOnly=true"), true,
                [search, callback](QJsonDocument doc, QString error) {
                    for (const auto &v : doc.object().value("data").toArray())
                        if (v.toObject().value("slug") == "data-packs") {
                            search(v.toObject().value("id").toInt());
                            return;
                        }
                    callback(
                        {{}, 0, error.isEmpty() ? "CurseForge did not expose a Data Packs class." : error});
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
        request(QUrl("https://api.curseforge.com/v1/categories?gameId=432"), true,
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
    request(QUrl("https://api.modrinth.com/v2/tag/game_version"), false,
            [batch, done](QJsonDocument doc, QString error) {
                QStringList versions;
                for (const auto &value : doc.array())
                    versions << value.toObject().value("version").toString();
                batch->options["versions"] = versions;
                if (!error.isEmpty())
                    batch->errors << error;
                done();
            });
    request(QUrl("https://api.modrinth.com/v2/tag/category"), false,
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

void CatalogueClient::loadProject(const Project &project, const QString &requestedGame,
                                  const QString &requestedLoader, int generation, bool allVersions) {
    const auto game = allVersions ? QString{} : requestedGame;
    const auto loader = allVersions                  ? QString{}
                        : project.type == "mod"      ? requestedLoader
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
        request(QUrl(base + (cf ? "/description" : "")), cf,
                [this, generation, batch, done, cf, project](const QJsonDocument &doc, const QString &error) {
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
                    if (error.isEmpty() && (!cf || !project.sourceIds.contains("modrinth")))
                        emit descriptionReady(generation, batch->detail.description);
                    done();
                });
        if (cf)
            request(QUrl(base), true, [batch, done](const QJsonDocument &doc, const QString &error) {
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
        request(
            versions, cf,
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
                        r = cf ? curseforgeRelease(row) : modrinthRelease(row);
                        if (cf && type == "mod" && !loader.isEmpty() && loader != "vanilla" &&
                            !sharesLoader(r.loaders, loader.split('|', Qt::SkipEmptyParts)))
                            continue;
                        batch->detail.releases.append(r);
                    }
                done();
            });
    }
}
void CatalogueClient::loadChangelog(const Release &release, std::function<void(QString, QString)> callback) {
    if (release.provider != "curseforge") {
        callback(release.changelog, {});
        return;
    }
    if (!hasCurseforgeKey()) {
        callback({}, "CurseForge changelogs need an approved API key.");
        return;
    }
    if (release.projectId.isEmpty() || release.id.isEmpty()) {
        callback({}, "This version has no source identity.");
        return;
    }
    request(QUrl("https://api.curseforge.com/v1/mods/" + release.projectId + "/files/" + release.id +
                 "/changelog"),
            true, [callback = std::move(callback)](const QJsonDocument &doc, QString error) {
                callback(doc.object().value("data").toString(), error);
            });
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
            request(QUrl(cf ? "https://api.curseforge.com/v1/mods/" + id
                            : "https://api.modrinth.com/v2/project/" + id),
                    cf,
                    [this, generation, batch, done, cf, provider](const QJsonDocument &doc,
                                                                  const QString &error) {
                        if (!error.isEmpty())
                            batch->errors << provider + ": " + error;
                        else {
                            const auto row = cf ? doc.object().value("data").toObject() : doc.object();
                            Project p;
                            p.provider = provider;
                            p.id = cf ? QString::number(row.value("id").toInteger())
                                      : row.value("id").toString();
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
                            if (!cf && !p.id.isEmpty() && p.id != "0")
                                emit descriptionReady(generation, row.value("body").toString());
                        }
                        done();
                    });
        }
    };
    if (sha1.isEmpty()) {
        fetch(ids, {});
        return;
    }
    request(
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
