#pragma once
#include <QObject>
#include <QQueue>
#include <functional>
namespace studio {
class ContentOperationQueue final : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void()>;
    using Task = std::function<void(Completion)>;
    explicit ContentOperationQueue(QObject *parent = nullptr) : QObject(parent) {}
    void enqueue(Task);
    bool busy() const {
        return m_running || !m_tasks.isEmpty();
    }
  signals:
    void changed();

  private:
    void startNext();
    QQueue<Task> m_tasks;
    bool m_running = false;
};
} // namespace studio
