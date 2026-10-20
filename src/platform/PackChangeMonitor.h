#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
namespace studio {
class PackChangeMonitor final : public QObject {
    Q_OBJECT
  public:
    explicit PackChangeMonitor(QObject *parent = nullptr);
    void setRoot(const QString &);
    void rebuild();
  signals:
    void changed();

  private:
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_root;
};
} // namespace studio
