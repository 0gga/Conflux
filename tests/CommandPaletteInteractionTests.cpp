#include "features/navigation/CommandPalette.h"
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
QVariantMap content(const QString &name, const QString &path) {
    return {{"title", name}, {"relativePath", path}, {"category", "mods"}, {"version", "1.0"}};
}
} // namespace
class CommandPaletteInteractionTests final : public QObject {
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
    void localSearchKeyboardFocusAndContainment_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<int>("scale");
        QTest::newRow("1080p") << 1080 << 720 << 100;
        QTest::newRow("short-150") << 1080 << 480 << 150;
        QTest::newRow("compact") << 420 << 320 << 100;
    }
    void localSearchKeyboardFocusAndContainment() {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(int, scale);
        CommandPalette model;
        const QVariantMap pack{{"path", "original-instance"}, {"name", "Fixture instance"}};
        QVariantList rows{content("Aero", "mods/aero.jar"),
                          content("Aeronautics with a long readable title", "mods/aeronautics.jar")};
        model.setContext(pack, rows, {});
        QVariantList routes;
        model.setNavigator([&](QVariantMap route) {
            routes << route;
            return true;
        });
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(
            R"QML(
import QtQuick
import QtQuick.Controls
import Conflux.Style
import "components"
ApplicationWindow {
 id: fixture
 required property var commands
 property real uiScale: 1
 property int discoverShortcuts: 0
 visible: true; color: Theme.colors.background
 Overlay.overlay.transform: Scale { xScale: fixture.uiScale; yScale: fixture.uiScale }
 Shortcut { sequence:"Ctrl+Shift+P"; onActivated: { if(fixture.commands.visible) fixture.commands.dismiss(); else fixture.commands.open() } }
 Shortcut { sequence:"Ctrl+K"; enabled:!palette.visible; onActivated:fixture.discoverShortcuts++ }
 Item {
  width:fixture.width/fixture.uiScale; height:fixture.height/fixture.uiScale
  scale:fixture.uiScale; transformOrigin:Item.TopLeft
  SearchField { objectName:"originalSearch"; width:180; text:"existing draft" }
  CommandPaletteDialog { id:palette; commands:fixture.commands }
 }
}
)QML",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/command-palette-test.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(
            component.createWithInitialProperties({{"commands", QVariant::fromValue(&model)},
                                                   {"width", width},
                                                   {"height", height},
                                                   {"uiScale", scale / 100.0}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *previous = find(window->contentItem(), "originalSearch");
        QVERIFY(previous);
        previous->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(model.visible());
        auto *dialog = root->findChild<QObject *>("commandPaletteDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *search = find(window->contentItem(), "commandPaletteSearch");
        QVERIFY(search);
        QTRY_VERIFY(search->hasActiveFocus());
        for (const char character : QByteArray("aero"))
            QTest::keyClick(window, character);
        QTRY_COMPARE(model.query(), QString("aero"));
        QTRY_COMPARE(model.rows().size(), 2);
        QSignalSpy frame(window, &QQuickWindow::frameSwapped);
        window->requestUpdate();
        QTRY_VERIFY(!frame.isEmpty());
        auto *entry = find(window->contentItem(), "commandPaletteEntry");
        QVERIFY(entry);
        const QRectF bounds(0, 0, width, height);
        QVERIFY(bounds.contains(search->mapRectToScene(search->boundingRect())));
        QVERIFY(bounds.contains(entry->mapRectToScene(entry->boundingRect())));
        auto *close = find(window->contentItem(), "closeCommandPalette");
        QVERIFY(close);
        QVERIFY(bounds.contains(close->mapRectToScene(close->boundingRect())));
        const auto image = window->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QStringLiteral(CONFLUX_TEST_ARTIFACT_DIR) +
                           QString("/command-palette-%1x%2-%3.png").arg(width).arg(height).arg(scale)));
        QTest::keyClick(window, Qt::Key_Down);
        auto *results = find(window->contentItem(), "commandPaletteResults");
        QVERIFY(results);
        QCOMPARE(results->property("currentIndex").toInt(), 1);
        auto enriched = rows[1].toMap();
        enriched["author"] = "Metadata author";
        rows[1] = enriched;
        model.setContext(pack, rows, {});
        QTRY_VERIFY(model.rows().last().toMap().value("subtitle").toString().contains("Metadata author"));
        QTRY_COMPARE(results->property("currentIndex").toInt(), 1);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(routes.size(), 1);
        QCOMPARE(routes.first().toMap().value("packPath").toString(), QString("original-instance"));
        QCOMPARE(routes.first().toMap().value("modPath").toString(), QString("mods/aeronautics.jar"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        previous->forceActiveFocus();
        model.open();
        QTRY_VERIFY(dialog->property("opened").toBool());
        search = find(window->contentItem(), "commandPaletteSearch");
        QVERIFY(search);
        QCOMPARE(search->property("text").toString(), QString{});
        for (const char character : QByteArray("aero draft"))
            QTest::keyClick(window, character);
        QTRY_COMPARE(model.query(), QString("aero draft"));
        rows << content("Aero draft addition", "mods/addition.jar");
        model.setContext(pack, rows, {});
        QTRY_COMPARE(model.rows().size(), 1);
        QCOMPARE(search->property("text").toString(), QString("aero draft"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "activate", Q_ARG(QVariant, QString("stale-result"))));
        QVERIFY(model.visible());
        QVERIFY(!dialog->property("activating").toBool());
        QCOMPARE(routes.size(), 1);
        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(root->property("discoverShortcuts").toInt(), 0);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!model.visible());
        QTRY_VERIFY(previous->hasActiveFocus());
        QCOMPARE(previous->property("text").toString(), QString("existing draft"));
        QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier);
        QTRY_COMPARE(root->property("discoverShortcuts").toInt(), 1);
        QCOMPARE(routes.size(), 1);
        QStringList messages;
        for (const auto &args : warnings)
            for (const auto &error : args.first().value<QList<QQmlError>>())
                messages << error.toString();
        QVERIFY2(messages.isEmpty(), qPrintable(messages.join('\n')));
    }
};
QTEST_MAIN(CommandPaletteInteractionTests)
#include "CommandPaletteInteractionTests.moc"
