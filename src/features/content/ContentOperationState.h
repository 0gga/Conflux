#pragma once
#include "ContentPorts.h"
#include <QHash>
namespace studio {
struct ContentOperation {
    enum Kind { Enable, Remove, Import, Install, Restore, Lock } kind = Enable;
    quint64 id = 0;
    QString packPath;
    QStringList targets, reservations;
    QHash<QString, QString> expectedHashes;
    QVariantMap sourceLock;
    bool enabled = true, locked = false;
    bool dependencyCleanup = false;
    QList<QUrl> urls;
    QString category;
    QVector<ContentInstallRequest> requests;
    QVariantList predictions;
};
QString contentRowKey(const QVariantMap &);
QVariantMap contentInstallPrediction(const ContentInstallRequest &, const QVariantMap &pack, QVariantMap row);
// Exact-file metadata only: concrete file state is preserved; ambiguous identities return no prediction.
QVariantMap contentRetainedPrediction(const ContentInstallRequest &, QVariantMap row);
QVariantList reconcileContentRows(const QVariantList &actual, const QVariantList &previous,
                                  const QVariantList &predictions = {}, bool indexedMetadata = false);
QVariantList projectContentOperations(QVariantList, const QList<ContentOperation> &);
QVariantList resolveContentTargets(const QVariantList &, const QStringList &);
} // namespace studio
