#pragma once
#include "domain/CatalogueSearch.h"
#include <QObject>
#include <memory>
namespace studio {
class BrowsePreferences final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connectorFabric READ connectorFabric WRITE setConnectorFabric NOTIFY changed)
  public:
    explicit BrowsePreferences(std::shared_ptr<IBrowseSettingsStore> store, QObject *parent = nullptr);
    bool connectorFabric() const {
        return m_bridge;
    }
    void setConnectorFabric(bool);
  signals:
    void changed();

  private:
    std::shared_ptr<IBrowseSettingsStore> m_store;
    bool m_bridge = true;
};
} // namespace studio
