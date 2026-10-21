#include "ContentOperationState.h"
#include <QHash>
#include <QSet>
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
                                  const QVariantList &predictions) {
    QHash<QPair<QString, QString>, QVariantMap> hashes, paths;
    for (const auto &value : previous + predictions) {
        const auto row = value.toMap();
        const auto category = row.value("category").toString(), hash = row.value("sha1").toString();
        if (!hash.isEmpty())
            hashes.insert({category, hash}, row);
        paths.insert({category, normalized(row.value("relativePath").toString())}, row);
    }
    QVariantList result;
    const QStringList metadata{"sourceIds",        "external",      "provider",        "projectId",
                               "installedRelease", "updateRelease", "updateAvailable", "projectUrl",
                               "author",           "authorIcon",    "authorIds",       "environment",
                               "openSource",       "title",         "version",         "icon"};
    QSet<QString> keys;
    for (const auto &value : actual) {
        auto row = value.toMap();
        row.remove("pending");
        const auto category = row.value("category").toString(), hash = row.value("sha1").toString();
        const auto same = hashes.value({category, hash});
        if (!hash.isEmpty() && !same.isEmpty())
            for (const auto &key : metadata)
                if (same.contains(key))
                    row[key] = same.value(key);
        auto old = paths.value({category, normalized(row.value("relativePath").toString())});
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
    for (const auto &value : rows)
        if (targets.contains(contentRowKey(value.toMap())))
            out << value;
    return out;
}
QVariantList projectContentOperations(QVariantList rows, const QList<ContentOperation> &operations) {
    for (const auto &operation : operations) {
        if (operation.kind == ContentOperation::Remove) {
            for (int i = rows.size() - 1; i >= 0; --i)
                if (operation.targets.contains(contentRowKey(rows.at(i).toMap())))
                    rows.removeAt(i);
        } else if (operation.kind == ContentOperation::Enable) {
            for (auto &value : rows) {
                auto row = value.toMap();
                if (operation.targets.contains(contentRowKey(row))) {
                    row["enabled"] = operation.enabled;
                    row["pending"] = true;
                    value = row;
                }
            }
        } else {
            for (const auto &value : operation.predictions) {
                auto predicted = value.toMap();
                predicted["pending"] = true;
                int index = -1;
                for (int i = 0; i < rows.size(); ++i)
                    if (contentRowKey(rows.at(i).toMap()) == contentRowKey(predicted)) {
                        index = i;
                        break;
                    }
                if (index >= 0)
                    rows[index] = predicted;
                else
                    rows << predicted;
            }
        }
    }
    return rows;
}
} // namespace studio
