#pragma once
#include <QHash>
#include <QVariantList>
namespace studio {
// Derived lookups over one immutable inventory; target results retain inventory order.
class ContentTargetIndex final {
  public:
    QVariantList resolve(const QVariantList &rows, const QStringList &targets);

  private:
    QVariantList m_snapshot;
    QHash<QString, QList<qsizetype>> m_positions;
    bool m_ready = false;
};
} // namespace studio
