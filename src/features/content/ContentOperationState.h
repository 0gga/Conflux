#pragma once
#include "ContentPorts.h"
namespace studio {
struct ContentOperation {
    enum Kind { Enable, Remove, Import, Install } kind = Enable;
    quint64 id = 0;
    QString packPath;
    QStringList targets;
    bool enabled = true;
    QList<QUrl> urls;
    QString category;
    QVector<ContentInstallRequest> requests;
    QVariantList predictions;
};
QString contentRowKey(const QVariantMap &);
QVariantList reconcileContentRows(const QVariantList &actual, const QVariantList &previous,
                                  const QVariantList &predictions = {});
QVariantList projectContentOperations(QVariantList, const QList<ContentOperation> &);
QVariantList resolveContentTargets(const QVariantList &, const QStringList &);
} // namespace studio
