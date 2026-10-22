#include "ReleaseFilters.h"
#include <QSignalBlocker>
namespace studio {
ReleaseFilters::ReleaseFilters(QObject *parent) : QObject(parent) {
    clear();
    m_options = calculateOptions();
}
void ReleaseFilters::clear() {
    m_filters = {{"channels", QStringList{}},
                 {"versions", QStringList{}},
                 {"loaders", QStringList{}},
                 {"source", "all"}};
    m_rows = calculateRows();
    emit changed();
}
void ReleaseFilters::setContext(const QString &identity, const QVariantList &groups,
                                const QStringList &versions, const QStringList &loaders) {
    const bool reset = identity != m_identity;
    const bool seed = !m_seeded && !versions.isEmpty();
    if (!reset && !seed && groups == m_groups)
        return;
    if (reset) {
        m_identity = identity;
        m_seeded = false;
        const QSignalBlocker blocker(this);
        clear();
    }
    if (!m_seeded && !versions.isEmpty()) {
        m_filters["versions"] = versions;
        m_filters["loaders"] = loaders;
        m_seeded = true;
    }
    m_groups = groups;
    m_options = calculateOptions();
    m_rows = calculateRows();
    emit changed();
}
void ReleaseFilters::configure(const QVariantMap &changes) {
    bool dirty = false;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (!m_filters.contains(it.key()))
            continue;
        QVariant value;
        if (it.key() == "source") {
            if (!QStringList{"all", "modrinth", "curseforge"}.contains(it.value().toString()))
                continue;
            value = it.value().toString();
        } else {
            auto values = it.value().toStringList();
            values.removeAll("");
            values.removeDuplicates();
            if (it.key() == "channels") {
                const QStringList allowed{"release", "beta", "alpha"};
                values.removeIf([&allowed](const QString &channel) { return !allowed.contains(channel); });
            }
            value = values;
        }
        if (m_filters.value(it.key()) != value) {
            m_filters[it.key()] = value;
            dirty = true;
        }
    }
    if (dirty) {
        m_rows = calculateRows();
        emit changed();
    }
}
QVariantMap ReleaseFilters::calculateOptions() const {
    QStringList versions, loaders;
    for (const auto &group : m_groups)
        for (const auto &item : group.toMap().value("variants").toList()) {
            const auto release = item.toMap();
            versions += release.value("gameVersions").toStringList();
            loaders += release.value("loaders").toStringList();
        }
    versions.removeDuplicates();
    loaders.removeDuplicates();
    loaders.removeAll("minecraft");
    return {
        {"channels", QStringList{"release", "beta", "alpha"}}, {"versions", versions}, {"loaders", loaders}};
}
QVariantList ReleaseFilters::calculateRows() const {
    QVariantList rows;
    const auto versions = m_filters.value("versions").toStringList();
    const auto loaders = m_filters.value("loaders").toStringList();
    const auto channels = m_filters.value("channels").toStringList();
    const auto source = m_filters.value("source").toString();
    const auto matches = [](const QStringList &selected, const QStringList &available) {
        if (selected.isEmpty())
            return true;
        for (const auto &value : selected)
            if (available.contains(value))
                return true;
        return false;
    };
    for (const auto &value : m_groups) {
        auto group = value.toMap();
        QVariantList variants;
        for (const auto &item : group.value("variants").toList()) {
            const auto release = item.toMap();
            auto sources = release.value("sources").toStringList();
            if (sources.isEmpty())
                sources << release.value("provider").toString();
            if ((source == "all" || sources.contains(source)) &&
                (channels.isEmpty() || channels.contains(release.value("channel", "release").toString())) &&
                matches(versions, release.value("gameVersions").toStringList()) &&
                matches(loaders, release.value("loaders").toStringList()))
                variants << item;
        }
        if (!variants.isEmpty()) {
            group["variants"] = variants;
            rows << group;
        }
    }
    return rows;
}
} // namespace studio
