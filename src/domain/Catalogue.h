#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace studio {
struct Project {
    QString provider, id, title, summary, author, icon, url;
    QStringList categories;
    qint64 downloads = 0;
    QMap<QString, QString> sourceIds;
    QMap<QString, QString> sourceUrls;
    QMap<QString, QString> authorIds;
    QString type = "mod", environment, updated, created;
    QStringList loaders;
    QMap<QString, qint64> sourceDownloads;
    qint64 followers = 0;
    QVariantMap toVariant() const;
};
struct Release {
    QString provider, projectId, id, version, title, filename, url, sha1, sha512, published;
    QString channel = "release";
    QStringList gameVersions, loaders, requiredDependencies;
    QJsonArray dependencyRefs;
    qint64 fileSize = 0;
    QMap<QString, QString> sourceIds;
    QVariantMap toVariant() const;
};
struct ProjectDetail {
    QString description;
    QStringList images, errors;
    QVector<Release> releases;
};
struct ContentInstallRequest {
    Release release;
    QString title;
    QJsonObject sourceIds;
    QString contentType = "mod";
};
// Only explicit, reviewed provider:id -> shared identity mappings may collapse projects.
QVector<Project> mergeProjects(const QVector<Project> &projects,
                               const QHash<QString, QString> &identities = {});
// Equal display labels do not establish equivalence. Equal files and compatibility do.
QVector<Release> mergeReleases(const QVector<Release> &releases);
Release modrinthRelease(const QJsonObject &version, const QString &fileSha1 = {});
bool isNewerRelease(const Release &installed, const Release &candidate);
QVariantList groupReleaseVariants(const QVector<Release> &releases);
Release releaseFromVariant(const QVariantMap &value);
QByteArray decodedImageFingerprint(const QByteArray &encodedImage);
QString compactCount(qint64 value);
} // namespace studio
