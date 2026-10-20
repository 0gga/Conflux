#include "BrowsePreferences.h"
namespace studio {
BrowsePreferences::BrowsePreferences(std::shared_ptr<IBrowseSettingsStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)),
      m_bridge(m_store->value("browse/connectorFabric", true).toBool()) {}
void BrowsePreferences::setConnectorFabric(bool enabled) {
    if (enabled == m_bridge)
        return;
    m_bridge = enabled;
    m_store->save("browse/connectorFabric", enabled);
    emit changed();
}
} // namespace studio
