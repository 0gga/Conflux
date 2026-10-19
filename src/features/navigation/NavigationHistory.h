#pragma once
#include <QObject>
#include <QVariantMap>
#include <functional>
namespace studio {
class NavigationHistory final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap route READ route NOTIFY changed)
    Q_PROPERTY(bool canBack READ canBack NOTIFY changed)
    Q_PROPERTY(bool canForward READ canForward NOTIFY changed)
    Q_PROPERTY(bool hasPending READ hasPending NOTIFY pendingChanged)
  public:
    explicit NavigationHistory(QObject *parent = nullptr);
    QVariantMap route() const {
        return m_routes.at(m_index);
    }
    bool canBack() const {
        return m_index > 0;
    }
    bool canForward() const {
        return m_index + 1 < m_routes.size();
    }
    bool hasPending() const {
        return !m_pending.isEmpty();
    }
    void setLeaveGuard(std::function<bool()> guard) {
        m_guard = std::move(guard);
    }
    Q_INVOKABLE bool go(QVariantMap route);
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();
    Q_INVOKABLE void resumePending();
    Q_INVOKABLE void cancelPending();
  signals:
    void changed();
    void pendingChanged();
    void blocked();

  private:
    QList<QVariantMap> m_routes;
    int m_index = 0, m_pendingIndex = -1;
    QVariantMap m_pending;
    std::function<bool()> m_guard;
    bool request(const QVariantMap &, int historyIndex);
};
} // namespace studio
