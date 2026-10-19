#pragma once
#include <QJsonObject>
#include <QStringList>
namespace studio {
struct RepositoryResult {
    bool ok = false;
    QString error, path, status, history;
    QJsonObject manifest;
};
class PackRepository {
public:
    static RepositoryResult create(const QString& root, const QString& name, const QString& gameVersion, const QString& loader);
    static RepositoryResult inspect(const QString& path);
    static RepositoryResult pin(const QString& path, const QJsonObject& reference);
    static RepositoryResult remove(const QString& path, const QString& provider, const QString& projectId);
    static RepositoryResult commit(const QString& path, const QString& message);
    static RepositoryResult applyInstallation(const QString& path, const QJsonObject& expectedManifest,
        const QJsonArray& references, const QString& stagingDirectory);
    static QString slug(const QString& name);
private:
    static RepositoryResult write(const QString& path, const QJsonObject& manifest);
};
}
