#include "ContentListModel.h"
#include <QHash>
#include <QSet>
namespace studio {
namespace {
using Identity = QPair<QString, int>;
QList<Identity> identities(const QVariantList &rows) {
    QHash<QString, int> occurrences;
    QList<Identity> keys;
    keys.reserve(rows.size());
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto key = row.value("contentKey", row.value("relativePath")).toString();
        keys << qMakePair(key, occurrences[key]++);
    }
    return keys;
}
} // namespace
QVariant ContentListModel::data(const QModelIndex &index, int role) const {
    return index.isValid() && index.row() < m_rows.size() && role == Qt::UserRole + 1 ? m_rows.at(index.row())
                                                                                      : QVariant{};
}
void ContentListModel::setRows(const QVariantList &rows) {
    if (rows == m_rows)
        return;
    const auto wanted = identities(rows);
    auto current = identities(m_rows);
    const QSet<Identity> desired(wanted.cbegin(), wanted.cend());
    // Remove obsolete rows first: ordinary filtering retains the surviving relative order.
    for (int end = current.size(); end > 0;) {
        if (desired.contains(current.at(end - 1))) {
            --end;
            continue;
        }
        int first = end - 1;
        while (first > 0 && !desired.contains(current.at(first - 1)))
            --first;
        beginRemoveRows({}, first, end - 1);
        m_rows.erase(m_rows.begin() + first, m_rows.begin() + end);
        current.erase(current.begin() + first, current.begin() + end);
        endRemoveRows();
        end = first;
    }
    QHash<Identity, int> positions;
    for (int i = 0; i < current.size(); ++i)
        positions.insert(current.at(i), i);
    QList<Identity> ordered;
    ordered.reserve(current.size());
    for (const auto &key : wanted)
        if (positions.contains(key))
            ordered << key;
    if (ordered != current) {
        emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
        const auto previous = persistentIndexList();
        QHash<Identity, int> destinations;
        QVariantList moved;
        moved.reserve(current.size());
        for (int i = 0; i < ordered.size(); ++i) {
            destinations.insert(ordered.at(i), i);
            moved << m_rows.at(positions.value(ordered.at(i)));
        }
        QModelIndexList next;
        next.reserve(previous.size());
        for (const auto &index : previous)
            next << this->index(destinations.value(current.at(index.row())));
        m_rows = std::move(moved);
        current = std::move(ordered);
        changePersistentIndexList(previous, next);
        emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
    }
    const QSet<Identity> retained(current.cbegin(), current.cend());
    for (int i = 0; i < wanted.size();) {
        if (i < current.size() && current.at(i) == wanted.at(i)) {
            ++i;
            continue;
        }
        int end = i;
        while (end < wanted.size() && !retained.contains(wanted.at(end)))
            ++end;
        Q_ASSERT(end > i);
        beginInsertRows({}, i, end - 1);
        m_rows.insert(i, end - i, QVariant{});
        current.insert(i, end - i, Identity{});
        for (int j = i; j < end; ++j) {
            m_rows[j] = rows.at(j);
            current[j] = wanted.at(j);
        }
        endInsertRows();
        i = end;
    }
    int changed = -1;
    for (int i = 0; i <= rows.size(); ++i) {
        const bool different = i < rows.size() && m_rows.at(i) != rows.at(i);
        if (different) {
            m_rows[i] = rows.at(i);
            if (changed < 0)
                changed = i;
        } else if (changed >= 0) {
            emit dataChanged(index(changed), index(i - 1), {Qt::UserRole + 1});
            changed = -1;
        }
    }
}
} // namespace studio
