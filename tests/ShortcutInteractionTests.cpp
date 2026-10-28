#include "features/preferences/ShortcutPreferences.h"
#include "platform/BrowseSettingsStore.h"
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRawFont>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)
using namespace studio;
namespace {
QQuickItem *find(QQuickItem *root, const QString &name) {
    if (root->objectName() == name && root->isVisible())
        return root;
    for (auto *child : root->childItems())
        if (auto *item = find(child, name))
            return item;
    return nullptr;
}
} // namespace
class ShortcutInteractionTests final : public QObject {
    Q_OBJECT
    QTemporaryDir style;
  private slots:
    void initTestCase() {
        const auto fonts = qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/";
        for (const auto &file : {"segoeui.ttf", "seguisb.ttf", "segoeuib.ttf", "seguisym.ttf"})
            QVERIFY(QFontDatabase::addApplicationFont(fonts + file) >= 0);
        QGuiApplication::setFont(QFont("Segoe UI"));
        QVERIFY(QRawFont::fromFont(QGuiApplication::font()).supportsCharacter('A'));
        QQuickStyle::setStyle("Basic");
        qputenv("CONFLUX_STYLE_SETTINGS_DIR", style.path().toUtf8());
    }
    void recordingPreservesFocusAndNeverRunsTheCapturedAction_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<int>("scale");
        QTest::newRow("1080p") << 1080 << 720 << 100;
        QTest::newRow("short-150") << 1080 << 480 << 150;
        QTest::newRow("compact") << 420 << 320 << 100;
    }
    void recordingPreservesFocusAndNeverRunsTheCapturedAction() {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(int, scale);
        QTemporaryDir temp;
        const auto settings = temp.path() + "/settings.ini";
        ShortcutPreferences prefs(std::make_shared<BrowseSettingsStore>(settings));
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "components" as Components
import "settings" as Settings
ApplicationWindow {
 id:fixture
 required property var preferences
 property real uiScale:1
 property int paletteActivations:0
 property int discoverActivations:0
 visible:true; color:Theme.colors.background
 Overlay.overlay.transform:Scale{xScale:fixture.uiScale;yScale:fixture.uiScale}
 Shortcut{sequence:fixture.preferences.bindings.palette;enabled:!!fixture.preferences.bindings.palette&&!fixture.preferences.capturing;autoRepeat:false;onActivated:fixture.paletteActivations++}
 Shortcut{sequence:fixture.preferences.bindings.discover;enabled:!!fixture.preferences.bindings.discover&&!fixture.preferences.capturing;autoRepeat:false;onActivated:fixture.discoverActivations++}
 function revealPalette(){scroll.contentItem.contentY=Math.max(0,scroll.contentItem.contentHeight-scroll.availableHeight)}
 function revealTop(){scroll.contentItem.contentY=0}
 Item {
  width:fixture.width/fixture.uiScale;height:fixture.height/fixture.uiScale
  scale:fixture.uiScale;transformOrigin:Item.TopLeft
  ColumnLayout {
   anchors.fill:parent;anchors.margins:12;spacing:10
   Components.SearchField{objectName:"previousDraft";Layout.fillWidth:true;text:"kept draft"}
   Components.BoundedScrollView{id:scroll;Layout.fillWidth:true;Layout.fillHeight:true;clip:true
    Settings.ShortcutSettings{width:scroll.availableWidth;preferences:fixture.preferences}
   }
  }
  Components.ShortcutCaptureDialog{preferences:fixture.preferences}
 }
}
)QML",
                          QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/shortcut-test.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(
            component.createWithInitialProperties({{"preferences", QVariant::fromValue(&prefs)},
                                                   {"width", width},
                                                   {"height", height},
                                                   {"uiScale", scale / 100.0}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto frame = [&] {
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->update();
            return QTest::qWaitFor([&] { return !frames.isEmpty(); }, 2000);
        };
        const auto click = [&](QQuickItem *item) {
            if (!item || !frame())
                return false;
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        QVERIFY(frame());
        QVERIFY(QMetaObject::invokeMethod(root.get(), "revealPalette"));
        QVERIFY(frame());
        auto *button = find(window->contentItem(), "recordShortcut-palette");
        QVERIFY(button);
        const QRectF bounds(0, 0, width, height);
        QVERIFY(bounds.contains(button->mapRectToScene(button->boundingRect())));
        QVERIFY(click(button));
        QTRY_VERIFY(prefs.capturing());
        auto *dialog = root->findChild<QObject *>("shortcutCaptureDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *keys = find(window->contentItem(), "shortcutCaptureKeys");
        QVERIFY(keys);
        QTRY_VERIFY(keys->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY(prefs.error().contains("Explore content"));
        QCOMPARE(root->property("discoverActivations").toInt(), 0);
        QCOMPARE(root->property("paletteActivations").toInt(), 0);
        QVERIFY(prefs.capturing());
        QTest::keyPress(window, Qt::Key_J, Qt::ControlModifier);
        QVERIFY(prefs.capturing());
        QCOMPARE(prefs.bindings().value("palette").toString(), QString("Ctrl+Shift+P"));
        QVERIFY(!prefs.candidate().isEmpty());
        QVERIFY(frame());
        QVERIFY(bounds.contains(keys->mapRectToScene(keys->boundingRect())));
        QVERIFY(window->grabWindow().save(
            QStringLiteral(CONFLUX_TEST_ARTIFACT_DIR) +
            QString("/shortcut-capture-%1x%2-%3.png").arg(width).arg(height).arg(scale)));
        QTest::keyRelease(window, Qt::Key_J, Qt::ControlModifier);
        QTRY_VERIFY(!prefs.capturing());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(prefs.bindings().value("palette").toString(), QString("Ctrl+J"));
        QCOMPARE(root->property("paletteActivations").toInt(), 0);
        QCOMPARE(find(window->contentItem(), "recordShortcut-palette"), button);
        QTRY_VERIFY(button->hasActiveFocus());
        ShortcutPreferences reopened(std::make_shared<BrowseSettingsStore>(settings));
        QCOMPARE(reopened.bindings().value("palette").toString(), QString("Ctrl+J"));
        QTest::keyClick(window, Qt::Key_J, Qt::ControlModifier);
        QTRY_COMPARE(root->property("paletteActivations").toInt(), 1);
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(root->property("paletteActivations").toInt(), 1);
        prefs.beginCapture("palette");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyPress(window, Qt::Key_L, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::keyRelease(window, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(!prefs.capturing());
        QCOMPARE(prefs.bindings().value("palette").toString(), QString("Ctrl+J"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        prefs.beginCapture("palette");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Tab);
        QTest::keyClick(window, Qt::Key_Tab);
        auto *clear = find(window->contentItem(), "clearCapturedShortcut");
        QVERIFY(clear);
        QTRY_VERIFY(clear->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(!prefs.capturing());
        QVERIFY(prefs.bindings().value("palette").toString().isEmpty());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root.get(), "revealTop"));
        QVERIFY(frame());
        QVERIFY(click(find(window->contentItem(), "resetAllShortcuts")));
        QCOMPARE(prefs.bindings().value("palette").toString(), QString("Ctrl+Shift+P"));
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(root->property("paletteActivations").toInt(), 2);
        QCOMPARE(find(window->contentItem(), "previousDraft")->property("text").toString(),
                 QString("kept draft"));
        QStringList messages;
        for (const auto &args : warnings)
            for (const auto &error : args.first().value<QList<QQmlError>>())
                messages << error.toString();
        QVERIFY2(messages.isEmpty(), qPrintable(messages.join('\n')));
    }
};
QTEST_MAIN(ShortcutInteractionTests)
#include "ShortcutInteractionTests.moc"
