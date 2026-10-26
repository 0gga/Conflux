#include "ContentOperationState.h"
#include "domain/ContentTypes.h"
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
namespace studio {
static QString normalized(QString path) {
    if (path.endsWith(".disabled"))
        path.chop(9);
    return path;
}
QString contentRowKey(const QVariantMap &row) {
    const auto key = row.constFind(QStringLiteral("contentKey"));
    if (key != row.cend())
        return key->toString();
    return row.value(QStringLiteral("category")).toString() + ':' +
           normalized(row.value(QStringLiteral("relativePath")).toString());
}
static bool knownHash(const QString &hash) {
    static const QRegularExpression pattern("^[a-fA-F0-9]{40}$");
    return hash.size() == 40 && pattern.match(hash).hasMatch();
}
static bool conflictingSources(const QVariantMap &left, const QVariantMap &right) {
    const auto a = left.value("sourceIds").toMap(), b = right.value("sourceIds").toMap();
    for (auto it = a.cbegin(); it != a.cend(); ++it) {
        const auto other = b.value(it.key()).toString();
        if (!it.value().toString().isEmpty() && !other.isEmpty() && it.value().toString() != other)
            return true;
    }
    return false;
}
static bool verifiedModrinthRelease(const QVariantMap &row) {
    const auto release = row.value("installedRelease").toMap();
    const auto hash = row.value("sha1").toString(), project = release.value("projectId").toString();
    return release.value("provider") == "modrinth" && !release.value("id").toString().isEmpty() &&
           !project.isEmpty() && knownHash(hash) &&
           release.value("sha1").toString().compare(hash, Qt::CaseInsensitive) == 0 &&
           row.value("sourceIds").toMap().value("modrinth").toString() == project;
}
QVariantMap contentRetainedPrediction(const ContentInstallRequest &request, QVariantMap row) {
    const auto &release = request.release;
    const auto hash = row.value("sha1").toString();
    if (row.value("category") != "mods" || row.value("relativePath").toString().isEmpty() ||
        !knownHash(hash) || release.sha1.compare(hash, Qt::CaseInsensitive) != 0 ||
        release.projectId.isEmpty() || release.id.isEmpty() ||
        (release.provider != "modrinth" && release.provider != "curseforge"))
        return {};
    auto incoming = request.sourceIds.toVariantMap();
    incoming[release.provider] = release.projectId;
    if (conflictingSources(row, {{"sourceIds", incoming}}))
        return {};
    const bool preserveModrinth = release.provider != "modrinth" && verifiedModrinthRelease(row);
    const auto previousProvider = row.value("provider").toString();
    const auto previousProject = row.value("projectId").toString();
    auto urls = row.value("sourceUrls").toMap();
    if ((previousProvider == "modrinth" || previousProvider == "curseforge") &&
        !row.value("projectUrl").toString().isEmpty())
        urls[previousProvider] = row.value("projectUrl");
    auto ids = row.value("sourceIds").toMap();
    for (auto it = incoming.cbegin(); it != incoming.cend(); ++it)
        if ((it.key() == "modrinth" || it.key() == "curseforge") && !it.value().toString().isEmpty())
            ids[it.key()] = it.value();
    row["sourceIds"] = ids;
    if (!preserveModrinth) {
        row["installedRelease"] = release.toVariant();
        row["version"] = release.version;
    }
    if (release.provider == "modrinth" || ids.value("modrinth").toString().isEmpty()) {
        row["provider"] = release.provider;
        row["projectId"] = release.projectId;
        if (!request.title.isEmpty())
            row["title"] = request.title;
    } else {
        row["provider"] = "modrinth";
        row["projectId"] = ids.value("modrinth");
    }
    if (row.value("provider") == "modrinth") {
        const auto url = "https://modrinth.com/mod/" + row.value("projectId").toString();
        row["projectUrl"] = url;
        urls["modrinth"] = url;
    }
    if (!urls.isEmpty())
        row["sourceUrls"] = urls;
    if (row.value("provider").toString() != previousProvider ||
        row.value("projectId").toString() != previousProject) {
        row["metadataResolved"] = false;
        row.remove("metadataSha1");
        row.remove("metadataAttemptedAt");
    }
    row["contentKey"] = contentRowKey(row);
    return row;
}
static void retainVerifiedModrinth(QVariantMap &row, const QVariantMap &previous) {
    const auto current = row.value("installedRelease").toMap();
    if (current.value("provider") == "modrinth" && row.value("provider") == "modrinth" &&
        row.value("projectId") == current.value("projectId"))
        return;
    const bool currentVerified = verifiedModrinthRelease(row),
               previousVerified = verifiedModrinthRelease(previous);
    if (!currentVerified && !previousVerified)
        return;
    const auto release = currentVerified ? current : previous.value("installedRelease").toMap();
    const auto project = release.value("projectId").toString();
    if (row.value("sourceIds").toMap().value("modrinth").toString() != project)
        return;
    const bool currentPresentation =
        row.value("provider") == "modrinth" && row.value("projectId").toString() == project;
    row["provider"] = "modrinth";
    row["projectId"] = project;
    row["installedRelease"] = release;
    row["version"] = release.value("version");
    if (!currentPresentation) {
        const bool complete = previousVerified && previous.value("provider") == "modrinth" &&
                              previous.value("projectId").toString() == project &&
                              previous.value("metadataResolved").toBool() &&
                              previous.value("metadataSha1")
                                      .toString()
                                      .compare(row.value("sha1").toString(), Qt::CaseInsensitive) == 0;
        row["metadataResolved"] = complete;
        if (complete)
            row["metadataSha1"] = row.value("sha1");
        else {
            row.remove("metadataSha1");
            row.remove("metadataAttemptedAt");
        }
    }
    if (currentPresentation || !previousVerified || previous.value("provider") != "modrinth" ||
        previous.value("projectId").toString() != project)
        return;
    for (const auto &field : {"title", "projectUrl", "icon", "author", "authorIcon", "authorIds", "authors",
                              "environment", "openSource", "categories", "tags"}) {
        const auto value = previous.value(field);
        const QLatin1StringView name(field);
        if ((name == QLatin1StringView("categories") || name == QLatin1StringView("tags")) &&
            value.isValid()) {
            row[field] = value;
            continue;
        }
        if (value.isValid() && !value.isNull() &&
            !(value.typeId() == QMetaType::QString && value.toString().isEmpty()) &&
            !(value.typeId() == QMetaType::QVariantMap && value.toMap().isEmpty()) &&
            !(value.typeId() == QMetaType::QVariantList && value.toList().isEmpty()))
            row[field] = value;
    }
}
QVariantMap contentInstallPrediction(const ContentInstallRequest &request, const QVariantMap &pack,
                                     QVariantMap row) {
    const auto category = contentCategory(request.contentType);
    row["title"] = request.title;
    row["category"] = category;
    row["sourceIds"] = request.sourceIds.toVariantMap();
    row["provider"] = request.release.provider;
    row["projectId"] = request.release.projectId;
    const auto stableKey = row.value("relativePath").toString().isEmpty() ? QString{} : contentRowKey(row);
    row["relativePath"] =
        category + '/' +
        (category == "mods" && pack.value("managed").toBool() ? request.release.sha1 + ".jar"
                                                              : request.release.filename) +
        (row.contains("enabled") && !row.value("enabled").toBool() ? ".disabled" : "");
    if (!stableKey.isEmpty() && stableKey != ":")
        row["contentKey"] = stableKey;
    else
        row.remove("contentKey");
    row["fileName"] = request.release.filename;
    row["version"] = request.release.version;
    row["sha1"] = request.release.sha1;
    row["installedRelease"] = request.release.toVariant();
    row["updateAvailable"] = false;
    row["contentKey"] = contentRowKey(row);
    if (!row.contains("enabled"))
        row["enabled"] = true;
    return row;
}
static bool sameProject(const QVariantMap &a, const QVariantMap &b) {
    if (a.value("category") != b.value("category"))
        return false;
    const auto left = a.value("sourceIds").toMap(), right = b.value("sourceIds").toMap();
    for (auto it = left.begin(); it != left.end(); ++it)
        if (!it.value().toString().isEmpty() && right.value(it.key()) == it.value())
            return true;
    return false;
}
QVariantList reconcileContentRows(const QVariantList &actual, const QVariantList &previous,
                                  const QVariantList &predictions, bool indexedMetadata) {
    QHash<QPair<QString, QString>, QVariantMap> hashes, paths;
    QSet<QPair<QString, QString>> ambiguousHashes;
    for (const auto &value : previous + predictions) {
        const auto row = value.toMap();
        const auto category = row.value("category").toString(), hash = row.value("sha1").toString();
        if (!hash.isEmpty()) {
            if (hashes.contains({category, hash}))
                ambiguousHashes.insert({category, hash});
            hashes.insert({category, hash}, row);
        }
        paths.insert({category, normalized(row.value("relativePath").toString())}, row);
    }
    QVariantList result;
    const QStringList metadata{
        "sourceIds",     "sourceUrls",      "external",   "provider", "projectId",  "installedRelease",
        "updateRelease", "updateAvailable", "projectUrl", "author",   "authorIcon", "authorIds",
        "authors",       "environment",     "openSource", "title",    "version",    "icon"};
    QSet<QString> keys;
    for (const auto &value : actual) {
        auto row = value.toMap();
        row.remove("pending");
        row.remove("lockPending");
        const auto category = row.value("category").toString(), hash = row.value("sha1").toString();
        const auto path = paths.value({category, normalized(row.value("relativePath").toString())});
        auto same = path.value("sha1").toString() == hash        ? path
                    : ambiguousHashes.contains({category, hash}) ? QVariantMap{}
                                                                 : hashes.value({category, hash});
        if (conflictingSources(row, same))
            same.clear();
        if (!hash.isEmpty() && !same.isEmpty()) {
            const bool preferCurrentModrinth = row.value("provider") == "modrinth" &&
                                               same.value("provider") != "modrinth" &&
                                               verifiedModrinthRelease(row);
            const bool identityKnown =
                !row.value("sourceIds").toMap().isEmpty() || !row.value("installedRelease").toMap().isEmpty();
            for (const auto &key : metadata) {
                if (key == "sourceIds" || key == "sourceUrls") {
                    auto ids = same.value(key).toMap();
                    const auto fresh = row.value(key).toMap();
                    for (auto it = fresh.cbegin(); it != fresh.cend(); ++it)
                        if (!it.value().toString().isEmpty())
                            ids.insert(it.key(), it.value());
                    if (!ids.isEmpty())
                        row[key] = ids;
                } else if (key == "updateRelease" && row.contains("updateAvailable") &&
                           !row.value("updateAvailable").toBool()) {
                    row.remove(key);
                } else {
                    const bool missing = !row.contains(key) || !row.value(key).isValid() ||
                                         (row.value(key).typeId() == QMetaType::QString &&
                                          row.value(key).toString().isEmpty()) ||
                                         (row.value(key).typeId() == QMetaType::QVariantMap &&
                                          row.value(key).toMap().isEmpty());
                    // A raw filesystem refresh may borrow its unchanged file's presentation.
                    // Fresh index facts remain authoritative within a provider. Verified Modrinth
                    // presentation has priority across providers, including metadata-only aliases.
                    const bool identityField = key == "external" || key == "provider" || key == "projectId";
                    const bool preferPrevious =
                        !preferCurrentModrinth && !(identityField ? identityKnown : indexedMetadata) &&
                        key != "installedRelease" && key != "updateRelease" && key != "updateAvailable";
                    if (same.contains(key) && (missing || preferPrevious))
                        row[key] = same.value(key);
                }
            }
            retainVerifiedModrinth(row, same);
        }
        auto old = path;
        if (old.isEmpty() && !same.isEmpty())
            old = same;
        if (old.isEmpty()) {
            QVariantList matches;
            for (const auto &candidate : previous + predictions)
                if (sameProject(row, candidate.toMap()))
                    matches << candidate;
            if (matches.size() == 1)
                old = matches.first().toMap();
        }
        auto key = old.isEmpty() ? contentRowKey(row) : contentRowKey(old);
        if (keys.contains(key))
            key = category + ":" + row.value("relativePath").toString();
        keys.insert(key);
        row["contentKey"] = key;
        result << row;
    }
    return result;
}
QVariantList resolveContentTargets(const QVariantList &rows, const QStringList &targets) {
    if (rows.isEmpty() || targets.isEmpty())
        return {};
    QVariantList out;
    const QSet<QString> keys(targets.cbegin(), targets.cend());
    for (const auto &value : rows)
        if (keys.contains(contentRowKey(value.toMap())))
            out << value;
    return out;
}
QVariantList projectContentOperations(QVariantList rows, const QList<ContentOperation> &operations) {
    QHash<QString, int> indices;
    indices.reserve(rows.size());
    for (int i = 0; i < rows.size(); ++i)
        indices.insert(contentRowKey(rows.at(i).toMap()), i);
    bool removed = false;
    for (const auto &operation : operations) {
        if (operation.kind == ContentOperation::Remove) {
            for (const auto &key : operation.targets) {
                const auto found = indices.find(key);
                if (found != indices.end()) {
                    rows[*found] = QVariant{};
                    indices.erase(found);
                    removed = true;
                }
            }
        } else if (operation.kind == ContentOperation::Enable || operation.kind == ContentOperation::Lock) {
            for (const auto &key : operation.targets) {
                const auto found = indices.constFind(key);
                if (found != indices.cend()) {
                    auto row = rows.at(*found).toMap();
                    if (operation.kind == ContentOperation::Lock)
                        row["lockPending"] = operation.locked;
                    else
                        row["enabled"] = operation.enabled;
                    row["pending"] = true;
                    rows[*found] = row;
                }
            }
        } else {
            for (const auto &value : operation.predictions) {
                auto predicted = value.toMap();
                predicted["pending"] = true;
                const auto key = contentRowKey(predicted);
                const auto index = indices.value(key, -1);
                if (index >= 0)
                    rows[index] = predicted;
                else {
                    indices.insert(key, rows.size());
                    rows << predicted;
                }
            }
        }
    }
    if (removed)
        rows.erase(
            std::remove_if(rows.begin(), rows.end(), [](const auto &value) { return !value.isValid(); }),
            rows.end());
    return rows;
}
} // namespace studio
