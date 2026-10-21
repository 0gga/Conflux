#include "WindowFrame.h"
#include <QGuiApplication>
#include <QtMath>
#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <qt_windows.h>
#include <windowsx.h>
#endif
namespace studio {
WindowFrame::WindowFrame(QWindow &window) : m_window(&window) {
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != "windows")
        return;
    m_nativeId = window.winId();
    qApp->installNativeEventFilter(this);
    const auto handle = reinterpret_cast<HWND>(m_nativeId);
    const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
    SetWindowLongPtrW(handle, GWL_STYLE,
                      style | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    // Windows 11 honors this preference; older systems keep their compositor policy.
    const DWORD corners = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(handle, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corners, sizeof(corners));
    const MARGINS shadow{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(handle, &shadow);
    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#endif
}
WindowFrame::~WindowFrame() {
    if (m_nativeId && qApp)
        qApp->removeNativeEventFilter(this);
}
bool WindowFrame::nativeEventFilter(const QByteArray &type, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    if (!m_window || (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG"))
        return false;
    const auto *event = static_cast<MSG *>(message);
    if (event->hwnd != reinterpret_cast<HWND>(m_nativeId))
        return false;
    if (event->message == WM_NCCALCSIZE && event->wParam) {
        if (IsZoomed(event->hwnd)) {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (GetMonitorInfoW(MonitorFromWindow(event->hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
                reinterpret_cast<NCCALCSIZE_PARAMS *>(event->lParam)->rgrc[0] = monitor.rcWork;
        }
        *result = 0;
        return true;
    }
    if (event->message == WM_NCHITTEST && !IsZoomed(event->hwnd) &&
        m_window->windowState() != Qt::WindowFullScreen) {
        RECT rect;
        if (!GetWindowRect(event->hwnd, &rect))
            return false;
        const auto x = GET_X_LPARAM(event->lParam), y = GET_Y_LPARAM(event->lParam);
        const int grip = qCeil(6 * m_window->devicePixelRatio());
        const bool left = x >= rect.left && x < rect.left + grip;
        const bool right = x < rect.right && x >= rect.right - grip;
        const bool top = y >= rect.top && y < rect.top + grip;
        const bool bottom = y < rect.bottom && y >= rect.bottom - grip;
        if (!left && !right && !top && !bottom)
            return false;
        *result = top      ? (left    ? HTTOPLEFT
                              : right ? HTTOPRIGHT
                                      : HTTOP)
                  : bottom ? (left    ? HTBOTTOMLEFT
                              : right ? HTBOTTOMRIGHT
                                      : HTBOTTOM)
                  : left   ? HTLEFT
                           : HTRIGHT;
        return true;
    }
#else
    Q_UNUSED(type)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}
} // namespace studio
