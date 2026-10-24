#include "ContentTargetIndex.h"
#include "ContentOperationState.h"
#include <QSet>
#include <algorithm>
namespace studio {
QVariantList ContentTargetIndex::resolve(const QVariantList &rows, const QStringList &targets) {
    if (rows.isEmpty() || targets.isEmpty())
        return {};
    if (!m_ready || !m_snapshot.isSharedWith(rows)) {
        m_ready = true;
        m_snapshot = rows;
        m_positions.clear();
        m_positions.reserve(rows.size());
        for (qsizetype i = 0; i < rows.size(); ++i)
            m_positions[contentRowKey(rows.at(i).toMap())] << i;
    }
    QList<qsizetype> positions;
    QSet<QString> seen;
    for (const auto &target : targets)
        if (!seen.contains(target)) {
            seen.insert(target);
            positions += m_positions.value(target);
        }
    std::sort(positions.begin(), positions.end());
    QVariantList result;
    result.reserve(positions.size());
    for (const auto position : positions)
        result << m_snapshot.at(position);
    return result;
}
} // namespace studio
