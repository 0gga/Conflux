#pragma once
#include <QObject>
#include <QQueue>
#include <functional>
namespace studio {
// Serial owner-thread dispatch. Tasks and their completion callbacks run on the
// queue's thread; completion is idempotent and schedules the next task asynchronously.
class ContentOperationQueue final : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void()>;
    using Task = std::function<void(Completion)>;
    explicit ContentOperationQueue(QObject *parent = nullptr) : QObject(parent) {}
    void enqueue(Task, QString group = {});
    void discardQueued(const QString &group = {});
    bool busy() const {
        return m_running || !m_tasks.isEmpty();
    }
  signals:
    void changed();

  private:
    void startNext();
    struct QueuedTask {
        Task run;
        QString group;
    };
    QQueue<QueuedTask> m_tasks;
    bool m_running = false;
};
} // namespace studio
