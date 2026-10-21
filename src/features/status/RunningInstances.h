#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <memory>
namespace studio {
struct RunningInstanceResult {
    QVariantList instances;
    QString error;
};
class IInstanceProcessSource {
  public:
    virtual ~IInstanceProcessSource() = default;
    virtual RunningInstanceResult scan(const QVariantList &) const = 0;
};
class RunningInstances final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList instances READ instances NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    explicit RunningInstances(std::shared_ptr<const IInstanceProcessSource>, QObject *parent = nullptr);
    QVariantList instances() const {
        return m_instances;
    }
    QString error() const {
        return m_error;
    }
    void setPacks(const QVariantList &);
    Q_INVOKABLE void refresh();
  signals:
    void changed();

  private:
    std::shared_ptr<const IInstanceProcessSource> m_source;
    QVariantList m_packs, m_instances;
    QString m_error;
    QTimer m_timer;
    bool m_busy = false;
    int m_generation = 0;
};
} // namespace studio
