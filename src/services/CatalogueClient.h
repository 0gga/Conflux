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
    explicit CatalogueClient(QObject *parent = nullptr);
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
    void resolveProject(const QString &sha1, QMap<QString, QString> sourceIds, int generation);
  signals:
    void searchFinished(int generation, QVector<studio::Project> projects, QStringList errors);
    void detailFinished(int generation, studio::ProjectDetail detail);
    void projectResolved(int generation, studio::Project project, QString error);

  private:
    QNetworkAccessManager m_network;
    QByteArray m_curseforgeKey;
    using JsonCallback = std::function<void(QJsonDocument, QString)>;
    void get(const QUrl &url, bool curseforge, JsonCallback callback, bool allowMissing = false);
};
} // namespace studio
