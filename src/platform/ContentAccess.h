#pragma once
#include <QMutex>
#include <QString>
#include <memory>
namespace studio {
struct ContentAccessGate;
// Worker-only instance leases cover complete snapshots and backed-up commits,
// including other Conflux processes. Recursive leases share one file lock.
class ContentAccess final {
  public:
    enum Mode { Read, Write };
    explicit ContentAccess(const QString &root, Mode mode = Read);
    ~ContentAccess();
    ContentAccess(const ContentAccess &) = delete;
    ContentAccess &operator=(const ContentAccess &) = delete;
    bool ok() const {
        return m_held;
    }
    QString error() const {
        return m_error;
    }

  private:
    std::shared_ptr<ContentAccessGate> m_gate;
    QMutexLocker<QRecursiveMutex> m_guard;
    bool m_held = false;
    QString m_error;
};
} // namespace studio
