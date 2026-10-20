#pragma once
#include "domain/CatalogueSearch.h"
#include <QSettings>
#include <memory>
namespace studio {
class BrowseSettingsStore final : public IBrowseSettingsStore {
  public:
    explicit BrowseSettingsStore(const QString &file = {});
    QVariant value(const QString &, const QVariant &fallback = {}) const override;
    void save(const QString &, const QVariant &) override;

  private:
    std::unique_ptr<QSettings> m_settings;
};
} // namespace studio
