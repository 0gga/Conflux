#pragma once
#include "ModMetadataReader.h"
#include <QVariantList>
namespace studio {
struct ContentSnapshot {
    QVariantMap pack;
    QVariantList files;
    int enabledMods = 0, identifiedMods = 0;
    bool limited = false, unavailable = false;
};
class IContentInventory {
  public:
    virtual ~IContentInventory() = default;
    virtual ContentSnapshot scan(const QVariantMap &pack) const = 0;
};
class ContentInventory final : public IContentInventory {
  public:
    ContentInventory(std::shared_ptr<const IModMetadataReader> mods,
                     std::shared_ptr<const IArchiveReader> archives)
        : m_mods(std::move(mods)), m_archives(std::move(archives)) {}
    ContentSnapshot scan(const QVariantMap &) const override;
    ContentSnapshot scanContent(const QVariantMap &pack) const {
        return collect(pack, false);
    }

  private:
    ContentSnapshot collect(const QVariantMap &, bool includeConfigs) const;
    std::shared_ptr<const IModMetadataReader> m_mods;
    std::shared_ptr<const IArchiveReader> m_archives;
};
} // namespace studio
