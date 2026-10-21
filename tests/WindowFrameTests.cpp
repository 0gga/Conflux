#include "platform/WindowFrame.h"
#include <QGuiApplication>
#include <QWindow>
#include <QtTest>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <windowsx.h>
#endif
class WindowFrameTests : public QObject {
    Q_OBJECT
  private slots:
    void nativeFramePreservesResizeAndMaximizedWorkArea() {
#ifndef Q_OS_WIN
        QSKIP("Native Windows frame integration");
#else
        if (QGuiApplication::platformName() != "windows")
            QSKIP("Requires the native Windows platform");
        QWindow window;
        window.setFlags(Qt::Window | Qt::FramelessWindowHint);
        window.setMinimumSize({300, 200});
        window.setGeometry(100, 100, 600, 400);
        studio::WindowFrame frame(window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto handle = reinterpret_cast<HWND>(window.winId());
        const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
        QVERIFY(style & WS_THICKFRAME);
        QVERIFY(style & WS_MAXIMIZEBOX);
        QVERIFY(style & WS_MINIMIZEBOX);
        RECT bounds, client;
        QVERIFY(GetWindowRect(handle, &bounds));
        QVERIFY(GetClientRect(handle, &client));
        QCOMPARE(client.right, bounds.right - bounds.left);
        QCOMPARE(client.bottom, bounds.bottom - bounds.top);
        QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(bounds.left + 2, bounds.top + 2)),
                 LRESULT(HTTOPLEFT));
        QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, MAKELPARAM(bounds.right - 2, bounds.bottom - 2)),
                 LRESULT(HTBOTTOMRIGHT));
        window.showMaximized();
        QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
        QTest::qWait(100);
        MONITORINFO monitor{sizeof(MONITORINFO)};
        QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
        QVERIFY(GetClientRect(handle, &client));
        POINT origin{0, 0};
        QVERIFY(ClientToScreen(handle, &origin));
        QCOMPARE(origin.x, LONG(monitor.rcWork.left));
        QCOMPARE(origin.y, LONG(monitor.rcWork.top));
        QCOMPARE(client.right, monitor.rcWork.right - monitor.rcWork.left);
        QCOMPARE(client.bottom, monitor.rcWork.bottom - monitor.rcWork.top);
        window.showNormal();
        QTRY_COMPARE(window.windowState(), Qt::WindowNoState);
        window.showMinimized();
        QTRY_COMPARE(window.windowState(), Qt::WindowMinimized);
        window.showNormal();
        QTRY_VERIFY(window.isVisible());
        QVERIFY(window.close());
#endif
    }
};
QTEST_MAIN(WindowFrameTests)
#include "WindowFrameTests.moc"
