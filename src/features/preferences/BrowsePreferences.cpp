#include "BrowsePreferences.h"
#include <optional>
namespace studio {
namespace {
std::optional<QVariant> normalizeDefault(const QString &key, const QVariant &value) {
    QVariant normalized = value;
    if (key == "size") {
        bool valid = false;
        const double number = value.toDouble(&valid);
        if (!valid || !QList<double>{10, 20, 50, 100}.contains(number))
            return std::nullopt;
        normalized = int(number);
    } else if (key == "sort") {
        if (!QStringList{"relevance", "downloads", "follows", "newest", "updated"}.contains(value.toString()))
            return std::nullopt;
    } else if (key == "source") {
        if (!QStringList{"merged", "modrinth", "curseforge"}.contains(value.toString()))
            return std::nullopt;
    } else if (key == "hideInstalled") {
        if (value.metaType().id() != QMetaType::Bool)
            return std::nullopt;
    } else {
        return std::nullopt;
    }
    return normalized;
}
} // namespace
BrowsePreferences::BrowsePreferences(std::shared_ptr<IBrowseSettingsStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)),
      m_bridge(m_store->value("browse/connectorFabric", true).toBool()) {
    m_defaults = {{"sort", "relevance"}, {"size", 20}, {"source", "merged"}, {"hideInstalled", false}};
    const auto saved = m_store->value("browse/defaults").toMap();
    // Reuse the same validation for persisted and interactive settings.
    for (auto it = saved.cbegin(); it != saved.cend(); ++it)
        if (const auto value = normalizeDefault(it.key(), it.value()))
            m_defaults[it.key()] = *value;
}
void BrowsePreferences::setConnectorFabric(bool enabled) {
    if (enabled == m_bridge)
        return;
    m_bridge = enabled;
    m_store->save("browse/connectorFabric", enabled);
    emit changed();
}
bool BrowsePreferences::setDefault(const QString &key, const QVariant &value) {
    const auto valid = normalizeDefault(key, value);
    if (!valid)
        return false;
    const auto normalized = *valid;
    if (m_defaults.value(key) == normalized)
        return true;
    m_defaults[key] = normalized;
    m_store->save("browse/defaults", m_defaults);
    emit defaultsChanged();
    return true;
}
void BrowsePreferences::resetDefaults() {
    const QVariantMap initial{
        {"sort", "relevance"}, {"size", 20}, {"source", "merged"}, {"hideInstalled", false}};
    if (m_defaults == initial)
        return;
    m_defaults = initial;
    m_store->save("browse/defaults", m_defaults);
    emit defaultsChanged();
}
} // namespace studio
