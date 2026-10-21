#include "TolerantCatalogueSearch.h"
#include "domain/TextSearch.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
namespace studio {
void TolerantCatalogueSearch::fetch(const CatalogueQuery &query, int offset, int limit, Callback callback) {
    QVariantMap authors;
    for (auto it = query.authorIds.cbegin(); it != query.authorIds.cend(); ++it)
        authors[it.key()] = it.value();
    const auto key = QJsonDocument(QJsonObject::fromVariantMap({{"text", query.text},
                                                                {"provider", query.provider},
                                                                {"type", query.type},
                                                                {"sort", query.sort},
                                                                {"versions", query.versions},
                                                                {"loaders", query.loaders},
                                                                {"tags", query.tags},
                                                                {"projects", query.projectIds},
                                                                {"authors", authors}}))
                         .toJson(QJsonDocument::Compact);
    const auto deliver = [offset, limit, callback](CataloguePage page) {
        page.projects = page.projects.mid(offset, limit);
        callback(page);
    };
    if (m_cache.contains(key) && m_cache.value(key).expiry.hasExpired())
        m_cache.remove(key);
    if (m_cache.contains(key)) {
        deliver(m_cache.value(key).page);
        return;
    }
    QPointer<TolerantCatalogueSearch> guard(this);
    const auto generation = m_generation;
    m_source->fetch(
        query, offset, limit, [guard, query, key, callback, deliver, generation](CataloguePage page) {
            const auto normalized = normalizedSearch(query.text);
            if (!guard || !page.error.isEmpty() || page.total > 0 || !page.projects.isEmpty() ||
                normalized.size() < 5) {
                callback(page);
                return;
            }
            auto prefix = query;
            prefix.text = normalized.left(3);
            guard->m_source->fetch(
                prefix, 0, 100, [guard, query, key, callback, deliver, generation](CataloguePage candidates) {
                    if (!guard)
                        return;
                    if (!candidates.error.isEmpty()) {
                        callback({{}, 0, candidates.error});
                        return;
                    }
                    TextQuery matcher(query.text);
                    QVector<Project> ranked[4];
                    CataloguePage result;
                    for (const auto &project : candidates.projects) {
                        const auto rank = matcher.rank(project.title);
                        if (rank < 0)
                            continue;
                        if (query.sort == "relevance")
                            ranked[rank] << project;
                        else
                            result.projects << project;
                    }
                    if (query.sort == "relevance")
                        for (const auto &group : ranked)
                            result.projects += group;
                    result.total = result.projects.size();
                    if (guard->m_generation == generation) {
                        if (guard->m_cache.size() >= 16)
                            guard->m_cache.erase(guard->m_cache.begin());
                        guard->m_cache.insert(key, CachedPage{result});
                    }
                    deliver(result);
                });
        });
}
} // namespace studio
