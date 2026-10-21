#include "ContentOperationQueue.h"
#include <QPointer>
#include <QTimer>
#include <memory>
namespace studio {
void ContentOperationQueue::enqueue(Task task) {
    m_tasks.enqueue(std::move(task));
    if (!m_running)
        startNext();
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
    task([guard, completed] {
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
