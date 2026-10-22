#include "BrowseCatalogue.h"
#include "domain/ContentTypes.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>
namespace studio {
BrowseCatalogue::BrowseCatalogue(std::shared_ptr<ICatalogueSearch> source,
                                 std::shared_ptr<IContentVisibility> visibility, QHash<QString, QString> ids,
                                 std::shared_ptr<IBrowseFilterPreferences> preferences, QObject *parent)
    : QObject(parent), m_source(std::move(source)), m_visibility(std::move(visibility)),
      m_preferences(std::move(preferences)), m_identities(std::move(ids)) {
    m_filters = {{"query", ""},
                 {"source", "merged"},
                 {"type", "mod"},
                 {"sort", "relevance"},
                 {"size", 20},
                 {"versions", QStringList{}},
                 {"loaders", QStringList{}},
                 {"tags", QStringList{}},
                 {"hideInstalled", false},
                 {"hidden", false},
                 {"unlocked", false}};
}
void BrowseCatalogue::setDefaults(const QVariantMap &defaults) {
    m_defaults = defaults;
}
void BrowseCatalogue::setScrollPosition(qreal value) {
    if (!std::isfinite(value))
        return;
    value = qMax(qreal(0), value);
    if (m_scroll == value)
        return;
    m_scroll = value;
    emit scrollChanged();
}
void BrowseCatalogue::setKey(const QByteArray &key) {
    if (m_key == key)
        return;
    m_key = key;
    m_source->setKey(key);
    m_cf = !key.isEmpty();
    m_contexts.clear();
    m_options.clear();
    m_optionsLoaded = false;
    ++m_optionsGeneration;
    reset();
    loadOptions();
}
void BrowseCatalogue::loadOptions() {
    if (m_optionsLoaded)
        return;
    m_optionsLoaded = true;
    QPointer<BrowseCatalogue> guard(this);
    const auto generation = m_optionsGeneration;
    m_source->facets([guard, generation](QVariantMap options, QString error) {
        if (!guard || guard->m_optionsGeneration != generation)
            return;
        guard->m_options = options;
        guard->m_optionsLoaded = error.isEmpty();
        if (!error.isEmpty())
            guard->m_notice = error;
        emit guard->changed();
    });
}
QString BrowseCatalogue::viewKey() const {
    return m_context + QChar(0x1f) + m_filters.value("type").toString();
}
void BrowseCatalogue::saveCompatibility() {
    if (m_preferences && !m_context.isEmpty())
        m_preferences->save(m_context, m_filters.value("type").toString(),
                            {m_filters.value("versions").toStringList(),
                             m_filters.value("loaders").toStringList(), m_filters.value("unlocked").toBool(),
                             m_defaultLoaderSelection});
}
void BrowseCatalogue::saveSnapshot() {
    if (m_context.isEmpty())
        return;
    m_contexts[viewKey()] = {m_filters,
                             m_pages,
                             m_streams,
                             m_seen,
                             m_installed,
                             m_known,
                             m_building,
                             m_page,
                             m_scroll,
                             m_defaultLoaderSelection,
                             m_visibility->hiddenIdentities(),
                             !m_busy};
}
bool BrowseCatalogue::applyDefaults() {
    // A requested instance can be selected before its inventory metadata has arrived.
    if (m_context != "global" && m_defaultVersions.isEmpty())
        return false;
    bool changed = false;
    if (!m_filters.value("unlocked").toBool() &&
        m_filters.value("versions").toStringList() != m_defaultVersions) {
        m_filters["versions"] = m_defaultVersions;
        changed = true;
    }
    if (m_defaultLoaderSelection && m_filters.value("loaders").toStringList() != m_defaultLoaders) {
        m_filters["loaders"] = m_defaultLoaders;
        changed = true;
    }
    return changed;
}
void BrowseCatalogue::activateView(const QVariantMap &changes) {
    ++m_generation;
    m_downloadLookups.clear();
    m_awaitingReply = false;
    m_busy = false;
    m_notice.clear();
    if (m_contexts.contains(viewKey())) {
        const auto state = m_contexts.value(viewKey());
        m_filters = state.filters;
        m_pages = state.pages;
        m_streams = state.streams;
        m_seen = state.seen;
        const bool installedChanged = m_installed != state.installed;
        m_known = state.known;
        m_building = state.building;
        m_page = state.page;
        m_targetPage = m_page;
        m_scroll = state.scroll;
        m_defaultLoaderSelection = state.defaultLoaders;
        m_reads = 0;
        const bool defaultsChanged = applyDefaults();
        const bool filtersChanged = applyChanges(changes);
        if (defaultsChanged || filtersChanged || !state.complete ||
            state.hidden != m_visibility->hiddenIdentities() || (installedChanged && hideInstalledActive())) {
            reset();
            return;
        }
        emit changed();
        emit scrollChanged();
        return;
    }
    for (const auto &key : {"sort", "size", "source", "hideInstalled"})
        if (m_defaults.contains(key))
            m_filters[key] = m_defaults.value(key);
    if (!m_author.isEmpty()) {
        m_filters["query"] = "";
        m_filters["source"] = "merged";
        m_filters["hidden"] = false;
        m_filters["hideInstalled"] = false;
    }
    m_filters["versions"] = m_defaultVersions;
    m_filters["loaders"] = m_defaultLoaders;
    m_filters["unlocked"] = !m_author.isEmpty();
    m_filters["tags"] = QStringList{};
    m_defaultLoaderSelection = true;
    if (m_preferences)
        if (const auto saved = m_preferences->load(m_context, m_filters.value("type").toString())) {
            m_filters["versions"] = saved->versions;
            m_filters["loaders"] = saved->loaders;
            m_filters["unlocked"] = saved->versionUnlocked;
            m_defaultLoaderSelection = saved->defaultLoaders;
            applyDefaults();
        }
    applyChanges(changes);
    reset();
}
void BrowseCatalogue::setContext(const QString &baseKey, const QStringList &versions,
                                 const QStringList &loaders, const QVariantMap &author) {
    const auto key =
        author.isEmpty()
            ? baseKey
            : baseKey + "|author:" +
                  QString::fromUtf8(
                      QJsonDocument::fromVariant(author.value("ids")).toJson(QJsonDocument::Compact));
    m_defaultVersions = author.isEmpty() ? versions : QStringList{};
    m_defaultLoaders = author.isEmpty() ? loaders : QStringList{};
    if (m_context == key) {
        if (applyDefaults())
            reset();
        return;
    }
    saveSnapshot();
    m_contextTypes[m_context] = m_filters.value("type").toString();
    const auto previousAuthor = m_author;
    m_author = author;
    m_filters["type"] =
        m_contextTypes.value(key, !author.isEmpty()           ? "all"
                                  : !previousAuthor.isEmpty() ? "mod"
                                                              : m_filters.value("type").toString());
    m_context = key;
    m_baseContext = baseKey;
    m_installed = m_inventories.value(baseKey);
    activateView();
}
void BrowseCatalogue::setInstalled(const QVariantList &inventory) {
    QSet<QString> ids;
    for (const auto &value : inventory) {
        const auto row = value.toMap();
        const auto sourceIds = row.value("sourceIds").toMap();
        for (auto it = sourceIds.cbegin(); it != sourceIds.cend(); ++it)
            if (!it.key().isEmpty() && !it.value().toString().isEmpty())
                ids.insert(it.key() + ':' + it.value().toString() + '|' + row.value("category").toString());
    }
    if (ids == m_installed)
        return;
    m_installed = std::move(ids);
    m_inventories[m_baseContext] = m_installed;
    if (hideInstalledActive())
        reset();
    else
        emit changed();
}
CatalogueQuery BrowseCatalogue::request(const QString &provider) const {
    CatalogueQuery q;
    q.provider = provider;
    q.text = m_filters.value("query").toString();
    q.type = m_filters.value("type").toString();
    q.sort = m_filters.value("sort").toString();
    q.versions = m_filters.value("versions").toStringList();
    if (q.type == "mod")
        q.loaders = m_filters.value("loaders").toStringList();
    q.tags = m_filters.value("tags").toStringList();
    const auto authors = m_author.value("ids").toMap();
    for (auto it = authors.cbegin(); it != authors.cend(); ++it)
        q.authorIds[it.key()] = it.value().toString();
    if (m_filters.value("hidden").toBool())
        for (const auto &id : m_visibility->hiddenIdentities())
            if (id.startsWith(provider + ':'))
                q.projectIds << id.section(':', 1);
    return q;
}
void BrowseCatalogue::configure(const QVariantMap &requested) {
    auto changes = requested;
    const auto type = changes.take("type").toString();
    if (!type.isEmpty() && type != m_filters.value("type").toString() &&
        QStringList{"all", "mod", "resourcepack", "shader", "datapack"}.contains(type)) {
        saveSnapshot();
        m_filters["type"] = type;
        activateView(changes);
        return;
    }
    if (applyChanges(changes))
        reset();
}
bool BrowseCatalogue::applyChanges(const QVariantMap &requested) {
    auto changes = requested;
    for (const auto &key : {"hideInstalled", "hidden", "unlocked"})
        if (changes.contains(key) && changes.value(key).metaType().id() != QMetaType::Bool)
            changes.remove(key);
    if (changes.contains("loaders"))
        m_defaultLoaderSelection = false;
    if (changes.contains("unlocked") && !changes.value("unlocked").toBool() && m_context != "global") {
        changes["versions"] = m_defaultVersions;
    }
    bool changed = false;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (!m_filters.contains(it.key()))
            continue;
        auto value = it.value();
        if (QStringList{"versions", "loaders", "tags"}.contains(it.key())) {
            auto values = value.toStringList();
            values.removeAll("");
            values.removeDuplicates();
            value = values;
        }
        if (it.key() == "size" &&
            (value.toDouble() != value.toInt() || !QList<int>{10, 20, 50, 100}.contains(value.toInt())))
            continue;
        if (it.key() == "sort" &&
            !QStringList{"relevance", "downloads", "follows", "newest", "updated"}.contains(value.toString()))
            continue;
        if (it.key() == "source" &&
            !QStringList{"merged", "modrinth", "curseforge"}.contains(value.toString()))
            continue;
        if (m_filters.value(it.key()) != value) {
            m_filters[it.key()] = value;
            changed = true;
        }
    }
    if (changes.contains("versions") || changes.contains("loaders") || changes.contains("unlocked"))
        saveCompatibility();
    return changed;
}
void BrowseCatalogue::refresh() {
    reset();
    if (!m_optionsLoaded)
        loadOptions();
}
void BrowseCatalogue::reset() {
    ++m_generation;
    m_streams.clear();
    m_pages.clear();
    m_building.clear();
    m_seen.clear();
    m_known.clear();
    m_page = 0;
    m_targetPage = 0;
    m_downloadLookups.clear();
    m_scroll = 0;
    m_reads = 0;
    m_busy = true;
    m_awaitingReply = false;
    m_notice.clear();
    const auto source = m_filters.value("source").toString();
    if (source != "curseforge")
        m_streams["modrinth"] = {};
    if (source != "modrinth" && m_cf)
        m_streams["curseforge"] = {};
    if (m_filters.value("hidden").toBool())
        for (auto it = m_streams.begin(); it != m_streams.end();) {
            if (request(it.key()).projectIds.isEmpty())
                it = m_streams.erase(it);
            else
                ++it;
        }
    if (m_filters.value("sort") == "follows" && m_streams.contains("curseforge"))
        m_notice = "CurseForge has no follower sort; its results use popularity.";
    emit changed();
    emit scrollChanged();
    pump();
}
QString BrowseCatalogue::identity(const Project &p) const {
    const auto id = p.provider + ':' + p.id;
    return m_identities.value(id, id);
}
bool BrowseCatalogue::accepted(const Project &p) const {
    const auto map = p.toVariant();
    if (m_visibility->hidden(map) != m_filters.value("hidden").toBool())
        return false;
    if (hideInstalledActive())
        for (auto it = p.sourceIds.cbegin(); it != p.sourceIds.cend(); ++it)
            if (m_installed.contains(it.key() + ':' + it.value() + '|' + contentCategory(p.type)) ||
                (m_filters.value("type") == "datapack" &&
                 m_installed.contains(it.key() + ':' + it.value() + "|mods")))
                return false;
    return true;
}
bool BrowseCatalogue::before(const Project &a, const Project &b) const {
    const auto sort = m_filters.value("sort").toString();
    if (sort == "downloads") {
        const auto left = m_known.value(identity(a), a).totalDownloads();
        const auto right = m_known.value(identity(b), b).totalDownloads();
        if (left != right)
            return left > right;
    }
    if (sort == "follows" && a.followers != b.followers)
        return a.followers > b.followers;
    if (sort == "updated" && a.updated != b.updated)
        return QDateTime::fromString(a.updated, Qt::ISODateWithMs) >
               QDateTime::fromString(b.updated, Qt::ISODateWithMs);
    if (sort == "newest" && a.created != b.created)
        return QDateTime::fromString(a.created, Qt::ISODateWithMs) >
               QDateTime::fromString(b.created, Qt::ISODateWithMs);
    // Provider relevance scores cannot be compared. Stable interleaving preserves each provider's rank.
    return m_streams.value(a.provider).offset - m_streams.value(a.provider).rows.size() <=
           m_streams.value(b.provider).offset - m_streams.value(b.provider).rows.size();
}
void BrowseCatalogue::fetchMore(const QString &provider) {
    const auto generation = m_generation;
    const int offset = m_streams.value(provider).offset;
    QPointer<BrowseCatalogue> guard(this);
    m_awaitingReply = true;
    m_source->fetch(
        request(provider), offset, 50, [guard, generation, provider, offset](CataloguePage result) {
            if (!guard || guard->m_generation != generation)
                return;
            guard->m_awaitingReply = false;
            auto &stream = guard->m_streams[provider];
            stream.offset += result.projects.size();
            stream.total = qMax(qint64(0), result.total);
            stream.nextDownloadBound =
                result.projects.isEmpty() ? 0 : qMax(qint64(0), result.projects.last().downloads);
            if (guard->m_filters.value("sort") == "downloads")
                for (const auto &project : result.projects) {
                    const auto key = guard->identity(project);
                    if (guard->m_known.contains(key))
                        guard->m_known[key] =
                            mergeProjects({guard->m_known[key], project}, guard->m_identities).first();
                    else
                        guard->m_known[key] = project;
                }
            stream.rows += result.projects;
            stream.exhausted =
                result.projects.isEmpty() || stream.offset >= result.total || !result.error.isEmpty();
            if (!result.error.isEmpty())
                guard->m_notice = result.error;
            // A provider total may shrink between visits. Rebase an empty offset
            // jump on the current last page instead of showing a phantom page.
            if (result.error.isEmpty() && result.projects.isEmpty() && guard->m_streams.size() == 1 &&
                offset > 0) {
                const int size = qMax(10, guard->m_filters.value("size").toInt());
                guard->m_page = result.total > 0 ? int((result.total - 1) / size) : 0;
                guard->m_targetPage = guard->m_page;
                guard->m_building.clear();
                if (result.total > 0) {
                    stream.offset = guard->m_page * size;
                    stream.exhausted = false;
                } else {
                    guard->m_pages.clear();
                }
            }
            guard->schedulePump();
        });
}
bool BrowseCatalogue::hydrateDownloads() {
    for (const auto &stream : m_streams)
        for (const auto &project : stream.rows) {
            const auto key = identity(project);
            for (auto it = m_identities.cbegin(); it != m_identities.cend(); ++it) {
                const auto provider = it.key().section(':', 0, 0);
                if (it.value() != key || provider == project.provider ||
                    (provider == "curseforge" && !m_cf) || m_downloadLookups.contains(it.key()) ||
                    m_known.value(key).sourceDownloads.contains(provider))
                    continue;
                m_downloadLookups.insert(it.key());
                CatalogueQuery query;
                query.provider = provider;
                query.type = project.type;
                query.projectIds = {it.key().section(':', 1)};
                const auto generation = m_generation;
                QPointer<BrowseCatalogue> guard(this);
                m_awaitingReply = true;
                m_source->fetch(query, 0, 1, [guard, generation, key](CataloguePage result) {
                    if (!guard || guard->m_generation != generation)
                        return;
                    guard->m_awaitingReply = false;
                    if (!result.projects.isEmpty())
                        guard->m_known[key] =
                            mergeProjects({guard->m_known.value(key), result.projects.first()},
                                          guard->m_identities)
                                .first();
                    if (!result.error.isEmpty())
                        guard->m_notice = result.error;
                    guard->schedulePump();
                });
                return true;
            }
        }
    return false;
}
void BrowseCatalogue::schedulePump() {
    const auto generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation == m_generation)
            pump();
    });
}
void BrowseCatalogue::pump() {
    if (!m_busy || m_awaitingReply)
        return;
    for (auto it = m_streams.begin(); it != m_streams.end(); ++it)
        if (it->rows.isEmpty() && !it->exhausted) {
            if (++m_reads > 24) {
                m_notice = "Many results are hidden. This page is partial; continue to see more.";
                break;
            }
            fetchMore(it.key());
            return;
        }
    if (m_filters.value("sort") == "downloads") {
        if (hydrateDownloads())
            return;
        for (auto &stream : m_streams)
            std::stable_sort(stream.rows.begin(), stream.rows.end(),
                             [this](const Project &a, const Project &b) {
                                 return m_known.value(identity(a), a).totalDownloads() >
                                        m_known.value(identity(b), b).totalDownloads();
                             });
        qint64 unseenBound = 0, largestSingleBound = 0;
        QString more;
        qint64 largestBound = -1;
        qint64 best = -1;
        for (auto it = m_streams.cbegin(); it != m_streams.cend(); ++it) {
            if (!it->rows.isEmpty())
                best =
                    qMax(best, m_known.value(identity(it->rows.first()), it->rows.first()).totalDownloads());
            if (!it->exhausted) {
                largestSingleBound = qMax(largestSingleBound, it->nextDownloadBound);
                unseenBound += qMin(it->nextDownloadBound, std::numeric_limits<qint64>::max() - unseenBound);
                if (it->nextDownloadBound > largestBound) {
                    largestBound = it->nextDownloadBound;
                    more = it.key();
                }
            }
        }
        bool unseenPair = false;
        if (m_streams.contains("modrinth") && m_streams.contains("curseforge"))
            for (auto it = m_identities.cbegin(); it != m_identities.cend(); ++it)
                if (!m_known.contains(it.value()) && !m_seen.contains(it.value())) {
                    unseenPair = true;
                    break;
                }
        if (!unseenPair)
            unseenBound = largestSingleBound;
        // A project below both provider page boundaries can still have a larger
        // combined count. Read ahead until no unseen pair can outrank this head.
        if (!more.isEmpty() && best < unseenBound) {
            fetchMore(more);
            return;
        }
    }
    const int size = qBound(10, m_filters.value("size").toInt(), 100);
    while (m_building.size() < size) {
        QString selected;
        for (auto it = m_streams.cbegin(); it != m_streams.cend(); ++it)
            if (!it->rows.isEmpty() &&
                (selected.isEmpty() || before(it->rows.first(), m_streams.value(selected).rows.first())))
                selected = it.key();
        if (selected.isEmpty())
            break;
        auto p = m_streams[selected].rows.takeFirst();
        const auto key = identity(p);
        // Join reviewed identities currently at either stream head before publishing a card.
        QVector<Project> variants{p};
        for (auto it = m_streams.begin(); it != m_streams.end(); ++it)
            for (int i = it->rows.size() - 1; i >= 0; --i)
                if (identity(it->rows[i]) == key)
                    variants << it->rows.takeAt(i);
        p = mergeProjects(variants, m_identities).first();
        if (m_identities.contains(p.provider + ':' + p.id))
            for (auto it = m_identities.cbegin(); it != m_identities.cend(); ++it)
                if (it.value() == key)
                    p.sourceIds[it.key().section(':', 0, 0)] = it.key().section(':', 1);
        if (p.provider == "curseforge" && p.sourceIds.contains("modrinth") && !m_known.contains(key)) {
            CatalogueQuery query;
            query.type = p.type;
            query.projectIds = {p.sourceIds.value("modrinth")};
            const auto generation = m_generation;
            QPointer<BrowseCatalogue> guard(this);
            m_awaitingReply = true;
            m_source->fetch(query, 0, 1, [guard, generation, p](CataloguePage result) {
                if (!guard || guard->m_generation != generation)
                    return;
                guard->m_awaitingReply = false;
                auto joined = p;
                if (!result.projects.isEmpty())
                    joined = mergeProjects(QVector<Project>{p, result.projects.first()}, guard->m_identities)
                                 .first();
                else if (!result.error.isEmpty())
                    guard->m_notice = "Modrinth metadata: " + result.error;
                guard->publish(joined);
                guard->schedulePump();
            });
            return;
        }
        publish(p);
        if (m_filters.value("sort") == "downloads" && m_building.size() < size) {
            schedulePump();
            return;
        }
        bool needsPage = false;
        for (const auto &stream : m_streams)
            if (stream.rows.isEmpty() && !stream.exhausted)
                needsPage = true;
        if (needsPage && m_reads <= 24) {
            pump();
            return;
        }
    }
    if (m_building.isEmpty() && m_page > 0 && !hasNext())
        --m_page;
    else {
        if (m_pages.size() <= m_page)
            m_pages.resize(m_page + 1);
        m_pages[m_page] = m_building;
    }
    m_building.clear();
    if (m_page < m_targetPage && hasNext()) {
        ++m_page;
        m_reads = 0;
        schedulePump();
        return;
    }
    m_targetPage = m_page;
    m_busy = false;
    emit changed();
}
void BrowseCatalogue::publish(Project project) {
    const auto key = identity(project);
    if (m_known.contains(key))
        project = mergeProjects(QVector<Project>{m_known.value(key), project}, m_identities).first();
    m_known[key] = project;
    if (!m_seen.contains(key) && accepted(project)) {
        m_seen.insert(key);
        m_building << project.toVariant();
    }
}
QVariantList BrowseCatalogue::projects() const {
    QVariantList rows;
    if (m_page >= m_pages.size())
        return rows;
    for (const auto &value : m_pages[m_page]) {
        const auto item = value.toMap();
        const auto id = item.value("provider").toString() + ':' + item.value("id").toString();
        const auto key = m_identities.value(id, id);
        rows << (m_known.contains(key) ? m_known.value(key).toVariant() : item);
    }
    return rows;
}
bool BrowseCatalogue::hasNext() const {
    if (m_page + 1 < m_pages.size())
        return true;
    for (const auto &s : m_streams) {
        if (!s.exhausted)
            return true;
        for (const auto &p : s.rows)
            if (!m_seen.contains(identity(p)) && accepted(p))
                return true;
    }
    return false;
}
int BrowseCatalogue::pageCount() const {
    qint64 total = 0;
    for (const auto &stream : m_streams)
        total += qMin(stream.total, qint64(std::numeric_limits<int>::max()) - total);
    const int size = qMax(10, m_filters.value("size").toInt());
    if (!m_busy && !hasNext())
        return qMax(1, m_page + 1);
    return qMax(m_page + 1, int((total + size - 1) / size));
}
void BrowseCatalogue::goToPage(int pageNumber) {
    if (m_busy)
        return;
    const int target = qBound(1, pageNumber, pageCount()) - 1;
    if (target == m_page)
        return;
    ++m_generation;
    m_scroll = 0;
    emit scrollChanged();
    m_targetPage = target;
    if (target < m_pages.size() && !m_pages[target].isEmpty()) {
        m_page = target;
        emit changed();
        return;
    }
    // A single unfiltered provider supports a native offset jump without loading
    // every intervening page. Filtered/merged views preserve their dedup cursor.
    const bool sequential = target == m_page + 1 && target == m_pages.size();
    if (!sequential && m_streams.size() == 1 && !m_filters.value("hidden").toBool() &&
        !m_filters.value("hideInstalled").toBool() &&
        !(m_filters.value("sort") == "downloads" && m_cf && !m_identities.isEmpty())) {
        auto &stream = m_streams.first();
        stream.rows.clear();
        stream.offset = target * qMax(10, m_filters.value("size").toInt());
        stream.exhausted = false;
        m_seen.clear();
        m_page = target;
    } else {
        m_page = m_pages.size();
    }
    m_busy = true;
    m_reads = 0;
    emit changed();
    pump();
}
void BrowseCatalogue::next() {
    if (!m_busy && hasNext())
        goToPage(m_page + 2);
}
void BrowseCatalogue::previous() {
    if (!m_busy && m_page > 0)
        goToPage(m_page);
}
void BrowseCatalogue::hide(const QVariantMap &project, bool hidden) {
    m_visibility->setHidden(project, hidden);
}
} // namespace studio
