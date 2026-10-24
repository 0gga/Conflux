#include "features/content/OptionalDependencies.h"
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
class Source final : public IOptionalDependencySource {
  public:
    int calls = 0;
    QString error;
    void resolve(const QVariantMap &, const QVariantList &, const ContentDependency &reference,
                 const QStringList &, Callback callback) override {
        ++calls;
        Release release;
        release.provider = reference.provider;
        release.projectId = reference.projectId;
        release.id = reference.projectId + "-v1";
        release.version = "1.0.0";
        release.sha1 = QString(40, 'a');
        release.filename = "optional.jar";
        release.url = "https://example.test/file.jar";
        release.gameVersions = {"1.21.1"};
        release.loaders = {"fabric"};
        const auto name = "Optional project with a long readable name " + reference.projectId;
        callback({release, name, {{reference.provider, reference.projectId}}, "mod"}, {{"title", name}},
                 error);
    }
    void cancel() override {}
    void setCurseforgeKey(const QByteArray &) override {}
};
QQuickItem *find(QQuickItem *root, const QString &name) {
    if (root->objectName() == name && root->isVisible())
        return root;
    for (auto *child : root->childItems())
        if (auto *item = find(child, name))
            return item;
    return nullptr;
}
} // namespace
class OptionalDependencyInteractionTests final : public QObject {
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
    void modalActionsFitScaledWindowsAndStayExplicit_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<int>("scale");
        QTest::newRow("1080p-100") << 1080 << 720 << 100;
        QTest::newRow("short-window-150") << 1080 << 480 << 150;
        QTest::newRow("compact-100") << 420 << 320 << 100;
    }
    void modalActionsFitScaledWindowsAndStayExplicit() {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(int, scale);
        QTemporaryDir temp;
        auto store = std::make_shared<BrowseSettingsStore>(temp.path() + "/settings.ini");
        auto preferences = std::make_shared<InstallationPreferences>(store);
        auto policy = std::make_shared<PackReleasePolicy>(store);
        auto source = std::make_shared<Source>();
        OptionalDependencies model(source, preferences, policy);
        QSignalSpy resets(model.model(), &QAbstractItemModel::modelReset);
        int installs = 0;
        model.setInstaller([&](auto, auto) {
            ++installs;
            return true;
        });
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
import QtQuick.Controls
import Conflux.Style
import "components"
ApplicationWindow {
 id: window
 required property var suggestions
 property real uiScale: 1
 visible: true
 color: Theme.colors.background
 Overlay.overlay.transform: Scale { xScale: window.uiScale; yScale: window.uiScale }
 Item {
  width: window.width/window.uiScale; height: window.height/window.uiScale
  scale: window.uiScale; transformOrigin: Item.TopLeft
  OptionalDependenciesDialog { suggestions: window.suggestions }
 }
}
)QML",
                          QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/optional-test.qml"));
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
        const QVariantMap pack{{"path", temp.path() + "/instance"},
                               {"name", "Fixture instance"},
                               {"minecraft", "1.21.1"},
                               {"loader", "fabric"}};
        const QVariantList refs{QVariantMap{{"provider", "modrinth"}, {"projectId", "first"}},
                                QVariantMap{{"provider", "curseforge"}, {"projectId", "22"}}};
        model.offer(pack, {}, refs);
        QTRY_COMPARE(source->calls, 2);
        auto *dialog = root->findChild<QObject *>("optionalDependenciesDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(dialog->property("modal").toBool());
        QCOMPARE(installs, 0);
        auto *install = find(window->contentItem(), "installOptionalDependency");
        auto *done = find(window->contentItem(), "finishOptionalDependencies");
        QVERIFY(install);
        QVERIFY(done);
        const QRectF bounds(0, 0, width, height);
        QVERIFY(bounds.contains(install->mapRectToScene(install->boundingRect())));
        QVERIFY(bounds.contains(done->mapRectToScene(done->boundingRect())));
        window->requestUpdate();
        QTest::qWait(80);
        const auto image = window->grabWindow();
        QVERIFY(!image.isNull());
        const auto file = QStringLiteral(CONFLUX_TEST_ARTIFACT_DIR) +
                          QString("/optional-overlay-%1x%2-%3.png").arg(width).arg(height).arg(scale);
        QVERIFY(image.save(file));
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            install->mapToScene(QPointF(install->width() / 2, install->height() / 2)).toPoint());
        QTRY_COMPARE(installs, 1);
        QCOMPARE(model.rows().first().toMap().value("state").toString(), "queued");
        auto secondPack = pack;
        secondPack["path"] = temp.path() + "/second-instance";
        secondPack["name"] = "Second fixture instance";
        model.offer(secondPack, {}, refs);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          done->mapToScene(QPointF(done->width() / 2, done->height() / 2)).toPoint());
        QTRY_COMPARE(model.pack().value("path"), secondPack.value("path"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!model.visible());
        QTRY_VERIFY(!dialog->property("opened").toBool());
        model.offer(pack, {}, refs);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!model.visible());
        QCOMPARE(installs, 1);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(warnings.size(), 0);
    }
    void settingsToggleIsKeyboardAccessibleAndPersists() {
        QTemporaryDir temp;
        auto store = std::make_shared<BrowseSettingsStore>(temp.path() + "/settings.ini");
        InstallationPreferences preferences(store);
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickWindow window;
        window.resize(420, 220);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/settings/InstallationSettings.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> item(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"preferences", QVariant::fromValue(&preferences)}})));
        QVERIFY2(item, qPrintable(component.errorString()));
        item->setParentItem(window.contentItem());
        item->setWidth(400);
        item->setX(10);
        item->setY(10);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *toggle = find(item.get(), "promptOptionalDependencies");
        QVERIFY(toggle);
        QVERIFY(QRectF(0, 0, 420, 220).contains(toggle->mapRectToScene(toggle->boundingRect())));
        toggle->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Space);
        QTRY_VERIFY(!preferences.promptOptionalDependencies());
        InstallationPreferences reopened(
            std::make_shared<BrowseSettingsStore>(temp.path() + "/settings.ini"));
        QVERIFY(!reopened.promptOptionalDependencies());
        QCOMPARE(warnings.size(), 0);
    }
};
QTEST_MAIN(OptionalDependencyInteractionTests)
#include "OptionalDependencyInteractionTests.moc"
