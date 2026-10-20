#pragma once
#include "ConfigCodec.h"
#include <QObject>
#include <QVariantList>
#include <memory>
namespace studio {
class IConfigOwnershipStore {
  public:
    virtual ~IConfigOwnershipStore() = default;
    virtual QString owner(const QString &pack, const QString &path) const = 0;
    virtual void setOwner(const QString &pack, const QString &path, const QString &modId) = 0;
};
class ConfigLibrary final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList files READ files NOTIFY changed)
    Q_PROPERTY(QVariantList mods READ mods NOTIFY changed)
  public:
    explicit ConfigLibrary(std::shared_ptr<IConfigOwnershipStore> store, QObject *parent = nullptr,
                           ConfigCodecs codecs = defaultConfigCodecs());
    void setContext(const QVariantMap &pack, const QVariantList &inventory);
    QVariantList files() const;
    QVariantList mods() const;
    QVariantMap file(const QString &path) const;
    Q_INVOKABLE bool identify(const QString &path, const QString &modId);
  signals:
    void changed();

  private:
    std::shared_ptr<IConfigOwnershipStore> m_store;
    QStringList m_extensions;
    QVariantMap m_pack;
    QVariantList m_inventory;
};
} // namespace studio
