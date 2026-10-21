#include "platform/WindowFrame.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
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
        QQuickWindow window;
        window.setColor(QColor("#24121b"));
        window.setFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                        Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
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
        const QRect normal = window.geometry();
        auto verifyGeometry = [&] {
            QTest::qWait(60);
            RECT outer{}, area{};
            QVERIFY(GetWindowRect(handle, &outer));
            QVERIFY(GetClientRect(handle, &area));
            const qreal dpr = window.devicePixelRatio();
            QVERIFY(qAbs(area.right - qRound(window.width() * dpr)) <= 1);
            QVERIFY(qAbs(area.bottom - qRound(window.height() * dpr)) <= 1);
            const QImage rendered = window.grabWindow();
            QVERIFY(!rendered.isNull());
            QVERIFY(qAbs(rendered.width() - area.right) <= 1);
            QVERIFY(qAbs(rendered.height() - area.bottom) <= 1);
            QCOMPARE(rendered.pixelColor(rendered.width() / 2, rendered.height() - 2), window.color());
            if (IsZoomed(handle)) {
                MONITORINFO monitor{sizeof(MONITORINFO)};
                QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
                POINT origin{0, 0};
                QVERIFY(ClientToScreen(handle, &origin));
                QCOMPARE(origin.x, monitor.rcWork.left);
                QCOMPARE(origin.y, monitor.rcWork.top);
                QCOMPARE(area.right, monitor.rcWork.right - monitor.rcWork.left);
                QCOMPARE(area.bottom, monitor.rcWork.bottom - monitor.rcWork.top);
            } else {
                QCOMPARE(area.right, outer.right - outer.left);
                QCOMPARE(area.bottom, outer.bottom - outer.top);
            }
        };
        // Both entry paths matter: Qt controls and Windows Snap/system commands.
        for (int cycle = 0; cycle < 3; ++cycle) {
            window.showMaximized();
            QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
            QVERIFY(IsZoomed(handle));
            verifyGeometry();
            window.showNormal();
            QTRY_COMPARE(window.windowState(), Qt::WindowNoState);
            QTRY_COMPARE(window.geometry(), normal);
            verifyGeometry();
            SendMessageW(handle, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
            QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
            verifyGeometry();
            // This reproduces Restore after Windows initiated maximization.
            window.showNormal();
            QTRY_COMPARE(window.windowState(), Qt::WindowNoState);
            QTRY_COMPARE(window.geometry(), normal);
            verifyGeometry();
        }
        // Exercise a real held-pointer system move from native maximization. The
        // release timer also prevents a failed restore from trapping the move loop.
        window.showMaximized();
        QTRY_COMPARE(window.windowState(), Qt::WindowMaximized);
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        POINT previousCursor{};
        GetCursorPos(&previousCursor);
        POINT anchor{qRound(window.width() * window.devicePixelRatio() / 2), 30};
        ClientToScreen(handle, &anchor);
        SetCursorPos(anchor.x, anchor.y);
        INPUT press{};
        press.type = INPUT_MOUSE;
        press.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        QCOMPARE(SendInput(1, &press, sizeof(INPUT)), UINT(1));
        QVERIFY(window.startSystemMove());
        QTimer::singleShot(80, [anchor] { SetCursorPos(anchor.x, anchor.y + 180); });
        QTimer::singleShot(160, [anchor] { SetCursorPos(anchor.x + 50, anchor.y + 240); });
        QTimer::singleShot(240, [] {
            INPUT release{};
            release.type = INPUT_MOUSE;
            release.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &release, sizeof(INPUT));
        });
        QTest::qWait(400);
        SetCursorPos(previousCursor.x, previousCursor.y);
        QTRY_COMPARE(window.windowState(), Qt::WindowNoState);
        QCOMPARE(window.size(), normal.size());
        verifyGeometry();
        window.setMinimumHeight(480);
        window.resize(600, 480);
        QTRY_COMPARE(window.height(), 480);
        verifyGeometry();
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
