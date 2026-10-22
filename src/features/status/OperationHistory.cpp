#include "OperationHistory.h"
#include <QTimer>
namespace studio {
int OperationHistory::insertionIndex(const QVariantMap &row, int excluded) const {
    const auto priority = [](const QVariantMap &entry) {
        const auto state = entry.value("state").toString();
        return state == "running" ? 0 : state == "queued" ? 1 : 2;
    };
    const auto rank = priority(row);
    const auto id = row.value("id").toULongLong();
    int position = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (i == excluded)
            continue;
        const auto other = m_entries[i].toMap();
        const auto otherRank = priority(other);
        if (otherRank < rank || (otherRank == rank && (rank == 1 ? other.value("id").toULongLong() < id
                                                                 : other.value("id").toULongLong() > id)))
            ++position;
    }
    return position;
}
int OperationHistory::reposition(int from) {
    const int to = insertionIndex(m_entries[from].toMap(), from);
    if (to != from) {
        beginMoveRows({}, from, from, {}, to + (to > from ? 1 : 0));
        m_entries.move(from, to);
        endMoveRows();
    }
    return to;
}
QVariant OperationHistory::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size() || role != Qt::UserRole + 1)
        return {};
    return m_entries[index.row()];
}
int OperationHistory::activeCount() const {
    int count = 0;
    for (const auto &value : m_entries)
        if (QStringList{"queued", "running"}.contains(value.toMap().value("state").toString()))
            ++count;
    return count;
}
int OperationHistory::failedCount() const {
    int count = 0;
    for (const auto &value : m_entries)
        if (value.toMap().value("state") == "failed")
            ++count;
    return count;
}
void OperationHistory::queued(quint64 id, const QVariantMap &pack, const QString &title,
                              const QString &action) {
    const QVariantMap row{{"id", QVariant::fromValue(id)},
                          {"pack", pack.value("title", pack.value("name"))},
                          {"path", pack.value("path")},
                          {"icon", pack.value("icon")},
                          {"title", title},
                          {"action", action},
                          {"state", "queued"},
                          {"message", QString{}},
                          {"retryable", false}};
    const auto position = insertionIndex(row);
    beginInsertRows({}, position, position);
    m_entries.insert(position, row);
    endInsertRows();
    prune();
    emit changed();
}
void OperationHistory::prune() {
    int inactive = m_entries.size() - activeCount();
    for (int i = m_entries.size() - 1; inactive > 100 && i >= 0; --i) {
        const auto row = m_entries[i].toMap();
        if (row.value("state") == "running" || row.value("state") == "queued")
            continue;
        const auto id = row.value("id").toULongLong();
        beginRemoveRows({}, i, i);
        m_entries.removeAt(i);
        endRemoveRows();
        --inactive;
        emit dismissed(id);
    }
}
void OperationHistory::set(quint64 id, const QString &key, const QVariant &value) {
    for (int i = 0; i < m_entries.size(); ++i) {
        auto row = m_entries[i].toMap();
        if (row.value("id").toULongLong() == id) {
            row[key] = value;
            m_entries[i] = row;
            const auto position = key == "state" ? reposition(i) : i;
            emit dataChanged(index(position), index(position), {Qt::UserRole + 1});
            break;
        }
    }
    emit changed();
}
void OperationHistory::start(quint64 id) {
    set(id, "state", "running");
}
void OperationHistory::progress(quint64 id, const QString &text) {
    set(id, "message", text);
}
void OperationHistory::finish(quint64 id, bool ok, const QString &error, bool retryable) {
    for (int i = 0; i < m_entries.size(); ++i) {
        auto row = m_entries[i].toMap();
        if (row.value("id").toULongLong() != id)
            continue;
        row["state"] = ok && error.isEmpty() ? "done" : "failed";
        row["message"] = error;
        row["retryable"] = !ok && retryable;
        m_entries[i] = row;
        const auto position = reposition(i);
        emit dataChanged(index(position), index(position), {Qt::UserRole + 1});
        break;
    }
    prune();
    emit changed();
    if (ok && error.isEmpty())
        QTimer::singleShot(1800, this, [this, id] { dismiss(id); });
}
void OperationHistory::dismiss(quint64 id) {
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].toMap().value("id").toULongLong() == id) {
            const auto state = m_entries[i].toMap().value("state").toString();
            if (state == "running" || state == "queued")
                return;
            beginRemoveRows({}, i, i);
            m_entries.removeAt(i);
            endRemoveRows();
            emit changed();
            emit dismissed(id);
            return;
        }
}
void OperationHistory::clearFinished() {
    const int first = activeCount();
    if (first == m_entries.size())
        return;
    QList<quint64> ids;
    for (int i = first; i < m_entries.size(); ++i)
        ids << m_entries[i].toMap().value("id").toULongLong();
    beginRemoveRows({}, first, m_entries.size() - 1);
    m_entries.erase(m_entries.begin() + first, m_entries.end());
    endRemoveRows();
    emit changed();
    for (const auto id : ids)
        emit dismissed(id);
}
QString OperationHistory::details(quint64 id) const {
    for (const auto &value : m_entries) {
        const auto row = value.toMap();
        if (row.value("id").toULongLong() == id)
            return row.value("pack").toString() + "\n" + row.value("title").toString() + "\n" +
                   row.value("action").toString() + "\n" + row.value("message").toString();
    }
    return {};
}
} // namespace studio
