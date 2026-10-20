#pragma once
#include "platform/PackFileStore.h"
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <memory>
#include <vector>
namespace studio {
class IWorldLaunchAdapter {
  public:
    virtual ~IWorldLaunchAdapter() = default;
    virtual bool supports(const QVariantMap &pack) const = 0;
    virtual QString unavailableReason(const QVariantMap &pack) const = 0;
    virtual QUrl worldUrl(const QVariantMap &pack, const QString &worldFolder) const = 0;
};
class ILaunchUrlOpener {
  public:
    virtual ~ILaunchUrlOpener() = default;
    virtual bool available(const QString &scheme) const = 0;
    virtual bool open(const QUrl &) = 0;
};
class WorldLaunch final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool packAvailable READ packAvailable NOTIFY changed)
  public:
    WorldLaunch(std::vector<std::shared_ptr<const IWorldLaunchAdapter>> adapters,
                std::shared_ptr<ILaunchUrlOpener> opener, std::shared_ptr<const IPackFileReader> files,
                QObject *parent = nullptr);
    void setPack(const QVariantMap &);
    bool available() const {
        return unavailableReason().isEmpty();
    }
    QString unavailableReason() const;
    QString status() const {
        return m_status;
    }
    Q_INVOKABLE void play(const QString &relativePath);
    bool packAvailable() const;
    Q_INVOKABLE void playPack();
  signals:
    void changed();

  private:
    const IWorldLaunchAdapter *adapter() const;
    std::vector<std::shared_ptr<const IWorldLaunchAdapter>> m_adapters;
    std::shared_ptr<ILaunchUrlOpener> m_opener;
    std::shared_ptr<const IPackFileReader> m_files;
    QVariantMap m_pack;
    QString m_status;
};
} // namespace studio
