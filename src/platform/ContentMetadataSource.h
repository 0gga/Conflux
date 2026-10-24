#pragma once
#include "features/content/ContentIndex.h"
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <functional>
#include <memory>
namespace studio {
// Read-only identity/author resolution; update discovery has a separate lifecycle.
class ContentMetadataSource final : public QObject {
  public:
    using Callback = std::function<void(QVariantList, QString)>;
    using Progress = std::function<void(QVariantList)>;
    explicit ContentMetadataSource(std::shared_ptr<const IContentIndex> index = {}, QObject *parent = nullptr,
                                   QUrl modrinthApi = QUrl("https://api.modrinth.com/v2/"),
                                   QUrl curseforgeApi = QUrl("https://api.curseforge.com/v1/"),
                                   QHash<QString, QString> reviewedIdentities = {});
    void setCurseforgeKey(const QByteArray &);
    void resolve(const QVariantMap &, const QVariantList &, Callback, Progress = {});

  private:
    struct Batch;
    using JsonCallback = std::function<void(QJsonDocument, QString)>;
    std::shared_ptr<const IContentIndex> m_index;
    QNetworkAccessManager m_network;
    QUrl m_modrinthApi, m_curseforgeApi;
    QByteArray m_key;
    int m_authGeneration = 0;
    QHash<QString, QList<JsonCallback>> m_pendingReads;
    QHash<QString, QVariantMap> m_aliases;
    void json(const QString &provider, const QString &path, const QByteArray &, JsonCallback);
    void deliver(const QString &key, QJsonDocument, QString error, bool curseforge, int authGeneration);
    void identify(const std::shared_ptr<Batch> &);
    void fingerprints(const std::shared_ptr<Batch> &);
    void matchFingerprints(const std::shared_ptr<Batch> &);
    void projects(const std::shared_ptr<Batch> &);
    void teams(const std::shared_ptr<Batch> &);
    void applyProjects(const std::shared_ptr<Batch> &);
    void publish(const std::shared_ptr<Batch> &);
    void finish(const std::shared_ptr<Batch> &);
};
} // namespace studio
