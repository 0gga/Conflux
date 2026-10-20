#pragma once
#include "domain/CatalogueSearch.h"
#include <memory>
#include <optional>
namespace studio {
struct BrowseCompatibility {
    QStringList versions, loaders;
    bool versionUnlocked = false, defaultLoaders = true;
};
class IBrowseFilterPreferences {
  public:
    virtual ~IBrowseFilterPreferences() = default;
    virtual std::optional<BrowseCompatibility> load(const QString &instance, const QString &type) const = 0;
    virtual void save(const QString &instance, const QString &type, const BrowseCompatibility &) = 0;
};
// Persists compatibility choices only; result pages and provider cursors stay in BrowseCatalogue.
class BrowseFilterPreferences final : public IBrowseFilterPreferences {
  public:
    explicit BrowseFilterPreferences(std::shared_ptr<IBrowseSettingsStore> store);
    std::optional<BrowseCompatibility> load(const QString &, const QString &) const override;
    void save(const QString &, const QString &, const BrowseCompatibility &) override;

  private:
    std::shared_ptr<IBrowseSettingsStore> m_store;
    static QString key(const QString &, const QString &);
};
} // namespace studio
