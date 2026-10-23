#include "ContentOperationState.h"
#include "domain/ContentTypes.h"
#include <QHash>
#include <QSet>
#include <algorithm>
namespace studio {
static QString normalized(QString path) {
    if (path.endsWith(".disabled"))
        path.chop(9);
    return path;
}
QString contentRowKey(const QVariantMap &row) {
    return row
        .value("contentKey",
               row.value("category").toString() + ":" + normalized(row.value("relativePath").toString()))
        .toString();
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
    const QStringList metadata{"sourceIds",
                               "external",
                               "provider",
                               "projectId",
                               "installedRelease",
                               "updateRelease",
                               "updateAvailable",
                               "projectUrl",
                               "author",
                               "authorIcon",
                               "authorIds",
                               "authors",
                               "environment",
                               "openSource",
                               "title",
                               "version",
                               "icon"};
    QSet<QString> keys;
    for (const auto &value : actual) {
        auto row = value.toMap();
        row.remove("pending");
        row.remove("lockPending");
        const auto category = row.value("category").toString(), hash = row.value("sha1").toString();
        const auto path = paths.value({category, normalized(row.value("relativePath").toString())});
        const auto same = path.value("sha1").toString() == hash        ? path
                          : ambiguousHashes.contains({category, hash}) ? QVariantMap{}
                                                                       : hashes.value({category, hash});
        if (!hash.isEmpty() && !same.isEmpty()) {
            const bool identityKnown =
                !row.value("sourceIds").toMap().isEmpty() || !row.value("installedRelease").toMap().isEmpty();
            for (const auto &key : metadata) {
                if (key == "sourceIds") {
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
                    // Fresh index results are authoritative, including explicit false update flags.
                    const bool identityField = key == "external" || key == "provider" || key == "projectId";
                    const bool preferPrevious = !(identityField ? identityKnown : indexedMetadata) &&
                                                key != "installedRelease" && key != "updateRelease" &&
                                                key != "updateAvailable";
                    if (same.contains(key) && (missing || preferPrevious))
                        row[key] = same.value(key);
                }
            }
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
