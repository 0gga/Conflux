#include "features/content/DependencyRemovals.h"
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
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
        if (auto *result = find(child, name))
            return result;
    return nullptr;
}
QVariantMap row(QString id, QStringList refs = {}) {
    QVariantList deps;
    for (const auto &r : refs)
        deps << QVariantMap{{"id", r}, {"required", true}};
    return {{"title", id},
            {"modId", id},
            {"category", "mods"},
            {"relativePath", "mods/" + id + ".jar"},
            {"sha1", id},
            {"version", "1.0"},
            {"runtimeDependencies", deps}};
}
} // namespace
class DependencyRemovalInteractionTests final : public QObject {
    Q_OBJECT
    QTemporaryDir style;
  private slots:
    void initTestCase() {
        qputenv("CONFLUX_STYLE_SETTINGS_DIR", style.path().toUtf8());
        QQuickStyle::setStyle("Basic");
        const auto fonts = qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/";
        for (const auto &file : {"segoeui.ttf", "seguisb.ttf", "seguisym.ttf"})
            QVERIFY(QFontDatabase::addApplicationFont(fonts + file) >= 0);
        QGuiApplication::setFont(QFont("Segoe UI"));
    }
    void sharedOverlayKeepsControlsContainedAndActionsExplicit_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<int>("scale");
        QTest::newRow("1080p") << 1080 << 720 << 100;
        QTest::newRow("short-150") << 1080 << 480 << 150;
        QTest::newRow("compact") << 420 << 320 << 100;
    }
    void sharedOverlayKeepsControlsContainedAndActionsExplicit() {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(int, scale);
        DependencyRemovals model;
        int writes = 0, acceptedRows = 0;
        model.setRemover([&](auto, auto rows) {
            ++writes;
            acceptedRows = rows.size();
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
ApplicationWindow { id: window; required property var suggestions; property real uiScale: 1; visible: true; color: Theme.colors.background
 Overlay.overlay.transform: Scale { xScale: window.uiScale; yScale: window.uiScale }
 Item { width: window.width/window.uiScale; height: window.height/window.uiScale; scale: window.uiScale; transformOrigin: Item.TopLeft
  DependenciesDialog { objectName: "dependencyRemovalDialog"; suggestions: window.suggestions; removalMode: true }
 }
}
)QML",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/dependency-removal-test.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(
            component.createWithInitialProperties({{"suggestions", QVariant::fromValue(&model)},
                                                   {"width", width},
                                                   {"height", height},
                                                   {"uiScale", scale / 100.0}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto a = row("Root", {"Child", "Shared"}), b = row("Child"), c = row("Shared"),
                   d = row("Other mod", {"Shared"});
        model.observe({{"path", "fixture"}, {"title", "Fixture instance"}}, {a, b, c, d}, {b, c, d},
                      {"mods:mods/Root.jar"}, {}, false);
        auto *dialog = root->findChild<QObject *>("dependencyRemovalDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(dialog->property("modal").toBool());
        QCOMPARE(writes, 0);
        QTRY_VERIFY(find(window->contentItem(), "removeDependency"));
        auto *remove = find(window->contentItem(), "removeDependency");
        auto *done = find(window->contentItem(), "finishOptionalDependencies");
        QVERIFY(remove);
        QVERIFY(done);
        for (auto *item : {remove, done}) {
            const auto bounds = item->mapRectToScene(item->boundingRect());
            QVERIFY(bounds.left() >= 0);
            QVERIFY(bounds.right() <= width + 1);
            QVERIFY(bounds.top() >= 0);
            QVERIFY(bounds.bottom() <= height + 1);
        }
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          remove->mapToScene(QPointF(remove->width() / 2, remove->height() / 2)).toPoint());
        QTRY_COMPARE(writes, 1);
        QCOMPARE(acceptedRows, 1);
        QTRY_VERIFY(!find(window->contentItem(), "removeUnusedDependencies"));
        QVERIFY(window->grabWindow().save(QStringLiteral(CONFLUX_TEST_ARTIFACT_DIR) + "/dependency-removal-" +
                                          QString::number(width) + "x" + QString::number(height) + "-" +
                                          QString::number(scale) + ".png"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!model.visible());
        QCOMPARE(warnings.size(), 0);
    }
};
QTEST_MAIN(DependencyRemovalInteractionTests)
#include "DependencyRemovalInteractionTests.moc"
