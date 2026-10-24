#pragma once
#include "ContentOperationState.h"
#include <QHash>
namespace studio {
// Derived claims only; inventory, pending commands and execution remain with their owners.
class ContentOperationReservations final {
  public:
    static QStringList keys(const ContentOperation &, const QVariantList &instanceRows);
    static QStringList keysForTargets(const ContentOperation &, const QVariantList &resolvedTargets);
    bool acquire(const ContentOperation &);
    void release(const ContentOperation &);

  private:
    static QString scope(const QString &);
    QHash<QString, QHash<QString, quint64>> m_claims;
};
} // namespace studio
