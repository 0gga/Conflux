#include "OperationHistory.h"
#include <QTimer>
namespace studio {
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
    beginInsertRows({}, 0, 0);
    m_entries.prepend(QVariantMap{{"id", QVariant::fromValue(id)},
                                  {"pack", pack.value("title", pack.value("name"))},
                                  {"path", pack.value("path")},
                                  {"icon", pack.value("icon")},
                                  {"title", title},
                                  {"action", action},
                                  {"state", "queued"},
                                  {"message", QString{}},
                                  {"retryable", false}});
    endInsertRows();
    while (m_entries.size() > 100) {
        int index = -1;
        for (int i = m_entries.size() - 1; i >= 0; --i)
            if (m_entries[i].toMap().value("state") != "running" &&
                m_entries[i].toMap().value("state") != "queued") {
                index = i;
                break;
            }
        if (index < 0)
            break;
        beginRemoveRows({}, index, index);
        m_entries.removeAt(index);
        endRemoveRows();
    }
    emit changed();
}
void OperationHistory::set(quint64 id, const QString &key, const QVariant &value) {
    for (int i = 0; i < m_entries.size(); ++i) {
        auto row = m_entries[i].toMap();
        if (row.value("id").toULongLong() == id) {
            row[key] = value;
            m_entries[i] = row;
            emit dataChanged(index(i), index(i), {Qt::UserRole + 1});
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
    set(id, "state", ok && error.isEmpty() ? "done" : "failed");
    set(id, "message", error);
    set(id, "retryable", !ok && retryable);
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
