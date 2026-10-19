#include "Catalogue.h"
#include <QCryptographicHash>
#include <QImage>
#include <algorithm>

namespace studio {
QString compactCount(qint64 n) {
    if (n >= 1000000) return QString::number(n / 1000000.0, 'f', 1) + "M";
    if (n >= 1000) return QString::number(n / 1000.0, 'f', 1) + "K";
    return QString::number(n);
}
static QVariantMap variantIds(const QMap<QString, QString>& ids) {
    QVariantMap out;
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) out.insert(it.key(), it.value());
    return out;
}
QVariantMap Project::toVariant() const {
    return {{"provider", provider}, {"id", id}, {"title", title}, {"summary", summary},
        {"author", author}, {"icon", icon}, {"url", url}, {"categories", categories},
        {"downloads", compactCount(downloads)}, {"sources", sourceIds.keys()}, {"sourceIds", variantIds(sourceIds)}};
}
QVariantMap Release::toVariant() const {
    return {{"provider", provider}, {"projectId", projectId}, {"id", id}, {"version", version},
        {"title", title}, {"filename", filename}, {"url", url}, {"sha1", sha1}, {"sha512", sha512},
        {"published", published.left(10)}, {"gameVersions", gameVersions}, {"loaders", loaders},
        {"dependencies", requiredDependencies}, {"sources", sourceIds.keys()}, {"sourceIds", variantIds(sourceIds)}};
}
QVector<Project> mergeProjects(const QVector<Project>& projects, const QHash<QString, QString>& identities) {
    QVector<Project> result;
    QHash<QString, qsizetype> positions;
    for (auto project : projects) {
        const auto providerId = project.provider + ':' + project.id;
        const auto key = identities.value(providerId, providerId);
        project.sourceIds.insert(project.provider, project.id);
        if (!positions.contains(key)) {
            positions.insert(key, result.size()); result.append(project); continue;
        }
        auto& existing = result[positions[key]];
        auto ids = existing.sourceIds;
        ids.insert(project.provider, project.id);
        if (project.provider == "modrinth") existing = project;
        existing.sourceIds = ids;
    }
    return result;
}
QVector<Release> mergeReleases(const QVector<Release>& releases) {
    QVector<Release> result;
    QHash<QString, qsizetype> positions;
    for (auto release : releases) {
        auto games = release.gameVersions, loaders = release.loaders;
        games.sort(); loaders.sort();
        const auto key = release.sha1.isEmpty() ? release.provider + ':' + release.id
            : release.sha1.toLower() + '|' + games.join(',') + '|' + loaders.join(',');
        release.sourceIds.insert(release.provider, release.id);
        if (!positions.contains(key)) {
            positions.insert(key, result.size()); result.append(release); continue;
        }
        auto& existing = result[positions[key]];
        auto ids = existing.sourceIds;
        ids.insert(release.provider, release.id);
        if (release.provider == "modrinth") existing = release;
        existing.sourceIds = ids;
    }
    std::stable_sort(result.begin(), result.end(), [](const Release& a, const Release& b) { return a.published > b.published; });
    return result;
}
QByteArray decodedImageFingerprint(const QByteArray& bytes) {
    const auto image = QImage::fromData(bytes).convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(image.width()) + ':' + QByteArray::number(image.height()));
    for (int y = 0; y < image.height(); ++y)
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 4));
    return hash.result();
}
}
