#include "Catalogue.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QImage>
#include <QVersionNumber>
#include <algorithm>
#include <limits>

namespace studio {
QString compactCount(qint64 n) {
    if (n >= 1000000)
        return QString::number(n / 1000000.0, 'f', 1) + "M";
    if (n >= 1000)
        return QString::number(n / 1000.0, 'f', 1) + "K";
    return QString::number(n);
}
static QVariantMap variantIds(const QMap<QString, QString> &ids) {
    QVariantMap out;
    for (auto it = ids.cbegin(); it != ids.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}
static QStringList orderedSources(const QMap<QString, QString> &ids) {
    auto sources = ids.keys();
    if (sources.removeAll("modrinth"))
        sources.prepend("modrinth");
    return sources;
}
qint64 Project::totalDownloads() const {
    qint64 total = 0;
    auto counts = sourceDownloads;
    if (!counts.contains(provider))
        counts[provider] = downloads;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (it.key() != provider && !sourceIds.contains(it.key()))
            continue;
        const qint64 count = qMax(qint64(0), it.value());
        total += qMin(count, std::numeric_limits<qint64>::max() - total);
    }
    return total;
}
QVariantMap Project::toVariant() const {
    QVariantList statistics;
    auto counts = sourceDownloads;
    if (!counts.contains(provider))
        counts[provider] = downloads;
    for (const auto &source :
         orderedSources(sourceIds.isEmpty() ? QMap<QString, QString>{{provider, id}} : sourceIds))
        if (counts.contains(source))
            statistics << QVariantMap{
                {"provider", source}, {"count", compactCount(counts[source])}, {"raw", counts[source]}};
    return {{"provider", provider},
            {"id", id},
            {"title", title},
            {"summary", summary},
            {"author", author},
            {"authorIds", variantIds(authorIds)},
            {"icon", icon},
            {"url", url},
            {"categories", categories},
            {"downloads", compactCount(totalDownloads())},
            {"downloadsRaw", totalDownloads()},
            {"sources", orderedSources(sourceIds)},
            {"sourceIds", variantIds(sourceIds)},
            {"type", type},
            {"loaders", loaders},
            {"environment", environment},
            {"updated", updated},
            {"created", created},
            {"followers", compactCount(followers)},
            {"followersRaw", followers},
            {"downloadStats", statistics}};
}
QVariantMap Release::toVariant() const {
    return {{"provider", provider},
            {"projectId", projectId},
            {"id", id},
            {"version", version},
            {"title", title},
            {"filename", filename},
            {"url", url},
            {"sha1", sha1},
            {"sha512", sha512},
            {"published", published.left(10)},
            {"channel", channel},
            {"gameVersions", gameVersions},
            {"loaders", loaders},
            {"dependencies", requiredDependencies},
            {"dependencyRefs", dependencyRefs.toVariantList()},
            {"fileSize", fileSize},
            {"sources", orderedSources(sourceIds)},
            {"sourceIds", variantIds(sourceIds)}};
}
Release modrinthRelease(const QJsonObject &row, const QString &fileSha1) {
    Release r;
    r.provider = "modrinth";
    r.channel = row.value("version_type").toString("release");
    r.projectId = row.value("project_id").toString();
    r.id = row.value("id").toString();
    r.version = row.value("version_number").toString();
    r.title = row.value("name").toString();
    r.published = row.value("date_published").toString();
    for (const auto &value : row.value("game_versions").toArray())
        r.gameVersions << value.toString();
    for (const auto &value : row.value("loaders").toArray())
        r.loaders << value.toString();
    QJsonObject primary;
    for (const auto &value : row.value("files").toArray()) {
        const auto file = value.toObject();
        if (!fileSha1.isEmpty() && file.value("hashes").toObject().value("sha1").toString().compare(
                                       fileSha1, Qt::CaseInsensitive) != 0)
            continue;
        if (!file.value("filename").toString().endsWith(".jar", Qt::CaseInsensitive) &&
            !file.value("filename").toString().endsWith(".zip", Qt::CaseInsensitive))
            continue;
        if (primary.isEmpty() || file.value("primary").toBool())
            primary = file;
    }
    r.filename = primary.value("filename").toString();
    r.url = primary.value("url").toString();
    r.fileSize = primary.value("size").toInteger();
    r.sha1 = primary.value("hashes").toObject().value("sha1").toString();
    r.sha512 = primary.value("hashes").toObject().value("sha512").toString();
    r.dependencyRefs = row.value("dependencies").toArray();
    for (const auto &value : r.dependencyRefs) {
        const auto dep = value.toObject();
        if (dep.value("dependency_type").toString() != "required")
            continue;
        const auto project = dep.value("project_id").toString();
        r.requiredDependencies << (project.isEmpty() ? "version:" + dep.value("version_id").toString()
                                                     : project);
    }
    return r;
}
bool isNewerRelease(const Release &installed, const Release &candidate) {
    const auto sameHash = [](const QString &a, const QString &b) {
        return !a.isEmpty() && !b.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0;
    };
    if (sameHash(installed.sha1, candidate.sha1) || sameHash(installed.sha512, candidate.sha512))
        return false;
    const auto oldVersion = QVersionNumber::fromString(installed.version),
               newVersion = QVersionNumber::fromString(candidate.version);
    if (!oldVersion.isNull() && !newVersion.isNull() && QVersionNumber::compare(newVersion, oldVersion) < 0)
        return false;
    const auto before = QDateTime::fromString(installed.published, Qt::ISODateWithMs);
    const auto after = QDateTime::fromString(candidate.published, Qt::ISODateWithMs);
    return installed.projectId == candidate.projectId && installed.id != candidate.id && before.isValid() &&
           after.isValid() && after > before;
}
QVariantList groupReleaseVariants(const QVector<Release> &releases) {
    QVariantList groups;
    QMap<QString, int> positions;
    for (int index = 0; index < releases.size(); ++index) {
        const auto &release = releases.at(index);
        const auto label = release.version.isEmpty() ? release.title : release.version;
        const auto key = label.isEmpty() ? release.provider + ':' + release.id : label;
        if (!positions.contains(key)) {
            positions[key] = groups.size();
            groups << QVariantMap{{"label", label}, {"variants", QVariantList{}}};
        }
        const int position = positions.value(key);
        auto group = groups.at(position).toMap();
        auto variants = group.value("variants").toList();
        auto variant = release.toVariant();
        variant["releaseIndex"] = index;
        int insertion = variants.size();
        if (release.provider == "modrinth") {
            insertion = 0;
            while (insertion < variants.size() &&
                   variants.at(insertion).toMap().value("provider") == "modrinth")
                ++insertion;
        }
        variants.insert(insertion, variant);
        group["variants"] = variants;
        groups[position] = group;
    }
    return groups;
}
Release releaseFromVariant(const QVariantMap &value) {
    Release r;
    r.provider = value.value("provider").toString();
    r.channel = value.value("channel", "release").toString();
    r.projectId = value.value("projectId").toString();
    r.id = value.value("id").toString();
    r.version = value.value("version").toString();
    r.title = value.value("title").toString();
    r.filename = value.value("filename").toString();
    r.url = value.value("url").toString();
    r.sha1 = value.value("sha1").toString();
    r.sha512 = value.value("sha512").toString();
    r.published = value.value("published").toString();
    r.fileSize = value.value("fileSize").toLongLong();
    r.gameVersions = value.value("gameVersions").toStringList();
    r.loaders = value.value("loaders").toStringList();
    r.requiredDependencies = value.value("dependencies").toStringList();
    r.dependencyRefs = QJsonArray::fromVariantList(value.value("dependencyRefs").toList());
    const auto ids = value.value("sourceIds").toMap();
    for (auto it = ids.begin(); it != ids.end(); ++it)
        r.sourceIds[it.key()] = it.value().toString();
    return r;
}
QVector<Project> mergeProjects(const QVector<Project> &projects, const QHash<QString, QString> &identities) {
    QVector<Project> result;
    QHash<QString, qsizetype> positions;
    for (auto project : projects) {
        const auto providerId = project.provider + ':' + project.id;
        const auto key = identities.value(providerId, providerId);
        project.sourceIds.insert(project.provider, project.id);
        project.sourceDownloads[project.provider] = project.downloads;
        if (!positions.contains(key)) {
            positions.insert(key, result.size());
            result.append(project);
            continue;
        }
        auto &existing = result[positions[key]];
        auto ids = existing.sourceIds;
        auto downloads = existing.sourceDownloads;
        for (auto it = project.sourceDownloads.cbegin(); it != project.sourceDownloads.cend(); ++it)
            downloads.insert(it.key(), it.value());
        downloads.insert(project.provider, project.downloads);
        ids.insert(project.provider, project.id);
        if (project.provider == "modrinth")
            existing = project;
        existing.sourceIds = ids;
        existing.sourceDownloads = downloads;
    }
    return result;
}
QVector<Release> mergeReleases(const QVector<Release> &releases) {
    QVector<Release> result;
    QHash<QString, qsizetype> positions;
    for (auto release : releases) {
        auto games = release.gameVersions, loaders = release.loaders;
        games.sort();
        loaders.sort();
        const auto key = release.sha1.isEmpty()
                             ? release.provider + ':' + release.id
                             : release.sha1.toLower() + '|' + games.join(',') + '|' + loaders.join(',');
        release.sourceIds.insert(release.provider, release.id);
        if (!positions.contains(key)) {
            positions.insert(key, result.size());
            result.append(release);
            continue;
        }
        auto &existing = result[positions[key]];
        auto ids = existing.sourceIds;
        ids.insert(release.provider, release.id);
        if (release.provider == "modrinth")
            existing = release;
        existing.sourceIds = ids;
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const Release &a, const Release &b) { return a.published > b.published; });
    return result;
}
QByteArray decodedImageFingerprint(const QByteArray &bytes) {
    const auto image = QImage::fromData(bytes).convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull())
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(image.width()) + ':' + QByteArray::number(image.height()));
    for (int y = 0; y < image.height(); ++y)
        hash.addData(
            QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)), image.width() * 4));
    return hash.result();
}
} // namespace studio
