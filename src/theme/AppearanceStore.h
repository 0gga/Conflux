#pragma once
#include <QSettings>
#include <QVariantMap>
#include <memory>
namespace studio {
class IAppearanceStore {
  public:
    virtual ~IAppearanceStore() = default;
    virtual QVariantMap load() const = 0;
    virtual void save(const QVariantMap &) = 0;
};
class NativeAppearanceStore final : public IAppearanceStore {
  public:
    explicit NativeAppearanceStore(QString storageOverride = {});
    QVariantMap load() const override;
    void save(const QVariantMap &) override;

  private:
    std::unique_ptr<QSettings> m_settings;
};
} // namespace studio
