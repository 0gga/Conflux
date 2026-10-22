#pragma once
#include "ContentPorts.h"
#include <QHash>
namespace studio {
struct ContentOperation {
    enum Kind { Enable, Remove, Import, Install, Restore } kind = Enable;
    quint64 id = 0;
    QString packPath;
    QStringList targets, reservations;
    QHash<QString, QString> expectedHashes;
    QVariantMap sourceLock;
    bool enabled = true;
    QList<QUrl> urls;
    QString category;
    QVector<ContentInstallRequest> requests;
    QVariantList predictions;
};
QString contentRowKey(const QVariantMap &);
QVariantMap contentInstallPrediction(const ContentInstallRequest &, const QVariantMap &pack, QVariantMap row);
QVariantList reconcileContentRows(const QVariantList &actual, const QVariantList &previous,
                                  const QVariantList &predictions = {}, bool indexedMetadata = false);
QVariantList projectContentOperations(QVariantList, const QList<ContentOperation> &);
QVariantList resolveContentTargets(const QVariantList &, const QStringList &);
} // namespace studio
