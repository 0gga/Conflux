#include "NavigationHistory.h"
namespace studio {
NavigationHistory::NavigationHistory(QObject *parent)
    : QObject(parent), m_routes{{{"page", "library"}, {"section", "content"}}} {}
bool NavigationHistory::request(const QVariantMap &target, int historyIndex) {
    if (target == route())
        return true;
    if (m_guard && !m_guard()) {
        m_pending = target;
        m_pendingIndex = historyIndex;
        emit pendingChanged();
        emit blocked();
        return false;
    }
    if (historyIndex >= 0)
        m_index = historyIndex;
    else {
        while (m_routes.size() > m_index + 1)
            m_routes.removeLast();
        m_routes.append(target);
        m_index = m_routes.size() - 1;
        if (m_routes.size() > 80) {
            m_routes.removeFirst();
            --m_index;
        }
    }
    emit changed();
    return true;
}
bool NavigationHistory::go(QVariantMap target) {
    if (!QStringList{"library", "discover", "workspace", "settings"}.contains(
            target.value("page").toString()))
        return false;
    if (!target.contains("section"))
        target["section"] = "content";
    return request(target, -1);
}
void NavigationHistory::back() {
    if (canBack())
        request(m_routes.at(m_index - 1), m_index - 1);
}
void NavigationHistory::forward() {
    if (canForward())
        request(m_routes.at(m_index + 1), m_index + 1);
}
void NavigationHistory::resumePending() {
    const auto target = m_pending;
    const int index = m_pendingIndex;
    cancelPending();
    if (!target.isEmpty())
        request(target, index);
}
void NavigationHistory::cancelPending() {
    m_pending.clear();
    m_pendingIndex = -1;
    emit pendingChanged();
}
} // namespace studio
