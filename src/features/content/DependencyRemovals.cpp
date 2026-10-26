#include "DependencyRemovals.h"
#include "ContentDependencyGraph.h"
#include "ContentOperationState.h"
#include <QPointer>
#include <QTimer>
#include <algorithm>
namespace studio {
DependencyRemovals::DependencyRemovals(QObject *parent) : QObject(parent), m_model(this) {}
void DependencyRemovals::publish() {
    const QPointer<DependencyRemovals> guard(this);
    rebuild();
    if (guard)
        emit changed();
}
void DependencyRemovals::accept(Offer offer) {
    if (m_pack.value("path") != offer.pack.value("path"))
        ++m_generation;
    m_pack = std::move(offer.pack);
    m_files = std::move(offer.files);
    QSet<QString> known(m_keys.cbegin(), m_keys.cend());
    for (const auto &key : offer.keys)
        if (!known.contains(key)) {
            known.insert(key);
            m_keys << key;
        }
    for (const auto &value : m_files) {
        const auto row = value.toMap();
        if (known.contains(contentRowKey(row)))
            m_details[contentRowKey(row)] = row;
    }
}
void DependencyRemovals::observe(const QVariantMap &pack, const QVariantList &before,
                                 const QVariantList &current, const QStringList &targets,
                                 const QString &error, bool cleanup) {
    const auto path = pack.value("path").toString();
    bool changed = visible() && m_pack.value("path").toString() == path;
    if (changed) {
        m_pack = pack;
        m_files = current;
        for (const auto &key : targets) {
            if (m_queued.remove(key) && !error.isEmpty())
                m_errors[key] = error;
        }
        const QSet<QString> offered(m_keys.cbegin(), m_keys.cend());
        for (const auto &value : current) {
            const auto row = value.toMap();
            if (offered.contains(contentRowKey(row)))
                m_details[contentRowKey(row)] = row;
        }
    }
    for (auto &offer : m_offers)
        if (offer.pack.value("path").toString() == path) {
            offer.pack = pack;
            offer.files = current;
        }
    if (!cleanup && error.isEmpty() && !path.isEmpty()) {
        auto keys = ContentDependencyGraph(before).descendants(targets);
        QSet<QString> present;
        for (const auto &value : current)
            present.insert(contentRowKey(value.toMap()));
        keys.erase(
            std::remove_if(keys.begin(), keys.end(), [&](const auto &key) { return !present.contains(key); }),
            keys.end());
        if (!keys.isEmpty()) {
            if (!visible() || m_pack.value("path").toString() == path) {
                accept({pack, current, keys});
                changed = true;
            } else {
                auto waiting = std::find_if(m_offers.begin(), m_offers.end(), [&](const auto &offer) {
                    return offer.pack.value("path").toString() == path;
                });
                if (waiting == m_offers.end())
                    m_offers << Offer{pack, current, keys};
                else {
                    QSet<QString> known(waiting->keys.cbegin(), waiting->keys.cend());
                    for (const auto &key : keys)
                        if (!known.contains(key)) {
                            known.insert(key);
                            waiting->keys << key;
                        }
                }
            }
        }
    }
    // Publish only the final snapshot. A subscriber can dismiss, replace or
    // destroy this offer; no old observation may continue afterward.
    if (changed)
        publish();
}
void DependencyRemovals::rebuild() {
    const ContentDependencyGraph graph(m_files);
    QHash<QString, int> positions;
    for (int i = 0; i < m_files.size(); ++i)
        positions[contentRowKey(m_files[i].toMap())] = i;
    m_rows.clear();
    m_individual.clear();
    m_unused = graph.unused(m_keys);
    m_unused.erase(std::remove_if(m_unused.begin(), m_unused.end(),
                                  [this](const auto &key) { return m_queued.contains(key); }),
                   m_unused.end());
    for (const auto &key : m_keys) {
        auto row = m_details.value(key);
        const bool present = positions.contains(key);
        if (present) {
            row = m_files[positions.value(key)].toMap();
            const auto relations = graph.relations(positions.value(key));
            for (auto it = relations.cbegin(); it != relations.cend(); ++it)
                row[it.key()] = it.value();
        }
        const bool ready = present && graph.individuallyUnused(key);
        if (ready)
            m_individual.insert(key);
        row["key"] = key;
        row["contentKey"] = key;
        row["state"] = m_queued.contains(key)                    ? "queued"
                       : !present                                ? "removed"
                       : ready && !m_errors.value(key).isEmpty() ? "error"
                       : ready                                   ? "ready"
                                                                 : "kept";
        row["error"] = m_errors.value(key);
        row["reason"] = row.value("locked").toBool()    ? QString("Locked")
                        : row.value("pending").toBool() ? QString("In use")
                                                        : row.value("dependencyHint").toString();
        m_rows << row;
    }
    m_model.setRows(m_rows);
}
void DependencyRemovals::apply(const QStringList &keys) {
    QVariantList rows;
    const QSet<QString> wanted(keys.cbegin(), keys.cend());
    for (const auto &value : m_files) {
        const auto row = value.toMap();
        const auto key = contentRowKey(row);
        if (wanted.contains(key) && !m_queued.contains(key))
            rows << row;
    }
    if (rows.isEmpty() || !m_remove)
        return;
    for (const auto &value : rows) {
        const auto key = contentRowKey(value.toMap());
        m_queued.insert(key);
        m_errors.remove(key);
    }
    const QPointer<DependencyRemovals> guard(this);
    const auto destination = m_pack;
    const auto remove = m_remove;
    const auto generation = m_generation;
    publish();
    if (!guard)
        return;
    bool accepted = false;
    try {
        accepted = remove(destination, rows);
    } catch (...) {
    }
    if (!guard || accepted || m_generation != generation)
        return;
    for (const auto &value : rows) {
        const auto key = contentRowKey(value.toMap());
        m_queued.remove(key);
        m_errors[key] = "Could not queue removal.";
    }
    publish();
}
void DependencyRemovals::remove(const QString &key) {
    if (m_individual.contains(key))
        apply({key});
}
void DependencyRemovals::removeUnused() {
    apply(m_unused);
}
void DependencyRemovals::dismiss() {
    if (!visible())
        return;
    ++m_generation;
    m_keys.clear();
    m_files.clear();
    m_details.clear();
    m_errors.clear();
    m_queued.clear();
    m_pack.clear();
    const QPointer<DependencyRemovals> guard(this);
    publish();
    if (guard)
        QTimer::singleShot(0, this, [this] {
            if (!visible() && !m_offers.isEmpty()) {
                accept(m_offers.takeFirst());
                publish();
            }
        });
}
} // namespace studio
