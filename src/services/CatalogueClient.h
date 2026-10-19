#pragma once
#include "domain/Catalogue.h"
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>

namespace studio {
struct ProjectDetail { QString description; QStringList images, errors; QVector<Release> releases; };
class CatalogueClient : public QObject {
    Q_OBJECT
public:
    explicit CatalogueClient(QObject* parent = nullptr);
    void setCurseforgeKey(const QByteArray& key) { m_curseforgeKey = key; }
    bool hasCurseforgeKey() const { return !m_curseforgeKey.isEmpty(); }
    void search(const QString& query, const QString& source, const QString& game, const QString& loader, int generation);
    void loadProject(const Project& project, const QString& game, const QString& loader, int generation);
signals:
    void searchFinished(int generation, QVector<studio::Project> projects, QStringList errors);
    void detailFinished(int generation, studio::ProjectDetail detail);
private:
    QNetworkAccessManager m_network;
    QByteArray m_curseforgeKey;
    using JsonCallback = std::function<void(QJsonDocument, QString)>;
    void get(const QUrl& url, bool curseforge, JsonCallback callback);
};
}
