#include "NavigationInput.h"
#include "NavigationHistory.h"
#include <QMouseEvent>
namespace studio {
NavigationInput::NavigationInput(NavigationHistory &history, QObject *parent)
    : QObject(parent), m_history(history) {}
void NavigationInput::attach(QObject &window) {
    window.installEventFilter(this);
}
bool NavigationInput::eventFilter(QObject *, QEvent *event) {
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease)
        return false;
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::BackButton && mouse->button() != Qt::ForwardButton)
        return false;
    if (event->type() == QEvent::MouseButtonPress) {
        if (mouse->button() == Qt::BackButton)
            m_history.back();
        else
            m_history.forward();
    }
    return true;
}
} // namespace studio
