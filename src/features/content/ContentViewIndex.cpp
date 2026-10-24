#include "ContentViewIndex.h"
#include "domain/TextSearch.h"
#include <QDateTime>
#include <QSet>
#include <algorithm>
#include <numeric>
namespace studio {
void ContentViewIndex::remember(const QVariantList &rows, const QString &instance, quint64 visibilityRevision,
                                const std::function<bool(const QVariantMap &)> &hidden) {
    if (m_ready && m_snapshot.isSharedWith(rows) && m_instance == instance &&
        m_visibilityRevision == visibilityRevision)
        return;
    m_ready = true;
    m_snapshot = rows;
    m_instance = instance;
    m_visibilityRevision = visibilityRevision;
    m_entries.clear();
    m_entries.reserve(rows.size());
    m_orders.clear();
    m_authors.clear();
    QSet<QString> authors;
    for (const auto &value : rows) {
        auto row = value.toMap();
        Entry entry;
        entry.category = row.value("category").toString();
        entry.title = row.value("title").toString();
        entry.author = row.value("author").toString();
        entry.filename = row.value("fileName").toString();
        entry.environment = row.value("environment", "unknown").toString();
        entry.hidden = hidden && hidden(row);
        if (!row.contains("hidden") || row.value("hidden").toBool() != entry.hidden)
            row["hidden"] = entry.hidden;
        entry.value = row;
        entry.enabled = row.value("enabled").toBool();
        entry.updates = row.value("updateAvailable").toBool();
        entry.locked = row.value("locked").toBool();
        entry.openSource = row.value("openSource").toBool();
        entry.external = row.value("external").toBool();
        entry.updatedAt = row.value("updatedAt").toLongLong();
        const auto text =
            entry.title + ' ' + entry.filename + ' ' + entry.author + ' ' + row.value("version").toString();
        entry.text = normalizedSearch(text);
        QString word;
        for (const auto ch : text) {
            if (ch.isLetterOrNumber())
                word += ch;
            else if (!word.isEmpty()) {
                entry.words << normalizedSearch(word);
                word.clear();
            }
        }
        if (!word.isEmpty())
            entry.words << normalizedSearch(word);
        if (!entry.author.isEmpty() && !authors.contains(entry.author)) {
            authors.insert(entry.author);
            m_authors << entry.author;
        }
        m_entries << std::move(entry);
    }
    m_authors.sort(Qt::CaseInsensitive);
}
const QList<int> &ContentViewIndex::order(const QString &sort) const {
    auto found = m_orders.constFind(sort);
    if (found != m_orders.cend())
        return *found;
    QList<int> indices(m_entries.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::stable_sort(indices.begin(), indices.end(), [this, &sort](int a, int b) {
        const auto &left = m_entries[a], &right = m_entries[b];
        if (sort == "updates" && left.updates != right.updates)
            return left.updates;
        const auto &l = sort == "author" ? left.author : sort == "filename" ? left.filename : left.title;
        const auto &r = sort == "author" ? right.author : sort == "filename" ? right.filename : right.title;
        const auto compared = QString::compare(l, r, Qt::CaseInsensitive);
        return sort == "name-desc" ? compared > 0 : compared < 0;
    });
    return *m_orders.insert(sort, std::move(indices));
}
QVariantList ContentViewIndex::filter(const QString &query, const QString &category, const QString &sort,
                                      const QVariantMap &filters) const {
    const auto author = filters.value("author").toString(),
               environment = filters.value("environment").toString(),
               state = filters.value("state").toString();
    const bool hidden = filters.value("hidden").toBool(), updates = filters.value("updates").toBool(),
               locked = filters.value("locked").toBool(), recent = filters.value("recent").toBool(),
               openSource = filters.value("openSource").toBool(),
               external = filters.value("external").toBool();
    const auto cutoff = QDateTime::currentMSecsSinceEpoch() - 7LL * 24 * 60 * 60 * 1000;
    const TextQuery search(query);
    QVariantList result;
    result.reserve(m_entries.size());
    for (const auto index : order(sort)) {
        const auto &entry = m_entries[index];
        if (entry.hidden != hidden || (category != "all" && entry.category != category) ||
            (!author.isEmpty() && entry.author != author) ||
            (!environment.isEmpty() && entry.environment != environment) ||
            (state == "enabled" && !entry.enabled) || (state == "disabled" && entry.enabled) ||
            (updates && !entry.updates) || (locked && !entry.locked) ||
            (recent && entry.updatedAt < cutoff) || (openSource && !entry.openSource) ||
            (external && !entry.external) || !search.matchesPrepared(entry.text, entry.words))
            continue;
        result << entry.value;
    }
    return result;
}
} // namespace studio
