#include "ContentOperationQueue.h"
#include <QPointer>
#include <QTimer>
#include <memory>
namespace studio {
void ContentOperationQueue::enqueue(Task task, QString group) {
    m_tasks.enqueue({std::move(task), std::move(group)});
    if (!m_running)
        startNext();
}
void ContentOperationQueue::discardQueued(const QString &group) {
    const auto removed =
        m_tasks.removeIf([&group](const QueuedTask &task) { return group.isEmpty() || task.group == group; });
    if (removed)
        emit changed();
}
void ContentOperationQueue::startNext() {
    if (m_tasks.isEmpty()) {
        m_running = false;
        emit changed();
        return;
    }
    m_running = true;
    auto task = m_tasks.dequeue();
    emit changed();
    const QPointer<ContentOperationQueue> guard(this);
    auto completed = std::make_shared<bool>(false);
    task.run([guard, completed] {
        if (!guard || *completed)
            return;
        *completed = true;
        // Do not recurse when a port completes synchronously.
        QTimer::singleShot(0, guard, [guard] {
            if (guard)
                guard->startNext();
        });
    });
}
} // namespace studio
