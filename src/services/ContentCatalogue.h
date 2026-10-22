#pragma once
#include "features/content/ContentPorts.h"
#include "services/CatalogueClient.h"
#include <QNetworkAccessManager>
namespace studio {
class ContentCatalogue final : public QObject, public IContentCatalogue {
    Q_OBJECT
  public:
    explicit ContentCatalogue(QObject *parent = nullptr, QUrl api = QUrl("https://api.modrinth.com/v2/"),
                              QUrl curseforgeApi = QUrl("https://api.curseforge.com/v1/"));
    void refresh(const QVariantList &, const QString &, const QString &, IndexCallback) override;
    void versions(const QVariantMap &, const QString &, const QString &, VersionsCallback) override;
    void setCurseforgeKey(const QByteArray &key) override {
        m_client.setCurseforgeKey(key);
    }

  private:
    using JsonCallback = std::function<void(QJsonDocument, QString)>;
    void request(const QString &path, const QJsonObject *body, JsonCallback);
    void refreshCurseforge(QVariantList, const QString &, const QString &, IndexCallback, const QString &);
    QNetworkAccessManager m_network;
    QUrl m_api;
    CatalogueClient m_client;
    int m_generation = 0;
    QHash<int, VersionsCallback> m_versions;
};
} // namespace studio
