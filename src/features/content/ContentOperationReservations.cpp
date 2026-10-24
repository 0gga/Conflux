#include "ContentOperationReservations.h"
#include "domain/ContentTypes.h"
#include <QDir>
#include <QSet>
namespace studio {
QString ContentOperationReservations::scope(const QString &path) {
    auto key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
QStringList ContentOperationReservations::keys(const ContentOperation &operation,
                                               const QVariantList &instanceRows) {
    return keysForTargets(operation, resolveContentTargets(instanceRows, operation.targets));
}
QStringList ContentOperationReservations::keysForTargets(const ContentOperation &operation,
                                                         const QVariantList &targets) {
    QSet<QString> result;
    auto add = [&](const QVariantMap &row) {
        auto path = row.value("relativePath").toString();
        if (path.endsWith(".disabled"))
            path.chop(9);
#ifdef Q_OS_WIN
        path = path.toCaseFolded();
#endif
        if (!path.isEmpty())
            result.insert("path:" + path);
        auto ids = row.value("sourceIds").toMap();
        const auto provider = row.value("provider").toString();
        const auto id = row.value("projectId", row.value("id")).toString();
        if (!provider.isEmpty() && !id.isEmpty())
            ids[provider] = id;
        for (auto it = ids.cbegin(); it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty())
                result.insert("project:" + row.value("category").toString() + ':' + it.key() + ':' +
                              it.value().toString());
    };
    for (const auto &value : targets)
        add(value.toMap());
    for (const auto &value : operation.predictions)
        add(value.toMap());
    for (const auto &request : operation.requests) {
        auto row = request.release.toVariant();
        row["sourceIds"] = request.sourceIds.toVariantMap();
        row["category"] = contentCategory(request.contentType);
        add(row);
    }
    if (!operation.sourceLock.isEmpty()) {
        auto row = operation.sourceLock;
        row["category"] = row.value("relativePath").toString().section('/', 0, 0);
        add(row);
    }
    return result.values();
}
bool ContentOperationReservations::acquire(const ContentOperation &operation) {
    if (!operation.id)
        return false;
    const auto pack = scope(operation.packPath);
    const auto existing = m_claims.constFind(pack);
    if (existing != m_claims.cend())
        for (const auto &key : operation.reservations) {
            const auto owner = existing->constFind(key);
            if (owner != existing->cend() && *owner != operation.id)
                return false;
        }
    if (!operation.reservations.isEmpty()) {
        auto &claims = m_claims[pack];
        for (const auto &key : operation.reservations)
            claims.insert(key, operation.id);
    }
    return true;
}
void ContentOperationReservations::release(const ContentOperation &operation) {
    const auto pack = m_claims.find(scope(operation.packPath));
    if (pack == m_claims.end())
        return;
    for (const auto &key : operation.reservations) {
        const auto owner = pack->find(key);
        if (owner != pack->end() && *owner == operation.id)
            pack->erase(owner);
    }
    if (pack->isEmpty())
        m_claims.erase(pack);
}
} // namespace studio
