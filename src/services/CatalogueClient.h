#pragma once
#include "domain/Catalogue.h"
#include "domain/CatalogueSearch.h"
#include <QCache>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>
#include <memory>

namespace studio {
class CatalogueClient : public QObject, public ICatalogueSearch {
    Q_OBJECT
  public:
    explicit CatalogueClient(QObject *parent = nullptr,
                             QUrl modrinthApi = QUrl("https://api.modrinth.com/v2/"),
                             QUrl curseforgeApi = QUrl("https://api.curseforge.com/v1/"));
    void setCurseforgeKey(const QByteArray &key);
    // Owning features cancel their operations separately; obsolete read callbacks are discarded.
    void cancelPendingReads();
    // Opt-in for read-only presentation clients, never installer validation.
    void setReadCacheLifetime(int milliseconds);
    bool hasCurseforgeKey() const {
        return !m_curseforgeKey.isEmpty();
    }
    void search(const QString &query, const QString &source, const QString &game, const QString &loader,
                int generation, const QString &type = "mod");
    void fetch(const CatalogueQuery &, int offset, int limit, Callback) override;
    void facets(std::function<void(QVariantMap, QString)>) override;
    void setKey(const QByteArray &key) override {
        setCurseforgeKey(key);
    }
    static QUrl searchUrl(const CatalogueQuery &, int offset, int limit, int classId = 6);
    void loadProject(const Project &project, const QString &game, const QString &loader, int generation,
                     bool allVersions = false);
    void loadChangelog(const Release &, std::function<void(QString, QString)>);
    void curseforgeVersions(const QString &projectId, const QString &game, const QStringList &loaders,
                            std::function<void(QVector<Release>, QString)>);
    void resolveProject(const QString &sha1, QMap<QString, QString> sourceIds, int generation);
  signals:
    void searchFinished(int generation, QVector<studio::Project> projects, QStringList errors);
    void detailFinished(int generation, studio::ProjectDetail detail);
    void descriptionReady(int generation, QString description);
    void projectResolved(int generation, studio::Project project, QString error);

  private:
    QNetworkAccessManager m_network;
    QByteArray m_curseforgeKey;
    QUrl m_modrinthApi, m_curseforgeApi;
    QMap<QString, QString> m_hashProjects, m_linkedProjects;
    using JsonCallback = std::function<void(QJsonDocument, QString)>;
    struct CachedResponse {
        QJsonDocument document;
        qint64 expires = 0;
    };
    struct PendingRead {
        QList<JsonCallback> callbacks;
    };
    QCache<QString, CachedResponse> m_readCache{16 * 1024 * 1024};
    QHash<QString, std::shared_ptr<PendingRead>> m_pendingReads;
    QElapsedTimer m_cacheClock;
    int m_cacheLifetime = 0, m_authGeneration = 0, m_readGeneration = 0;
    void request(const QUrl &url, bool curseforge, JsonCallback callback, bool allowMissing = false,
                 const QByteArray &body = {});
    void linkCurseforgeFiles(CataloguePage, QMap<QString, QStringList>, Callback);
};
} // namespace studio
