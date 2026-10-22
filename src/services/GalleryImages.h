#pragma once
#include <QCache>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
namespace studio {
// Bounded image retrieval is separate from project metadata and view state.
class GalleryImages final : public QObject {
    Q_OBJECT
  public:
    explicit GalleryImages(QObject *parent = nullptr, bool allowLoopback = false)
        : QObject(parent), m_network(this), m_allowLoopback(allowLoopback) {
        m_clock.start();
    }
    void load(QStringList urls, int generation);
    void cancel();
  signals:
    void ready(int generation, QStringList uniqueImages, QStringList errors);

  private:
    QNetworkAccessManager m_network;
    QList<QPointer<QNetworkReply>> m_replies;
    struct Fingerprint {
        QByteArray hash;
        qint64 expires;
    };
    QCache<QString, Fingerprint> m_cache{160};
    QElapsedTimer m_clock;
    int m_epoch = 0;
    bool m_allowLoopback = false;
};
} // namespace studio
