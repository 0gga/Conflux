#pragma once
#include "features/config/ConfigLibrary.h"
#include <QSettings>
namespace studio {
class SettingsConfigOwnershipStore final : public IConfigOwnershipStore {
  public:
    explicit SettingsConfigOwnershipStore(const QString &settingsFile = {});
    QString owner(const QString &pack, const QString &path) const override;
    void setOwner(const QString &pack, const QString &path, const QString &modId) override;

  private:
    std::unique_ptr<QSettings> m_settings;
    static QString key(const QString &, const QString &);
};
} // namespace studio
