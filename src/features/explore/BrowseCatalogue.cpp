#include "BrowseCatalogue.h"
#include "domain/ContentTypes.h"
#include <QDateTime>
#include <QPointer>
#include <QTimer>
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
    m_scroll = qMax(qreal(0), value);
    emit scrollChanged();
}
void BrowseCatalogue::setKey(const QByteArray &key) {
    m_source->setKey(key);
    const bool enabled = !key.isEmpty();
    if (enabled != m_cf) {
        m_cf = enabled;
        m_optionsLoaded = false;
        loadOptions();
        reset();
    }
}
void BrowseCatalogue::loadOptions() {
    if (m_optionsLoaded)
        return;
    m_optionsLoaded = true;
    QPointer<BrowseCatalogue> guard(this);
    m_source->facets([guard](QVariantMap options, QString error) {
        if (!guard)
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
    m_contexts[viewKey()] = {m_filters, m_pages,    m_streams, m_seen,   m_installed,
                             m_known,   m_building, m_page,    m_scroll, m_defaultLoaderSelection};
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
void BrowseCatalogue::activateView(bool sameInstance) {
    ++m_generation;
    m_busy = false;
    m_notice.clear();
    if (m_contexts.contains(viewKey())) {
        const auto state = m_contexts.value(viewKey());
        m_filters = state.filters;
        m_pages = state.pages;
        m_streams = state.streams;
        m_seen = state.seen;
        const bool installedChanged = sameInstance && m_installed != state.installed;
        if (!sameInstance)
            m_installed = state.installed;
        m_known = state.known;
        m_building = state.building;
        m_page = state.page;
        m_scroll = state.scroll;
        m_defaultLoaderSelection = state.defaultLoaders;
        m_reads = 0;
        if (applyDefaults() || (installedChanged && m_filters.value("hideInstalled").toBool())) {
            reset();
            return;
        }
        m_busy = m_page >= m_pages.size();
        emit changed();
        emit scrollChanged();
        if (m_busy)
            pump();
        return;
    }
    for (const auto &key : {"sort", "size", "source", "hideInstalled"})
        if (m_defaults.contains(key))
            m_filters[key] = m_defaults.value(key);
    m_filters["versions"] = m_defaultVersions;
    m_filters["loaders"] = m_defaultLoaders;
    m_filters["unlocked"] = false;
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
    reset();
}
void BrowseCatalogue::setContext(const QString &key, const QStringList &versions,
                                 const QStringList &loaders) {
    m_defaultVersions = versions;
    m_defaultLoaders = loaders;
    if (m_context == key) {
        if (applyDefaults())
            reset();
        return;
    }
    saveSnapshot();
    m_context = key;
    m_installed.clear();
    activateView();
}
void BrowseCatalogue::setInstalled(const QVariantList &inventory) {
    QSet<QString> ids;
    for (const auto &value : inventory) {
        const auto row = value.toMap();
        const auto sourceIds = row.value("sourceIds").toMap();
        for (auto it = sourceIds.cbegin(); it != sourceIds.cend(); ++it)
            ids.insert(it.key() + ':' + it.value().toString() + '|' + row.value("category").toString());
    }
    if (ids == m_installed)
        return;
    m_installed = std::move(ids);
    if (m_filters.value("hideInstalled").toBool())
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
    q.loaders = m_filters.value("loaders").toStringList();
    q.tags = m_filters.value("tags").toStringList();
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
        QStringList{"mod", "resourcepack", "shader", "datapack"}.contains(type)) {
        saveSnapshot();
        m_filters["type"] = type;
        activateView(true);
    }
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
        if (it.key() == "size" && !QList<int>{10, 20, 50, 100}.contains(value.toInt()))
            continue;
        if (it.key() == "sort" &&
            !QStringList{"relevance", "downloads", "follows", "newest", "updated"}.contains(value.toString()))
            continue;
        if (it.key() == "source" &&
            !QStringList{"merged", "modrinth", "curseforge"}.contains(value.toString()))
            continue;
        if (it.key() == "type" &&
            !QStringList{"mod", "resourcepack", "shader", "datapack"}.contains(value.toString()))
            continue;
        if (m_filters.value(it.key()) != value) {
            m_filters[it.key()] = value;
            changed = true;
        }
    }
    if (changes.contains("versions") || changes.contains("loaders") || changes.contains("unlocked"))
        saveCompatibility();
    if (changed)
        reset();
}
void BrowseCatalogue::refresh() {
    if (!m_optionsLoaded)
        loadOptions();
    reset();
}
void BrowseCatalogue::reset() {
    ++m_generation;
    m_streams.clear();
    m_pages.clear();
    m_building.clear();
    m_seen.clear();
    m_known.clear();
    m_page = 0;
    m_scroll = 0;
    m_reads = 0;
    m_busy = true;
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
    if (m_filters.value("hideInstalled").toBool())
        for (auto it = p.sourceIds.cbegin(); it != p.sourceIds.cend(); ++it)
            if (m_installed.contains(it.key() + ':' + it.value() + '|' +
                                     contentCategory(m_filters.value("type").toString())) ||
                (m_filters.value("type") == "datapack" &&
                 m_installed.contains(it.key() + ':' + it.value() + "|mods")))
                return false;
    return true;
}
bool BrowseCatalogue::before(const Project &a, const Project &b) const {
    const auto sort = m_filters.value("sort").toString();
    if (sort == "downloads" && a.downloads != b.downloads)
        return a.downloads > b.downloads;
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
void BrowseCatalogue::pump() {
    if (!m_busy)
        return;
    for (auto it = m_streams.begin(); it != m_streams.end(); ++it)
        if (it->rows.isEmpty() && !it->exhausted) {
            if (++m_reads > 24) {
                m_notice = "Many results are hidden. This page is partial; continue to see more.";
                break;
            }
            const auto provider = it.key();
            const auto generation = m_generation;
            const int offset = it->offset;
            QPointer<BrowseCatalogue> guard(this);
            m_source->fetch(request(provider), offset, 50,
                            [guard, generation, provider](CataloguePage result) {
                                if (!guard || guard->m_generation != generation)
                                    return;
                                auto &stream = guard->m_streams[provider];
                                stream.offset += result.projects.size();
                                stream.rows = std::move(result.projects);
                                stream.exhausted = stream.rows.isEmpty() || stream.offset >= result.total ||
                                                   !result.error.isEmpty();
                                if (!result.error.isEmpty())
                                    guard->m_notice = result.error;
                                QTimer::singleShot(0, guard, [guard] {
                                    if (guard)
                                        guard->pump();
                                });
                            });
            return;
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
            m_source->fetch(query, 0, 1, [guard, generation, p](CataloguePage result) {
                if (!guard || guard->m_generation != generation)
                    return;
                auto joined = p;
                if (!result.projects.isEmpty())
                    joined = mergeProjects(QVector<Project>{p, result.projects.first()}, guard->m_identities)
                                 .first();
                else if (!result.error.isEmpty())
                    guard->m_notice = "Modrinth metadata: " + result.error;
                guard->publish(joined);
                QTimer::singleShot(0, guard, [guard] {
                    if (guard)
                        guard->pump();
                });
            });
            return;
        }
        publish(p);
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
    else
        m_pages << m_building;
    m_building.clear();
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
void BrowseCatalogue::next() {
    if (m_busy || !hasNext())
        return;
    ++m_page;
    m_scroll = 0;
    emit scrollChanged();
    if (m_page < m_pages.size()) {
        emit changed();
        return;
    }
    m_busy = true;
    m_reads = 0;
    pump();
}
void BrowseCatalogue::previous() {
    if (m_busy || m_page == 0)
        return;
    --m_page;
    m_scroll = 0;
    emit changed();
    emit scrollChanged();
}
void BrowseCatalogue::hide(const QVariantMap &project, bool hidden) {
    m_visibility->setHidden(project, hidden);
}
} // namespace studio
