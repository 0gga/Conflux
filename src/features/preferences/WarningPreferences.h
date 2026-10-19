#pragma once
#include <QObject>
#include <QSettings>
#include <QVariantList>
#include <memory>
namespace studio {
class IWarningPreferenceStore {
  public:
    virtual ~IWarningPreferenceStore() = default;
    virtual bool enabled(const QString &category) const = 0;
    virtual void setEnabled(const QString &category, bool enabled) = 0;
};
class SettingsWarningPreferenceStore final : public IWarningPreferenceStore {
  public:
    explicit SettingsWarningPreferenceStore(QString settingsFile = {});
    bool enabled(const QString &) const override;
    void setEnabled(const QString &, bool) override;

  private:
    std::unique_ptr<QSettings> m_settings;
};
class WarningPreferences final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList options READ options NOTIFY changed)
    Q_PROPERTY(QStringList enabledCategories READ enabledCategories NOTIFY changed)
  public:
    explicit WarningPreferences(std::shared_ptr<IWarningPreferenceStore> store, QObject *parent = nullptr);
    QVariantList options() const;
    QStringList enabledCategories() const;
    Q_INVOKABLE void setEnabled(const QString &, bool);
    Q_INVOKABLE void reset();
  signals:
    void changed();

  private:
    std::shared_ptr<IWarningPreferenceStore> m_store;
};
} // namespace studio
