#pragma once
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
namespace studio {
// One coarse presentation clock per QML engine; metadata keeps its original timestamp.
class ContentTime final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(qint64 now READ now NOTIFY changed)
  public:
    explicit ContentTime(QObject *parent = nullptr);
    qint64 now() const;
    Q_INVOKABLE QString age(const QString &value, qint64 nowMs) const;
    Q_INVOKABLE QString dateLabel(const QString &value, qint64 nowMs) const;
    Q_INVOKABLE QString updatedHint(const QString &value, qint64 nowMs) const;
  signals:
    void changed();

  private:
    QTimer m_timer;
};
} // namespace studio
