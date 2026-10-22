#pragma once
#include "domain/Catalogue.h"
#include "domain/CatalogueSearch.h"
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>

namespace studio {
class CatalogueClient : public QObject, public ICatalogueSearch {
    Q_OBJECT
  public:
    explicit CatalogueClient(QObject *parent = nullptr,
                             QUrl modrinthApi = QUrl("https://api.modrinth.com/v2/"),
                             QUrl curseforgeApi = QUrl("https://api.curseforge.com/v1/"));
    void setCurseforgeKey(const QByteArray &key) {
        m_curseforgeKey = key;
    }
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
    void projectResolved(int generation, studio::Project project, QString error);

  private:
    QNetworkAccessManager m_network;
    QByteArray m_curseforgeKey;
    QUrl m_modrinthApi, m_curseforgeApi;
    QMap<QString, QString> m_hashProjects, m_linkedProjects;
    using JsonCallback = std::function<void(QJsonDocument, QString)>;
    void request(const QUrl &url, bool curseforge, JsonCallback callback, bool allowMissing = false,
                 const QByteArray &body = {});
    void linkCurseforgeFiles(CataloguePage, QMap<QString, QStringList>, Callback);
};
} // namespace studio
