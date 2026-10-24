#pragma once
#include "domain/CatalogueSearch.h"
#include <QObject>
#include <memory>
namespace studio {
class InstallationPreferences final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool promptOptionalDependencies READ promptOptionalDependencies WRITE
                   setPromptOptionalDependencies NOTIFY changed)
  public:
    explicit InstallationPreferences(std::shared_ptr<IBrowseSettingsStore>, QObject *parent = nullptr);
    bool promptOptionalDependencies() const {
        return m_optional;
    }
    void setPromptOptionalDependencies(bool);
  signals:
    void changed();

  private:
    std::shared_ptr<IBrowseSettingsStore> m_store;
    bool m_optional = true;
};
} // namespace studio
