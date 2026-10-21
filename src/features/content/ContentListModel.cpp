#include "ContentListModel.h"
namespace studio {
QVariant ContentListModel::data(const QModelIndex &index, int role) const {
    return index.isValid() && index.row() < m_rows.size() && role == Qt::UserRole + 1 ? m_rows.at(index.row())
                                                                                      : QVariant{};
}
void ContentListModel::setRows(const QVariantList &rows) {
    const auto key = [](const QVariant &value) {
        const auto row = value.toMap();
        return row.value("contentKey", row.value("relativePath")).toString();
    };
    for (int i = 0; i < rows.size(); ++i) {
        const auto wanted = key(rows.at(i));
        int existing = i;
        while (existing < m_rows.size() && key(m_rows.at(existing)) != wanted)
            ++existing;
        if (existing == m_rows.size()) {
            beginInsertRows({}, i, i);
            m_rows.insert(i, rows.at(i));
            endInsertRows();
        } else {
            if (existing != i) {
                beginMoveRows({}, existing, existing, {}, i);
                m_rows.move(existing, i);
                endMoveRows();
            }
            if (m_rows.at(i) != rows.at(i)) {
                m_rows[i] = rows.at(i);
                emit dataChanged(index(i), index(i), {Qt::UserRole + 1});
            }
        }
    }
    if (m_rows.size() > rows.size()) {
        beginRemoveRows({}, rows.size(), m_rows.size() - 1);
        m_rows.erase(m_rows.begin() + rows.size(), m_rows.end());
        endRemoveRows();
    }
}
} // namespace studio
