#pragma once
#include "domain/Catalogue.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <functional>

namespace studio {
// Verifies author distribution and exact file metadata before the content transaction.
// Only new installs expand required dependencies; explicit version changes remain shallow.
class CurseforgeInstallSource final : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(QVector<ContentInstallRequest>, QString)>;
    explicit CurseforgeInstallSource(QObject *parent = nullptr,
                                     QUrl api = QUrl("https://api.curseforge.com/v1/"));
    void setKey(const QByteArray &);
    bool busy() const {
        return bool(m_callback);
    }
    void prepare(const QVariantMap &, const QVariantList &, const QVector<ContentInstallRequest> &, Callback);
    void resolveDependency(const QVariantMap &, const QVariantList &, ContentInstallRequest, Callback);
    void cancel();

  private:
    QNetworkAccessManager m_network;
    QUrl m_api;
    QByteArray m_key;
    QNetworkReply *m_reply = nullptr;
    Callback m_callback;
    int m_generation = 0;
    QString m_game;
    QStringList m_loaders;
    QVariantList m_rows;
    QQueue<ContentInstallRequest> m_pending;
    QVector<ContentInstallRequest> m_prepared;
    QMap<QString, QString> m_selected;
    QSet<QString> m_shallow;
    void get(const QString &, std::function<void(QJsonObject)>);
    void next();
    void select(ContentInstallRequest, const QJsonObject &, int offset = 0, Release candidate = {});
    void accept(ContentInstallRequest, const QJsonObject &);
    void finish(QString error = {});
};
} // namespace studio
