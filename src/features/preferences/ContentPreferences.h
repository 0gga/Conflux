#pragma once
#include "features/content/ContentPorts.h"
#include <QCache>
#include <QSettings>
#include <memory>
namespace studio {
class SettingsContentPreferences final : public IContentPreferences {
  public:
    explicit SettingsContentPreferences(const QString &settingsFile = {});
    bool frozen(const QString &, const QString &) const override;
    bool hasFrozenPreference(const QString &, const QString &) const override;
    void setFrozen(const QString &, const QString &, bool) override;
    bool locked(const QString &, const QString &) const override;
    void setLocked(const QString &, const QString &, bool) override;
    qint64 updatedAt(const QString &, const QString &) const override;
    void setUpdatedAt(const QString &, const QString &, qint64) override;

  private:
    enum class Field { Frozen, Locked, Updated };
    struct Keys {
        QString frozen, locked, updated;
    };
    QString key(const QString &, const QString &, Field) const;
    // Immutable derived keys only; QSettings remains authoritative for every value read.
    mutable QCache<QPair<QString, QString>, Keys> m_keys{1024 * 1024};
    std::unique_ptr<QSettings> m_settings;
};
} // namespace studio
