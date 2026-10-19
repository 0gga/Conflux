#pragma once
#include <QHash>
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
    QVariantMap toVariant() const;
};
struct Release {
    QString provider, projectId, id, version, title, filename, url, sha1, sha512, published;
    QStringList gameVersions, loaders, requiredDependencies;
    QMap<QString, QString> sourceIds;
    QVariantMap toVariant() const;
};
// Only explicit, reviewed provider:id -> shared identity mappings may collapse projects.
QVector<Project> mergeProjects(const QVector<Project>& projects, const QHash<QString, QString>& identities = {});
// Equal display labels do not establish equivalence. Equal files and compatibility do.
QVector<Release> mergeReleases(const QVector<Release>& releases);
QByteArray decodedImageFingerprint(const QByteArray& encodedImage);
QString compactCount(qint64 value);
}
