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
    static RepositoryResult create(const QString &root, const QString &name, const QString &gameVersion,
                                   const QString &loader);
    static RepositoryResult inspect(const QString &path);
    // Verified manifest/ownership only; content transactions do not need Git presentation data.
    static RepositoryResult inspectContent(const QString &path);
    static RepositoryResult pin(const QString &path, const QJsonObject &reference);
    static RepositoryResult remove(const QString &path, const QString &provider, const QString &projectId);
    static RepositoryResult commit(const QString &path, const QString &message);
    static RepositoryResult updateContentPins(const QString &path, const QJsonObject &expected,
                                              const QJsonArray &pins);
    static RepositoryResult updateContentReferences(const QString &path, const QJsonObject &expected,
                                                    const QJsonArray &mods, const QJsonArray &assets);
    static RepositoryResult applyInstallation(const QString &path, const QJsonObject &expectedManifest,
                                              const QJsonArray &references, const QString &stagingDirectory,
                                              bool referenceOnly = false);
    static QString slug(const QString &name);

  private:
    static RepositoryResult write(const QString &path, const QJsonObject &manifest,
                                  bool includeHistory = true);
};
} // namespace studio
