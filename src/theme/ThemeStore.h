#pragma once
#include <QSettings>
#include <memory>
namespace studio {
inline const QString builtInOled = "Built-in OLED", builtInGrey = "Built-in dark grey";
struct ThemeRead {
    QByteArray bytes;
    QString error;
};
struct ThemeWrite {
    QString file, error;
};
class IThemeStore {
  public:
    virtual ~IThemeStore() = default;
    virtual ThemeRead read(const QString &) const = 0;
    virtual ThemeWrite save(const QString &name, const QByteArray &) = 0;
    virtual QStringList available() const = 0;
    virtual QString folder() const = 0;
    virtual QString selected() const = 0;
    virtual void select(const QString &) = 0;
};
class NativeThemeStore final : public IThemeStore {
  public:
    explicit NativeThemeStore(QString storageOverride = {});
    ThemeRead read(const QString &) const override;
    ThemeWrite save(const QString &, const QByteArray &) override;
    QStringList available() const override;
    QString folder() const override;
    QString selected() const override;
    void select(const QString &) override;

  private:
    QString m_root;
    std::unique_ptr<QSettings> m_settings;
};
} // namespace studio
