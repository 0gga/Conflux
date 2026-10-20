#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
namespace studio {
// Bounded image retrieval is separate from project metadata and view state.
class GalleryImages final : public QObject {
    Q_OBJECT
  public:
    explicit GalleryImages(QObject *parent = nullptr) : QObject(parent), m_network(this) {}
    void load(QStringList urls, int generation);
  signals:
    void ready(int generation, QStringList uniqueImages, QStringList errors);

  private:
    QNetworkAccessManager m_network;
    QList<QPointer<QNetworkReply>> m_replies;
};
} // namespace studio
