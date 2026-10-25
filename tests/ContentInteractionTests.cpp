#include "ContentFixtures.h"
#include "TestPerformance.h"
#include "features/config/ConfigLibrary.h"
#include "features/content/ContentActions.h"
#include "features/preferences/BrowsePreferences.h"
#include "features/preferences/ContentPreferences.h"
#include "features/status/RunningInstances.h"
#include "platform/BrowseSettingsStore.h"
#include "platform/ConfigOwnershipStore.h"
#include "theme/Appearance.h"
#include <QBuffer>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)
class ContentInteractionTests : public QObject {
    Q_OBJECT
    QTemporaryDir styleStorage;
    static QQuickItem *findItem(QQuickItem *root, const QString &name) {
        if (root->objectName() == name && root->isVisible())
            return root;
        for (auto *child : root->childItems())
            if (auto *found = findItem(child, name))
                return found;
        return nullptr;
    }
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        qputenv("CONFLUX_STYLE_SETTINGS_DIR", styleStorage.path().toUtf8());
    }
    void individualAuthorLinksHaveIndependentHoverAndClickTargets_data() {
        QTest::addColumn<int>("scale");
        for (int scale : {75, 100, 150})
            QTest::newRow(qPrintable(QString::number(scale))) << scale;
    }
    void individualAuthorLinksHaveIndependentHoverAndClickTargets() {
        QFETCH(int, scale);
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(700, 170);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/AuthorLinks.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const QVariantList authors{
            QVariantMap{{"name", "First_Author"}, {"ids", QVariantMap{{"modrinth", "mr-first"}}}},
            QVariantMap{{"name", "Second, Author"}, {"ids", QVariantMap{{"curseforge", "123"}}}},
            QVariantMap{{"name", "Unlinked"}, {"ids", QVariantMap{}}}};
        std::unique_ptr<QQuickItem> links(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"authors", authors}, {"prefix", "by "}})));
        QVERIFY2(links, qPrintable(component.errorString()));
        links->setParentItem(window.contentItem());
        links->setX(10);
        links->setY(30);
        links->setWidth(650);
        QFont font;
        font.setPixelSize(12 * scale / 100);
        links->setProperty("font", font);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCoreApplication::processEvents();
        QList<QQuickItem *> names;
        std::function<void(QQuickItem *)> collect = [&](QQuickItem *item) {
            if (item->objectName() == "authorNameLink")
                names << item;
            for (auto *child : item->childItems())
                collect(child);
        };
        collect(links.get());
        QCOMPARE(names.size(), 3);
        QVERIFY(names[0]->isEnabled());
        QVERIFY(names[1]->isEnabled());
        QVERIFY(!names[2]->isEnabled());
        auto center = [](QQuickItem *item) {
            return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        };
        auto underlined = [](QQuickItem *item) { return item->property("font").value<QFont>().underline(); };
        QTest::mouseMove(&window, center(names[0]));
        QTRY_VERIFY(underlined(names[0]));
        QVERIFY(!underlined(names[1]));
        QTest::mouseMove(&window, center(names[1]));
        QTRY_VERIFY(underlined(names[1]));
        QVERIFY(!underlined(names[0]));
        const auto payload = [](const QVariant &value) {
            return value.metaType() == QMetaType::fromType<QJSValue>()
                       ? value.value<QJSValue>().toVariant().toMap()
                       : value.toMap();
        };
        QSignalSpy requested(links.get(), SIGNAL(authorRequested(QVariant)));
        QVERIFY(requested.isValid());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(names[1]));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(payload(requested[0][0]).value("ids").toMap(), QVariantMap({{"curseforge", "123"}}));
        QCOMPARE(payload(requested[0][0]).value("name").toString(), QString("Second, Author"));
        names[0]->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(underlined(names[0]));
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(requested.size(), 2);
        QCOMPARE(payload(requested[1][0]).value("ids").toMap(), QVariantMap({{"modrinth", "mr-first"}}));
        links->setWidth(45);
        QCoreApplication::processEvents();
        QVERIFY(links->clip());
        for (auto *name : names)
            if (name->width() > 0) {
                const auto origin = name->mapToItem(links.get(), QPointF());
                QVERIFY(origin.x() + name->width() <= links->width() + 0.5);
            }
        QVERIFY(!names[1]->isEnabled());
        names[0]->setFocus(false);
        QTest::mouseMove(&window, QPoint(690, 150));
        QTRY_VERIFY(!underlined(names[0]));
        links->setWidth(650);
        QCoreApplication::processEvents();
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/separate-authors-" + QString::number(scale) +
                                         ".png"));
    }
    void brandAndNavigationIconsRemainCenteredAtEveryScale() {
        QQmlEngine engine;
        QQuickWindow window;
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import "components"
Item { width:400; height:200
    BrandBadge { id:badge; objectName:"badge"; brand:"neoforge"; iconOnly:true; x:20; y:20 }
    NavigationButton { objectName:"navigation"; x:100; y:20; width:56; iconName:"settings"; text:"" }
})",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/BadgeCenterFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> fixture(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(fixture);
        fixture->setParentItem(window.contentItem());
        window.resize(400, 200);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *badge = findItem(fixture.get(), "badge"), *navigation = findItem(fixture.get(), "navigation");
        QVERIFY(badge);
        QVERIFY(navigation);
        for (const auto &brand :
             {QString("neoforge"), QString("fabric"), QString("modrinth"), QString("forge")}) {
            badge->setProperty("brand", brand);
            for (const double scale : {0.75, 1.0, 1.5}) {
                badge->setProperty("badgeScale", scale);
                QTest::qWait(20);
                auto *art = findItem(badge, "brandBadgeImage");
                if (!art)
                    art = findItem(badge, "brandBadgeGlyph");
                QVERIFY(art);
                const auto center = art->mapToItem(badge, QPointF(art->width() / 2, art->height() / 2));
                QVERIFY2(qAbs(center.x() - badge->width() / 2) <= 0.51,
                         qPrintable(QString("%1 scale%2 center%3 width%4 art%5 parent%6 grand%7")
                                        .arg(brand)
                                        .arg(scale)
                                        .arg(center.x())
                                        .arg(badge->width())
                                        .arg(art->width())
                                        .arg(art->parentItem()->x())
                                        .arg(art->parentItem()->parentItem()->x())));
                QVERIFY2(qAbs(center.y() - badge->height() / 2) <= 0.51,
                         qPrintable(QString("%1 scale%2 center%3,%4 badge%5,%6 art%7,%8 parent%9,%10")
                                        .arg(brand)
                                        .arg(scale)
                                        .arg(center.x())
                                        .arg(center.y())
                                        .arg(badge->width())
                                        .arg(badge->height())
                                        .arg(art->y())
                                        .arg(art->height())
                                        .arg(art->parentItem()->y())
                                        .arg(art->parentItem()->height())));
            }
        }
        auto *body = findItem(navigation, "actionContentRow");
        QVERIFY(body);
        const auto center = body->mapToItem(navigation, QPointF(body->width() / 2, body->height() / 2));
        QVERIFY(qAbs(center.x() - navigation->width() / 2) <= 0.51);
        QVERIFY(qAbs(center.y() - navigation->height() / 2) <= 0.51);
    }

    void scrollbarsOnlyAppearWhenNeededWithoutChangingTheGutter() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(420, 240);
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import "components"
ContentListView {width:400;height:200;model:2;delegate:Rectangle {width:ListView.view.width;height:40}}
)",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/FittingScrollbarFixture.qml"));
        std::unique_ptr<QQuickItem> list(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(list, qPrintable(component.errorString()));
        list->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *bar = list->findChild<QQuickItem *>("contentScrollBar");
        QVERIFY(bar);
        QTRY_VERIFY(!bar->isVisible());
        const auto width = list->property("viewportWidth").toReal();
        list->setProperty("model", 30);
        QTRY_VERIFY(bar->isVisible());
        QCOMPARE(list->property("viewportWidth").toReal(), width);
        list->setProperty("model", 2);
        QTRY_VERIFY(!bar->isVisible());
        QCOMPARE(list->property("viewportWidth").toReal(), width);
    }
    void constrainedButtonsNoticesAndSingleLineStatisticsStayContained() {
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickWindow window;
        window.resize(520, 320);
        window.setColor(QColor("#08090a"));
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import "components"
Item { width:520;height:320
 ActionButton {objectName:"boundedButton";x:16;y:16;width:130;text:"An exceptionally long action label";iconName:"download"}
 ActionButton {objectName:"smallClearButton";x:170;y:16;width:24;height:24;iconName:"close"}
 NotificationBanner {objectName:"boundedNotice";x:16;y:72;width:488;text:"A required release is incompatible with Minecraft 1.21.1 / neoforge. No changes were made. A long message remains contained even when it needs another line."}
 Item {x:250;y:260;width:28;height:20;RoundedToolTip {objectName:"statisticTip";text:"281 followers";showWhen:true;delay:0}}
})",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/BoundedControlsFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> item(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(item, qPrintable(component.errorString()));
        item->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(80);
        auto *button = findItem(item.get(), "boundedButton");
        QVERIFY(button);
        auto *row = findItem(button, "actionContentRow");
        QVERIFY(row);
        QVERIFY(row->width() <= button->width());
        auto *small = findItem(item.get(), "smallClearButton");
        QVERIFY(small);
        auto *smallRow = findItem(small, "actionContentRow");
        QVERIFY(smallRow);
        const auto center =
            smallRow->mapToItem(small, QPointF(smallRow->width() / 2, smallRow->height() / 2));
        QVERIFY(qAbs(center.x() - small->width() / 2) < 0.1);
        QVERIFY(!smallRow->parentItem()->clip());
        auto *notice = findItem(item.get(), "boundedNotice");
        auto *dismiss = findItem(notice, "dismissNoticeButton");
        QVERIFY(dismiss);
        const auto rect = dismiss->mapRectToItem(notice, dismiss->boundingRect());
        QVERIFY(rect.left() >= 0);
        QVERIFY(rect.right() <= notice->width());
        QVERIFY(rect.bottom() <= notice->height());
        auto *tip = item->findChild<QObject *>("statisticTip");
        QVERIFY(tip);
        auto *label = tip->property("contentItem").value<QQuickItem *>();
        QVERIFY(label);
        QTRY_COMPARE(label->property("lineCount").toInt(), 1);
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/polish-controls-narrow.png"));
        QCOMPARE(warnings.count(), 0);
    }
    void operationHudShowsFailuresCopiesDetailsAndRetriesWithoutBlockingThePage() {
        QTemporaryDir temp;
        auto installer = std::make_shared<ContentInstallerFixture>();
        studio::ContentActions content(
            std::make_shared<ContentFileFixture>(), std::make_shared<ContentCatalogueFixture>(), installer,
            std::make_shared<studio::SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        const QVariantMap release{{"provider", "modrinth"}, {"projectId", "fixture"},   {"id", "new"},
                                  {"filename", "new.zip"},  {"sha1", QString(40, 'b')}, {"version", "2"}};
        const QVariantMap row{{"relativePath", "resourcepacks/old.zip"},
                              {"title", "Trident"},
                              {"category", "resourcepacks"},
                              {"enabled", true},
                              {"sha1", QString(40, 'a')},
                              {"sourceIds", QVariantMap{{"modrinth", "fixture"}}},
                              {"updateAvailable", true},
                              {"updateRelease", release}};
        content.setContext({{"path", temp.path()}, {"title", "Create: Takeoff"}}, {row});
        QTRY_VERIFY(!content.busy());
        class Source : public studio::IInstanceProcessSource {
          public:
            studio::RunningInstanceResult scan(const QVariantList &packs) const override {
                return {packs, {}};
            }
        };
        studio::RunningInstances running(std::make_shared<Source>());
        running.setPacks({QVariantMap{{"path", temp.path()}, {"title", "Create: Takeoff"}}});
        QTRY_COMPARE(running.instances().size(), 1);
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickWindow window;
        window.resize(1040, 620);
        window.setColor(QColor("#08090a"));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/OperationHud.qml"));
        std::unique_ptr<QQuickItem> hud(qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"content", QVariant::fromValue(&content)}, {"running", QVariant::fromValue(&running)}})));
        QVERIFY2(hud, qPrintable(component.errorString()));
        hud->setParentItem(window.contentItem());
        hud->setPosition({600, 20});
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        content.update("resourcepacks/old.zip");
        QCOMPARE(content.operations()->activeCount(), 1);
        installer->completion({false, "Fixture write failed; original content was preserved."});
        QTRY_VERIFY(!content.busy());
        auto click = [&](const QString &name) {
            auto *button = findItem(window.contentItem(), name);
            QVERIFY(button);
            QTest::mouseClick(
                &window, Qt::LeftButton, Qt::NoModifier,
                button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        };
        click("operationHudButton");
        QTest::qWait(100);
        auto *popup = hud->findChild<QObject *>("operationTasksPopup");
        QVERIFY(popup);
        QVERIFY(popup->property("activeFocus").toBool());
        QSignalSpy copied(&content, &studio::ContentActions::copyRequested);
        click("copyOperationDetails");
        QCOMPARE(copied.count(), 1);
        QVERIFY(copied.first().first().toString().contains("Fixture write failed"));
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/atomic-task-failure.png"));
        click("retryOperation");
        QCOMPARE(content.operations()->activeCount(), 1);
        content.operations()->queued(1000, {}, "Earlier failure", "Update");
        content.operations()->finish(1000, false, "A previous task failed");
        content.operations()->queued(1001, {}, "Next content", "Update");
        QCOMPARE(content.operations()->entries().first().toMap().value("state").toString(),
                 QString("running"));
        QTest::qWait(100);
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/active-task-overview.png"));
        click("clearFinishedOperations");
        QCOMPARE(content.operations()->failedCount(), 0);
        QCOMPARE(content.operations()->activeCount(), 2);
        QCOMPARE(content.operations()->rowCount(), 2);
        installer->completion({true, {}});
        QTRY_VERIFY(!content.busy());
        click("clearFinishedOperations");
        QCOMPARE(content.operations()->rowCount(), 1);
        content.operations()->finish(1001, true, {});
        click("clearFinishedOperations");
        QCOMPARE(content.operations()->rowCount(), 0);
        QMetaObject::invokeMethod(popup, "close");
        QTest::qWait(50);
        click("runningInstancesButton");
        QTest::qWait(100);
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/atomic-running-instances.png"));
        QCOMPARE(warnings.count(), 0);
    }
    void customWindowControlsRespectCloseGuardAndRestoreState() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import QtQuick.Controls
import "components"
ApplicationWindow { id:w; width:600; height:300; visible:true; flags:Qt.Window|Qt.CustomizeWindowHint|Qt.WindowTitleHint|Qt.WindowMinimizeButtonHint|Qt.WindowMaximizeButtonHint|Qt.WindowCloseButtonHint
    property int closeAttempts:0
    onClosing:function(event) { closeAttempts++;event.accepted=false }
    WindowResizeEdges { objectName:"resizeEdges"; anchors.fill:parent;targetWindow:w;z:2 }
    WindowDragArea { objectName:"drag"; width:200; height:48; targetWindow:w }
    WindowControls { anchors.top:parent.top; anchors.right:parent.right; targetWindow:w }
})",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/WindowControlsFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        auto click = [&](const QString &name) {
            auto *button = findItem(window->contentItem(), name);
            QVERIFY(button);
            QTest::mouseClick(
                window.get(), Qt::LeftButton, Qt::NoModifier,
                button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        };
        click("windowMaximize");
        QTRY_COMPARE(window->visibility(), QWindow::Maximized);
        click("windowMaximize");
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        auto *drag = findItem(window->contentItem(), "drag");
        QVERIFY(drag);
        QTest::mouseDClick(window.get(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 20));
        QTRY_COMPARE(window->visibility(), QWindow::Maximized);
        click("windowMaximize");
        QTRY_COMPARE(window->visibility(), QWindow::Windowed);
        click("windowClose");
        QTRY_COMPARE(window->property("closeAttempts").toInt(), 1);
        QVERIFY(window->isVisible());
    }
    void discoveryPageFieldAcceptsTypingAndPageShortcutsKeepTheirGeometry() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import "explore"
Item { id:fixture; width:600; height:80
    property int requestedPage:1
    QtObject { id:catalogue; property int page:1; property int pageCount:1076; property bool busy:false; property bool hasNext:true
        function goToPage(value) { page=value; fixture.requestedPage=value }
        function next() { goToPage(page+1) }
        function previous() { goToPage(page-1) }
    }
    PageNavigation { catalogue:catalogue }
})",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/PageNavigationFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(root);
        QQuickWindow window;
        window.resize(600, 100);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(60);
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/discovery-page-first.png"));
        int secondPageButtons = 0;
        QList<QQuickItem *> pending{root.get()};
        while (!pending.isEmpty()) {
            auto *item = pending.takeLast();
            if (item->objectName() == "browsePageShortcut" && item->isVisible() &&
                item->property("text").toString() == "2")
                ++secondPageButtons;
            pending.append(item->childItems());
        }
        QCOMPARE(secondPageButtons, 1);
        auto *field = findItem(root.get(), "browsePageNumber");
        QVERIFY(field);
        const auto size = field->size();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          field->mapToScene(QPointF(field->width() / 2, 18)).toPoint());
        QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_5);
        QTest::keyClick(&window, Qt::Key_2);
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_COMPARE(root->property("requestedPage").toInt(), 52);
        field = findItem(root.get(), "browsePageNumber");
        QVERIFY(field);
        QVERIFY(field->width() >= size.width());
        QTest::qWait(50); // Let the layout place the newly visible previous-page button.
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/discovery-page-entered.png"));
        auto *previous = findItem(root.get(), "browsePreviousPage");
        QVERIFY(previous);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          previous->mapToScene(QPointF(18, 18)).toPoint());
        QTRY_COMPARE(root->property("requestedPage").toInt(), 51);
    }
    void wheelScrollingAnimatesAccumulatesAndStaysWithinBounds() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(340, 280);
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import "components"
ContentListView { width:320; height:240; model:100
    delegate: Rectangle { width:ListView.view.width; height:40 }
})",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/ScrollMotionFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> list(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(list);
        list->setParentItem(window.contentItem());
        auto *appearance =
            engine.singletonInstance<studio::Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"));
        QVERIFY(appearance);
        QVERIFY(appearance->setValue("scrollDistance", 200));
        QVERIFY(appearance->setValue("scrollDuration", 200));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto wheel = [&](int angle, int pixels = 0) {
            const QPointF position(100, 100);
            QWheelEvent event(position, window.mapToGlobal(position.toPoint()), QPoint(0, pixels),
                              QPoint(0, angle), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&window, &event);
        };
        QSignalSpy settled(list.get(), SIGNAL(movementEnded()));
        wheel(-120);
        QTest::qWait(40);
        QVERIFY(list->property("contentY").toReal() > 0);
        QVERIFY(list->property("contentY").toReal() < 200);
        QVERIFY(list->property("moving").toBool());
        wheel(-120);
        wheel(-120);
        QTRY_VERIFY_WITH_TIMEOUT(!list->property("moving").toBool(), 1000);
        QVERIFY(qAbs(list->property("contentY").toReal() - 600) < 1);
        QVERIFY(settled.count() > 0);
        wheel(120);
        QTRY_VERIFY(!list->property("moving").toBool());
        QVERIFY(qAbs(list->property("contentY").toReal() - 400) < 1);
        wheel(0, -25);
        QVERIFY(qAbs(list->property("contentY").toReal() - 425) < 1);
        QVERIFY(!list->property("moving").toBool());
        QVERIFY(appearance->setValue("reduceMotion", 1));
        wheel(-120);
        QVERIFY(qAbs(list->property("contentY").toReal() - 625) < 1);
        list->setProperty("contentY", 0);
        wheel(120);
        QCOMPARE(list->property("contentY").toReal(), 0.0);
        list->setProperty("contentY", list->property("contentHeight").toReal() - list->height());
        wheel(-120);
        QCOMPARE(list->property("contentY").toReal(),
                 list->property("contentHeight").toReal() - list->height());
        appearance->reset();
    }
    void nestedScrollAndModalFocusDoNotMoveTheBackground() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(400, 360);
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import QtQuick.Controls
import "components"
Item { width:400;height:360
    BoundedScrollView { id:outer;objectName:"outerScroll";width:360;height:300
        Item { width:outer.availableWidth;implicitHeight:2000
            ContentListView { objectName:"innerScroll";width:300;height:160;model:50;delegate:Rectangle{width:ListView.view.width;height:40} }
        }
    }
    FocusPopup { objectName:"motionPopup";x:100;y:100;width:200;height:120;contentItem:TextField{focus:true} }
})",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/NestedMotionFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> fixture(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(fixture);
        fixture->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *inner = findItem(fixture.get(), "innerScroll");
        auto *outer = findItem(fixture.get(), "outerScroll");
        QVERIFY(inner && outer);
        auto *viewport = outer->property("viewport").value<QQuickItem *>();
        QVERIFY(viewport);
        auto wheel = [&] {
            const QPointF position(80, 80);
            QWheelEvent event(position, window.mapToGlobal(position.toPoint()), QPoint{}, QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&window, &event);
        };
        wheel();
        QTest::qWait(40);
        QVERIFY(inner->property("contentY").toReal() > 0);
        QCOMPARE(viewport->property("contentY").toReal(), 0.0);
        auto *popup = fixture->findChild<QObject *>("motionPopup");
        QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTest::qWait(40);
        const auto position = inner->property("contentY").toReal();
        wheel();
        QTest::qWait(250);
        QCOMPARE(inner->property("contentY").toReal(), position);
        QCOMPARE(viewport->property("contentY").toReal(), 0.0);
    }
    void galleryTabOnlyAppearsWithImagesAndEmptySelectionFallsBack() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1000, 700);
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import "pages"
Item { id:root;width:1000;height:700
    property alias gallery:model.gallery
    property alias selection:navigation.tab
    property alias busy:model.busy
    property alias releaseRows:filters.rows
    property string selectedVersion:model.changelogRelease.id || ""
    QtObject { id:navigation;property string tab:"gallery" }
    QtObject { id:model;property var project:({title:"Example",sources:[]});property var gallery:[];property bool busy:false;property string notice:"";property var descriptionBlocks:[];property var changelogVersions:[];property var changelogBlocks:[];property var changelogRelease:({});property string changelogNotice:"";property bool changelogBusy:false;function selectChangelog(release){changelogRelease=release} signal changed();onGalleryChanged:changed();onBusyChanged:changed() }
    QtObject { id:browser;property bool ready:false;property bool busy:false;property string notice:"";function installed(release,type){return false} }
    QtObject { id:applicationModel;property bool repositoryBusy:false;property bool navigationBusy:false;function openWeb(url){} }
    QtObject { id:content;property string notice:"";property int lockRevision:0;function projectLocked(project){return false} function sourceLock(project){return ({})} }
    QtObject { id:filters;property var rows:[];property var options:({channels:[],versions:[],loaders:[]});property var filters:({channels:[],versions:[],loaders:[],source:"all"}) }
    ShowcasePage { id:page;objectName:"galleryPage";anchors.fill:parent;showcase:model;browse:browser;app:applicationModel;contentController:content;releaseFilters:filters;tab:navigation.tab;onTabRequested:function(key){navigation.tab=key} }
})",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/EmptyGalleryFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> fixture(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(fixture, qPrintable(component.errorString()));
        fixture->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *page = findItem(fixture.get(), "galleryPage");
        QVERIFY(page);
        QCOMPARE(page->property("displayedTab").toString(), QString("description"));
        QTRY_COMPARE(fixture->property("selection").toString(), QString("description"));
        QVERIFY(!findItem(fixture.get(), "sectionTab_gallery"));
        fixture->setProperty("selection", "versions");
        QTRY_VERIFY(findItem(fixture.get(), "versionChannelFilter"));
        const QVariantMap release{{"id", "exact-release"},
                                  {"provider", "modrinth"},
                                  {"version", "1.2"},
                                  {"gameVersions", QStringList{"1.21.1"}},
                                  {"loaders", QStringList{"fabric"}}};
        fixture->setProperty(
            "releaseRows", QVariantList{QVariantMap{{"label", "1.2"}, {"variants", QVariantList{release}}}});
        QTRY_VERIFY(findItem(fixture.get(), "releaseChangelogLink"));
        auto *link = findItem(fixture.get(), "releaseChangelogLink");
        // Asynchronous tab incubation can expose objects before their first layout/render pass.
        QSignalSpy rendered(&window, &QQuickWindow::frameSwapped);
        window.update();
        QVERIFY(rendered.wait(1000));
        QVERIFY(link->width() > 0 && link->height() > 0);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          link->mapToScene(QPointF(link->width() / 2, link->height() / 2)).toPoint());
        QTRY_COMPARE(fixture->property("selection").toString(), QString("changelog"));
        QCOMPARE(fixture->property("selectedVersion").toString(), QString("exact-release"));

        QTemporaryDir temp;
        QImage picture(30, 30, QImage::Format_ARGB32);
        picture.fill(Qt::red);
        QVERIFY(picture.save(temp.path() + "/image.png"));
        fixture->setProperty("gallery",
                             QStringList{QUrl::fromLocalFile(temp.path() + "/image.png").toString()});
        QTRY_VERIFY(findItem(fixture.get(), "sectionTab_gallery"));
        fixture->setProperty("selection", "gallery");
        QCOMPARE(page->property("displayedTab").toString(), QString("gallery"));
        fixture->setProperty("gallery", QStringList{});
        QTRY_COMPARE(page->property("displayedTab").toString(), QString("description"));
        QVERIFY(!findItem(fixture.get(), "sectionTab_gallery"));
    }
    void sharedSelectBoxKeepsKeyboardSelectionWithGutter() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(400, 250);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/SelectBox.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> box(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"model", QStringList{"First", "Second", "Third"}}})));
        QVERIFY(box);
        box->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          QPoint(box->width() / 2, box->height() / 2));
        auto *popup = box->property("popup").value<QObject *>();
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::keyClick(&window, Qt::Key_Down);
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_COMPARE(box->property("currentIndex").toInt(), 1);
        QSignalSpy activated(box.get(), SIGNAL(activated(int)));
        QVERIFY(activated.isValid());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          QPoint(box->width() / 2, box->height() / 2));
        QTRY_VERIFY(findItem(window.contentItem(), "selectBoxOption-2"));
        auto *option = findItem(window.contentItem(), "selectBoxOption-2");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          option->mapToScene(QPointF(option->width() / 2, option->height() / 2)).toPoint());
        QTRY_COMPARE(box->property("currentIndex").toInt(), 2);
        QCOMPARE(activated.count(), 1);
    }
    void searchClearKeepsFocusAndNotifiesSearchBindings() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(400, 100);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/SearchField.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> field(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"text", "needle"}, {"leadingIcon", "search"}})));
        QVERIFY(field);
        field->setParentItem(window.contentItem());
        field->setWidth(350);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QSignalSpy edited(field.get(), SIGNAL(textEdited()));
        QVERIFY(edited.isValid());
        auto *clear = findItem(window.contentItem(), "clearSearchButton");
        QVERIFY(clear);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          clear->mapToScene(QPointF(clear->width() / 2, clear->height() / 2)).toPoint());
        QTRY_VERIFY(field->property("text").toString().isEmpty());
        QCOMPARE(edited.count(), 1);
        QVERIFY(field->hasActiveFocus());
        QVERIFY(!clear->isVisible());
    }
    void filterChipHoverAndPressKeepOneStablePillSilhouette() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(250, 100);
        window.setColor(Qt::green);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/FilterChip.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> chip(
            qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"text", "NeoForge"}})));
        QVERIFY(chip);
        chip->setParentItem(window.contentItem());
        chip->setX(20);
        chip->setY(20);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *remove = findItem(window.contentItem(), "filterChipRemove");
        QVERIFY(remove);
        const auto size = chip->size();
        const auto radius = chip->property("radius");
        auto *background = remove->property("background").value<QQuickItem *>();
        QVERIFY(background);
        QCOMPARE(background->property("radius").toReal(), remove->height() / 2);
        QTest::mouseMove(&window, QPoint(220, 80));
        QTest::qWait(120);
        const auto before = window.grabWindow();
        const auto point = remove->mapToScene(QPointF(remove->width() / 2, remove->height() / 2)).toPoint();
        QTest::mouseMove(&window, point);
        QTest::qWait(120);
        QCOMPARE(chip->size(), size);
        QCOMPARE(chip->property("radius"), radius);
        const auto after = window.grabWindow();
        for (const QPoint corner : {QPoint(20, 20), QPoint(20 + qRound(chip->width()) - 1, 20),
                                    QPoint(20 + qRound(chip->width()) - 1, 47)})
            QCOMPARE(after.pixelColor(corner), before.pixelColor(corner));
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(chip->size(), size);
        QCOMPARE(chip->property("radius"), radius);
        QCOMPARE(background->property("radius").toReal(), remove->height() / 2);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, point);
    }
    void descriptionViewLoadsNestedTableBlocks() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(600, 400);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/showcase/DescriptionView.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const QVariantList text{
            QVariantMap{{"kind", "text"}, {"html", "<p><strong>Readable table cell</strong></p>"}}};
        const QVariantList cells{QVariantMap{{"blocks", text}}};
        const QVariantList rows{QVariantMap{{"cells", cells}}};
        const QVariantList blocks{QVariantMap{{"kind", "table"}, {"rows", rows}}};
        std::unique_ptr<QQuickItem> view(
            qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"blocks", blocks}})));
        QVERIFY2(view, qPrintable(component.errorString()));
        view->setParentItem(window.contentItem());
        view->setWidth(550);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(findItem(window.contentItem(), "descriptionTextBlock"));
        QVERIFY(findItem(window.contentItem(), "descriptionTextBlock")
                    ->property("text")
                    .toString()
                    .contains("Readable table cell"));
        QVERIFY(view->implicitHeight() > 0);
    }
    void galleryViewerSupportsZoomNavigationAndEscape() {
        QTemporaryDir dir;
        QImage image(300, 180, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(dir.path() + "/first.png"));
        image.fill(Qt::red);
        QVERIFY(image.save(dir.path() + "/second.png"));
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(900, 600);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/showcase/GalleryViewer.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> viewer(component.createWithInitialProperties(
            {{"parent", QVariant::fromValue(window.contentItem())},
             {"images", QStringList{QUrl::fromLocalFile(dir.path() + "/first.png").toString(),
                                    QUrl::fromLocalFile(dir.path() + "/second.png").toString()}}}));
        QVERIFY2(viewer, qPrintable(component.errorString()));
        QVERIFY(QMetaObject::invokeMethod(viewer.get(), "open"));
        QTRY_VERIFY(viewer->property("activeFocus").toBool());
        const auto click = [&window](QQuickItem *item) {
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                              item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        QTRY_VERIFY(findItem(window.contentItem(), "galleryZoomIn"));
        click(findItem(window.contentItem(), "galleryZoomIn"));
        QTRY_VERIFY(viewer->property("zoom").toReal() > 1);
        click(findItem(window.contentItem(), "galleryNext"));
        QTRY_COMPARE(viewer->property("currentIndex").toInt(), 1);
        QCOMPARE(viewer->property("zoom").toReal(), 1);
        QTest::keyClick(&window, Qt::Key_Left);
        QTRY_COMPARE(viewer->property("currentIndex").toInt(), 0);
        auto *picture = findItem(window.contentItem(), "galleryEnlargedImage");
        auto *pan = findItem(window.contentItem(), "galleryEnlargedViewport");
        QVERIFY(picture && pan);
        QTRY_COMPARE(picture->property("status").toInt(), 1);
        QVERIFY(qAbs(qMax(picture->width() / pan->width(), picture->height() / pan->height()) - 0.83) < 0.01);
        click(picture);
        QTRY_VERIFY(viewer->property("visible").toBool());
        // Letterboxed space inside the viewport dismisses; the image and toolbar do not.
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          pan->mapToScene(QPointF(2, pan->height() / 2)).toPoint());
        QTRY_VERIFY(!viewer->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(viewer.get(), "open"));
        QTRY_VERIFY(viewer->property("visible").toBool());
        click(findItem(window.contentItem(), "galleryZoomIn"));
        QTRY_VERIFY(viewer->property("visible").toBool());
        viewer->setProperty("zoom", 2.0);
        const auto start = pan->mapToScene(QPointF(pan->width() / 2, pan->height() / 2)).toPoint();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&window, start - QPoint(40, 0), 30);
        QTest::mouseMove(&window, start - QPoint(90, 0), 30);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, start - QPoint(90, 0));
        QTRY_VERIFY(viewer->property("visible").toBool());
        QTRY_VERIFY(pan->property("contentX").toReal() > 0);
        // Even after panning, the outer padding is backdrop rather than clipped image geometry.
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QTRY_VERIFY(!viewer->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(viewer.get(), "open"));
        QTRY_VERIFY(viewer->property("visible").toBool());
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY(!viewer->property("visible").toBool());
    }
    void galleryBackdropWorksInScaledApplicationWindow_data() {
        QTest::addColumn<int>("scale");
        QTest::newRow("normal") << 100;
        QTest::newRow("scaled") << 150;
    }
    void galleryBackdropWorksInScaledApplicationWindow() {
        QFETCH(int, scale);
        QTemporaryDir dir;
        QImage image(300, 180, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(dir.path() + "/image.png"));
        QQmlEngine engine;
        auto *appearance =
            engine.singletonInstance<studio::Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"));
        QVERIFY(appearance);
        QVERIFY(appearance->setValue("uiScale", scale));
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import QtQuick.Controls
import Conflux.Style
import "showcase"
ApplicationWindow {
    id: fixture; width: 900; height: 600; visible: true
    required property string sampleImage
    property int backgroundClicks: 0
    Overlay.overlay.transform: Scale { xScale: Appearance.values.uiScale/100; yScale: xScale }
    MouseArea { anchors.fill: parent; onClicked: ++fixture.backgroundClicks }
    Item { width: fixture.width/(Appearance.values.uiScale/100); height: fixture.height/(Appearance.values.uiScale/100); scale: Appearance.values.uiScale/100; transformOrigin: Item.TopLeft; GalleryViewer { images: [fixture.sampleImage] } }
})",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/GalleryWindowFixture.qml"));
        std::unique_ptr<QQuickWindow> window(
            qobject_cast<QQuickWindow *>(component.createWithInitialProperties(
                {{"sampleImage", QUrl::fromLocalFile(dir.path() + "/image.png").toString()}})));
        QVERIFY2(window, qPrintable(component.errorString()));
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        auto *viewer = window->findChild<QObject *>("galleryViewer");
        QVERIFY(viewer);
        QVERIFY(QMetaObject::invokeMethod(viewer, "open"));
        QTRY_VERIFY(viewer->property("activeFocus").toBool());
        auto *picture = findItem(window->contentItem(), "galleryEnlargedImage");
        QVERIFY(picture);
        QTRY_COMPARE(picture->property("status").toInt(), 1);
        QTest::mouseClick(
            window.get(), Qt::LeftButton, Qt::NoModifier,
            picture->mapToScene(QPointF(picture->width() / 2, picture->height() / 2)).toPoint());
        QTRY_VERIFY(viewer->property("visible").toBool());
        auto *backdrop = findItem(window->contentItem(), "galleryBackdrop");
        QVERIFY(backdrop);
        QTest::mouseClick(window.get(), Qt::LeftButton, Qt::NoModifier,
                          backdrop->mapToScene(QPointF(5, 5)).toPoint());
        QTRY_VERIFY(!viewer->property("visible").toBool());
        QCOMPARE(window->property("backgroundClicks").toInt(), 0);
        appearance->reset();
    }
    void cardHoverRemainsActiveAcrossDescendantTagsAndStatistics() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1000, 240);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/ProjectCard.qml"));
        std::unique_ptr<QQuickItem> card(qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"project", QVariantMap{{"title", "Example"},
                                     {"author", "Author"},
                                     {"summary", "Summary"},
                                     {"provider", "modrinth"},
                                     {"sources", QStringList{"modrinth"}},
                                     {"loaders", QStringList{"neoforge"}},
                                     {"categories", QStringList{"optimization"}},
                                     {"downloadStats", QVariantList{QVariantMap{{"provider", "modrinth"},
                                                                                {"count", "123K"}}}}}},
             {"minecraft", "1.21.1"},
             {"installState", QVariantMap{{"canInstall", true}, {"label", "Install"}}}})));
        QVERIFY2(card, qPrintable(component.errorString()));
        card->setParentItem(window.contentItem());
        card->setPosition(QPointF(10, 10));
        card->setWidth(950);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *appearance =
            engine.singletonInstance<studio::Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"));
        QVERIFY(appearance);
        QVERIFY(appearance->setValue("tooltips", 1));
        QVERIFY(appearance->setValue("tooltipDelay", 0));
        auto metadata = card->property("project").toMap();
        metadata["updated"] = "2026-10-15T17:51:36.063140+00:00";
        metadata["environment"] = "client";
        metadata["followersRaw"] = 42000;
        metadata["followers"] = "42K";
        card->setProperty("project", metadata);
        QTest::mouseMove(&window, QPoint(980, 220));
        const auto idle = card->property("color").value<QColor>();
        QTest::mouseMove(&window, card->mapToScene(QPointF(400, 20)).toPoint());
        QTRY_VERIFY(card->property("color").value<QColor>() != idle);
        const auto highlighted = card->property("color").value<QColor>();
        for (const auto &name : {"browsePlatformBadge", "browseLoaderBadge", "browseVersionBadge",
                                 "browseCategoryBadge", "browseEnvironmentBadge", "browseStatistics",
                                 "browseUpdated", "browseInstallButton", "browseMoreButton"}) {
            auto *item = findItem(card.get(), name);
            QVERIFY(item);
            QTest::mouseMove(&window,
                             item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            QTest::qWait(30);
            QCOMPARE(card->property("color").value<QColor>(), highlighted);
            for (auto *tip : card->findChildren<QObject *>("roundedToolTip"))
                QVERIFY2(QString(name) == "browseStatistics" || QString(name) == "browseUpdated" ||
                             !tip->property("visible").toBool(),
                         qPrintable(QString("Unexpected card tooltip over %1").arg(name)));
        }
        metadata["downloadsRaw"] = qint64(83396619);
        metadata["downloads"] = "83.4M";
        metadata["downloadStats"] =
            QVariantList{QVariantMap{{"provider", "modrinth"}, {"raw", qint64(83389414)}},
                         QVariantMap{{"provider", "curseforge"}, {"raw", qint64(7205)}}};
        card->setProperty("project", metadata);
        for (const auto &name : {"browseDownloads", "browseFollowers", "browseUpdated"}) {
            auto *stat = findItem(card.get(), name);
            QVERIFY(stat);
            QTest::mouseMove(&window,
                             stat->mapToScene(QPointF(stat->width() / 2, stat->height() / 2)).toPoint());
            auto *tip = stat->findChild<QObject *>("roundedToolTip");
            QVERIFY(tip);
            QTRY_VERIFY(tip->property("visible").toBool());
            QTest::qWait(60);
            QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                             "/../artifacts/discovery-statistic-" + name + ".png"));
            const auto hint = tip->property("text").toString();
            QVERIFY(!hint.contains("T17:51"));
            if (QString(name) == "browseDownloads") {
                QVERIFY(hint.contains("Modrinth"));
                QVERIFY(hint.contains("CurseForge"));
                QVERIFY(hint.contains("total downloads"));
            }
            if (QString(name) == "browseFollowers")
                QVERIFY(hint.contains("followers"));
            if (QString(name) == "browseUpdated")
                QVERIFY(hint.startsWith("Updated "));
            QCOMPARE(card->property("color").value<QColor>(), highlighted);
        }
        appearance->reset();
        QTest::mouseMove(&window, QPoint(980, 220));
        QTRY_COMPARE(card->property("color").value<QColor>(), idle);
    }
    void scrollingCardsReserveScrollbarSpaceAndKeepClippedCornersRounded() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(620, 150);
        window.setColor(Qt::green);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import "components"
ContentListView {
    width: 600; height: 120; outsideColor: "lime"; model: 3; spacing: 8
    delegate: ProjectCard {
        objectName: "fixtureCard"; width: ListView.view.width
        project: ({title:"Example",author:"Author",summary:"Summary"})
        installState: ({canInstall:true,label:"Install"})
    }
})",
                          QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/ScrollFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> list(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(list);
        list->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(findItem(window.contentItem(), "fixtureCard"));
        auto *card = findItem(window.contentItem(), "fixtureCard");
        auto *bar = findItem(window.contentItem(), "contentScrollBar");
        QVERIFY(bar);
        QVERIFY(card->mapToScene(QPointF(card->width(), 0)).x() + 6 <= bar->mapToScene(QPointF{}).x());
        list->setProperty("contentY", 50);
        QTest::mouseMove(&window, QPoint(240, 20));
        QTest::qWait(120);
        const auto screenshot = window.grabWindow();
        QCOMPARE(screenshot.pixelColor(0, 0), QColor(Qt::green));
        QCOMPARE(screenshot.pixelColor(qRound(card->width()) - 2, 20),
                 card->property("color").value<QColor>());
    }
    void loadedInstanceFiltersRenderPromptlyWithoutRestartingCatalogueReads() {
        QTemporaryDir temp;
        auto catalogue = std::make_shared<ContentCatalogueFixture>();
        ContentActions model(std::make_shared<ContentFileFixture>(), catalogue,
                             std::make_shared<ContentInstallerFixture>(),
                             std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        QVariantList rows;
        for (int i = 0; i < 310; ++i)
            rows << QVariantMap{{"title", QString("Example %1").arg(i)},
                                {"relativePath", QString("mods/example%1.jar").arg(i)},
                                {"fileName", "example.jar"},
                                {"category", i % 2 ? "mods" : "resourcepacks"},
                                {"sha1", "hash" + QString::number(i)},
                                {"enabled", true},
                                {"version", "1.21.1"},
                                {"author", QString("Author %1").arg(i % 73)}};
        model.setContext({{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}}, rows);
        QTRY_VERIFY(!model.busy());
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1100, 650);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/pack/ContentPanel.qml"));
        std::unique_ptr<QQuickItem> panel(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"controller", QVariant::fromValue(&model)}})));
        QVERIFY2(panel, qPrintable(component.errorString()));
        panel->setParentItem(window.contentItem());
        panel->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto reads = catalogue->refreshCount;
        QElapsedTimer elapsed;
        elapsed.start();
        for (int i = 0; i < 12; ++i) {
            model.setCategory(i % 2 ? "all" : "mods");
            model.setQuery(i % 2 ? "1211" : "Example");
            QCoreApplication::processEvents();
        }
        const auto milliseconds = elapsed.elapsed();
        QSignalSpy rendered(&window, &QQuickWindow::frameSwapped);
        window.update();
        QVERIFY(rendered.wait(1000));
        qInfo() << "310 installed rows / 24 rendered filter transitions ms" << milliseconds;
        QCOMPARE(catalogue->refreshCount, reads);
        QCOMPARE(warnings.size(), 0);
        QVERIFY(findItem(panel.get(), "contentRow"));
        QVERIFY(test_performance::releaseBudgetMet(milliseconds, 450));
    }
    void tableHoverFillsRowsAndClipsOnceAtTheOuterFrame() {
        QTemporaryDir temp;
        ContentActions model(std::make_shared<ContentFileFixture>(),
                             std::make_shared<ContentCatalogueFixture>(),
                             std::make_shared<ContentInstallerFixture>(),
                             std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        QVariantList rows;
        for (int index = 0; index < 20; ++index)
            rows.append(QVariantMap{{"title", QString("Example %1").arg(index, 2, 10, QChar('0'))},
                                    {"relativePath", QString("mods/example%1.jar").arg(index)},
                                    {"fileName", "example.jar"},
                                    {"category", "mods"},
                                    {"sha1", "hash"},
                                    {"enabled", true}});
        model.setContext({{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}}, rows);
        QTRY_VERIFY(!model.busy());
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1124, 524);
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/pack/ContentPanel.qml"));
        std::unique_ptr<QQuickItem> panel(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"controller", QVariant::fromValue(&model)}})));
        QVERIFY2(panel, qPrintable(component.errorString()));
        panel->setParentItem(window.contentItem());
        panel->setPosition(QPointF(12, 12));
        panel->setSize(QSizeF(1100, 500));
        auto *corners = findItem(panel.get(), "contentTableCorners");
        QVERIFY(corners);
        const auto outside = corners->property("outsideColor").value<QColor>();
        window.setColor(outside);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *list = findItem(panel.get(), "instanceContentList");
        auto *viewport = findItem(panel.get(), "contentListViewport");
        auto *table = findItem(panel.get(), "contentTable");
        QVERIFY(list && viewport && table);
        const auto origin = viewport->mapToScene(QPointF{}).toPoint();
        const auto bottom = origin.y() + qRound(viewport->height()) - 1;
        auto *content = viewport->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        const auto hoveredRow = [&](int y) -> QQuickItem * {
            const QPoint point(origin.x() + 300, y);
            QTest::mouseMove(&window, point);
            QTest::qWait(100);
            for (auto *row : content->childItems())
                if (row->objectName() == "contentRow" &&
                    QRectF(row->mapToScene(QPointF{}), row->size()).contains(point))
                    return row;
            return nullptr;
        };
        auto *first = hoveredRow(origin.y() + 20);
        QVERIFY(first);
        auto rendered = window.grabWindow();
        const auto hover = first->property("color").value<QColor>();
        QVERIFY(hover != table->property("color").value<QColor>());
        for (const auto point : {QPoint(origin.x() + 1, origin.y() + 1),
                                 QPoint(origin.x() + qRound(viewport->width()) - 2, origin.y() + 1),
                                 QPoint(origin.x() + 1, origin.y() + 79)})
            QCOMPARE(rendered.pixelColor(point), hover);
        // A row clipped at the top stays full-width, without a second rounded mask.
        list->setProperty("contentY", 39);
        QVERIFY(hoveredRow(origin.y() + 5));
        rendered = window.grabWindow();
        QCOMPARE(rendered.pixelColor(origin.x() + 1, origin.y() + 1), hover);
        // Hover the partially visible bottom row: flat across the viewport, rounded only at the frame.
        QVERIFY(hoveredRow(bottom - 8));
        rendered = window.grabWindow();
        QCOMPARE(rendered.pixelColor(origin.x() + 25, bottom - 1), hover);
        QCOMPARE(rendered.pixelColor(origin.x() + qRound(viewport->width()) - 2, bottom - 1), hover);
        QCOMPARE(rendered.pixelColor(origin.x(), bottom), outside);
        // At the true end of the list no divider extends across the final rounded edge.
        list->setProperty("contentY", list->property("contentHeight").toReal() - list->height());
        auto *last = hoveredRow(bottom - 8);
        QVERIFY(last);
        QCOMPARE(last->property("index").toInt(), 19);
        rendered = window.grabWindow();
        QCOMPARE(rendered.pixelColor(origin.x() + 30, bottom), hover);
        QCOMPARE(rendered.pixelColor(origin.x(), bottom), outside);
    }
    void cardStatisticsShareCentersAndOneTrailingAlignment() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1000, 250);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/ProjectCard.qml"));
        std::unique_ptr<QQuickItem> card(qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"project", QVariantMap{{"title", "Example"},
                                     {"author", "Author"},
                                     {"summary", "Summary"},
                                     {"updated", "2026-10-15"},
                                     {"followersRaw", 42000},
                                     {"followers", "42.0K"},
                                     {"downloadStats", QVariantList{QVariantMap{{"provider", "modrinth"},
                                                                                {"count", "249.9M"}}}}}},
             {"installState",
              QVariantMap{{"installed", true}, {"canInstall", false}, {"label", "Installed"}}}})));
        QVERIFY2(card, qPrintable(component.errorString()));
        card->setParentItem(window.contentItem());
        card->setWidth(950);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *appearance =
            engine.singletonInstance<studio::Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"));
        QVERIFY(appearance);
        for (const int scale : {75, 100, 125}) {
            QVERIFY(appearance->setValue("contentScale", scale));
            QTest::qWait(100);
            auto *install = findItem(card.get(), "browseInstallButton");
            auto *statistics = findItem(card.get(), "browseStatistics");
            auto *updated = findItem(card.get(), "browseUpdated");
            auto *more = findItem(card.get(), "browseMoreButton");
            QVERIFY(install && statistics && updated && more);
            const auto right = [](QQuickItem *item) {
                return item->mapToScene(QPointF(item->width(), 0)).x();
            };
            QVERIFY(qAbs(right(install) - right(statistics)) < 1);
            QVERIFY(qAbs(right(install) - right(updated)) < 1);
            QVERIFY(right(install) + 4 * scale / 100.0 <= more->mapToScene(QPointF{}).x());
            QVERIFY(statistics->mapToScene(QPointF{}).y() >=
                    install->mapToScene(QPointF(0, install->height())).y());
            QVERIFY(updated->mapToScene(QPointF{}).y() >=
                    statistics->mapToScene(QPointF(0, statistics->height())).y());
            qreal center = -1;
            int count = 0;
            for (auto *statistic : statistics->childItems()) {
                auto *icon = findItem(statistic, "statisticIcon");
                auto *label = findItem(statistic, "statisticText");
                if (!icon || !label)
                    continue;
                ++count;
                const auto iconCenter = icon->mapToScene(QPointF(0, icon->height() / 2)).y();
                const auto labelCenter = label->mapToScene(QPointF(0, label->height() / 2)).y();
                QVERIFY(qAbs(iconCenter - labelCenter) <= 1);
                if (center >= 0)
                    QVERIFY(qAbs(center - iconCenter) <= 1);
                center = iconCenter;
            }
            QCOMPARE(count, 2);
        }
        appearance->reset();
    }
    void appearanceTabsSearchAndTogglesUpdateTheRealPreview() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(740, 740);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/settings/AppearanceSettings.qml"));
        std::unique_ptr<QQuickItem> settings(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(settings, qPrintable(component.errorString()));
        settings->setParentItem(window.contentItem());
        settings->setSize(QSizeF(720, 720));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *appearance =
            engine.singletonInstance<studio::Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"));
        QVERIFY(appearance);
        const auto click = [&window](QQuickItem *item) {
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                              item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        auto *tabs = findItem(settings.get(), "appearanceTabs");
        QVERIFY(tabs);
        click(findItem(tabs, "sectionTab_content"));
        QTRY_COMPARE(settings->property("section").toString(), QString("content"));
        auto *search = findItem(settings.get(), "appearanceSearch");
        QVERIFY(search);
        search->setProperty("text", "description");
        QTRY_VERIFY(findItem(settings.get(), "appearanceToggle_showSummary"));
        QVERIFY(!findItem(settings.get(), "appearanceField_artworkSize"));
        click(findItem(settings.get(), "appearanceToggle_showSummary"));
        QTRY_COMPARE(appearance->values().value("showSummary").toInt(), 0);
        QVERIFY(!findItem(settings.get(), "browseSummary"));
        click(findItem(settings.get(), "resetAppearanceSection"));
        QTRY_COMPARE(appearance->values().value("showSummary").toInt(), 1);
        QTRY_VERIFY(findItem(settings.get(), "browseSummary"));
        click(findItem(tabs, "sectionTab_colors"));
        QTRY_VERIFY(findItem(settings.get(), "themeColorsHeading"));
        QVERIFY(!findItem(settings.get(), "appearanceSearch"));
        auto *colorTabs = findItem(settings.get(), "themeColorTabs");
        QVERIFY(colorTabs);
        click(findItem(colorTabs, "sectionTab_status"));
        QTRY_COMPARE(colorTabs->property("current").toString(), QString("status"));
        click(findItem(settings.get(), "toggleAppearancePreview"));
        QTRY_VERIFY(!findItem(settings.get(), "appearanceLivePreview"));
        // The compact logical viewport retains accessible tabs without horizontal overflow.
        settings->setSize(QSizeF(450, 450));
        QCoreApplication::processEvents();
        QTRY_VERIFY2(tabs->width() <= settings->width(),
                     qPrintable(QString("tabs=%1 settings=%2").arg(tabs->width()).arg(settings->width())));
        appearance->reset();
    }
    void browsingSettingsApplyDefaultsAndKeepCompatibilitySeparate() {
        QTemporaryDir temp;
        BrowsePreferences preferences(
            std::make_shared<studio::BrowseSettingsStore>(temp.path() + "/browse.ini"));
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(660, 800);
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import "settings"
BrowseSettings {
    property alias applied: sample.received
    catalogue: QtObject { id: sample; property var received: ({}); function configure(values) { received=values } }
})",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/BrowseSettingsFixture.qml"));
        std::unique_ptr<QQuickItem> settings(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"preferences", QVariant::fromValue(&preferences)}})));
        QVERIFY2(settings, qPrintable(component.errorString()));
        settings->setParentItem(window.contentItem());
        settings->setWidth(640);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto click = [&window](QQuickItem *item) {
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                              item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        };
        QVERIFY(findItem(settings.get(), "browseDefault_size"));
        auto *hide = findItem(settings.get(), "browseDefault_hideInstalled");
        QVERIFY(hide);
        click(hide);
        QTRY_VERIFY(preferences.defaults().value("hideInstalled").toBool());
        click(findItem(settings.get(), "applyBrowseDefaults"));
        QTRY_COMPARE(settings->property("applied").toMap().value("hideInstalled").toBool(), true);
        auto *tabs = findItem(settings.get(), "browseSettingsTabs");
        QVERIFY(tabs);
        click(findItem(tabs, "sectionTab_compatibility"));
        QTRY_VERIFY(findItem(settings.get(), "browseConnectorFabric"));
        click(findItem(settings.get(), "browseConnectorFabric"));
        QTRY_VERIFY(!preferences.connectorFabric());
        QVERIFY(preferences.defaults().value("hideInstalled").toBool());
    }
    void tooltipVisibilityAndDelayFollowSharedPreferences() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(400, 200);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/ActionButton.qml"));
        std::unique_ptr<QQuickItem> button(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"text", "Example"}, {"hint", "Example tooltip"}})));
        QVERIFY2(button, qPrintable(component.errorString()));
        button->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *appearance =
            engine.singletonInstance<studio::Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"));
        QVERIFY(appearance);
        QVERIFY(appearance->setValue("tooltipDelay", 0));
        QTest::mouseMove(&window, QPoint(10, 10));
        auto *tip = button->findChild<QObject *>("roundedToolTip");
        QVERIFY(tip);
        QTRY_VERIFY(tip->property("visible").toBool());
        QVERIFY(appearance->setValue("tooltips", 0));
        QTRY_VERIFY(!tip->property("visible").toBool());
        QVERIFY(appearance->setValue("tooltips", 1));
        QTRY_VERIFY(tip->property("visible").toBool());
        appearance->reset();
    }
    void focusedPopupBlocksBackgroundScrollAndTyping() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(600, 400);
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
import QtQuick.Controls
import "components"
Item {
    width: 600; height: 400
    Flickable {
        objectName: "backgroundScroller"; anchors.fill: parent; contentHeight: 1200
        boundsBehavior: Flickable.StopAtBounds
        TextField { objectName: "backgroundInput"; width: 200; height: 40 }
        ScrollBar.vertical: ScrollBar {}
    }
    FocusPopup {
        objectName: "focusTestPopup"; x: 180; y: 80; width: 240; height: 180
        contentItem: TextField { objectName: "popupInput"; focus: true }
    }
})",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/PopupFocusFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> fixture(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(fixture, qPrintable(component.errorString()));
        fixture->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *background = findItem(window.contentItem(), "backgroundScroller");
        auto wheel = [&window] {
            const QPointF point(40, 280);
            QWheelEvent event(point, window.mapToGlobal(point.toPoint()), QPoint{}, QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&window, &event);
        };
        QVERIFY(background);
        wheel();
        QTRY_VERIFY(background->property("contentY").toReal() > 0);
        QTRY_VERIFY(!background->property("moving").toBool());
        const auto offset = background->property("contentY").toReal();
        auto *popup = fixture->findChild<QObject *>("focusTestPopup");
        QVERIFY(popup);
        QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        QTRY_VERIFY(popup->property("activeFocus").toBool());
        QVERIFY(popup->property("modal").toBool());
        wheel();
        QTest::qWait(100);
        QCOMPARE(background->property("contentY").toReal(), offset);
        for (const char key : QByteArray("inside"))
            QTest::keyClick(&window, key);
        auto *input = findItem(window.contentItem(), "popupInput");
        QVERIFY(input);
        QCOMPARE(input->property("text").toString(), QString("inside"));
        auto *behind = fixture->findChild<QObject *>("backgroundInput");
        QVERIFY(behind);
        QVERIFY(behind->property("text").toString().isEmpty());
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY(!popup->property("visible").toBool());
    }
    void disabledButtonsKeepIdleColorsUnderPointer() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(400, 150);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/ActionButton.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (const auto &mode : QStringList{"primary", "selected", "subtle", "accented"}) {
            std::unique_ptr<QQuickItem> button(
                qobject_cast<QQuickItem *>(component.createWithInitialProperties(
                    {{"text", "Installed"}, {"enabled", false}, {mode, true}})));
            QVERIFY(button);
            button->setParentItem(window.contentItem());
            button->setWidth(140);
            button->setHeight(40);
            QTest::mouseMove(&window, QPoint(300, 100));
            QTest::qWait(120);
            auto *background = button->property("background").value<QQuickItem *>();
            QVERIFY(background);
            const auto color = background->property("color");
            const auto foreground = button->property("foreground");
            QTest::mouseMove(&window, QPoint(40, 20));
            QTest::qWait(120);
            QCOMPARE(background->property("color"), color);
            QCOMPARE(button->property("foreground"), foreground);
            QVERIFY(!button->property("interactiveHover").toBool());
            button->setEnabled(true);
            QTest::mouseMove(&window, QPoint(300, 100));
            QTest::mouseMove(&window, QPoint(40, 20));
            QTRY_VERIFY(button->property("interactiveHover").toBool());
            button->setEnabled(false);
            QTest::qWait(120);
            QCOMPARE(background->property("color"), color);
            QCOMPARE(button->property("foreground"), foreground);
        }
    }
    void releaseFilterFormatsAndEmitsSelectedChannels() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(600, 400);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/ChoiceFilter.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> filter(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"label", "Channel"},
                                                   {"choices", QStringList{"release", "beta", "alpha"}},
                                                   {"values", QStringList{"release"}}})));
        QVERIFY2(filter, qPrintable(component.errorString()));
        filter->setParentItem(window.contentItem());
        QCOMPARE(filter->property("text").toString(), QString("Channel: Release"));
        QSignalSpy selection(filter.get(), SIGNAL(selectionRequested(QVariant)));
        QVERIFY(selection.isValid());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          QPoint(filter->width() / 2, filter->height() / 2));
        QTRY_VERIFY(findItem(window.contentItem(), "releaseFilterOption-beta"));
        auto *beta = findItem(window.contentItem(), "releaseFilterOption-beta");
        QCOMPARE(beta->property("text").toString(), QString("Beta"));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          beta->mapToScene(QPointF(beta->width() / 2, beta->height() / 2)).toPoint());
        QTRY_COMPARE(selection.count(), 1);
        const auto selected = selection.first().first().value<QJSValue>().toVariant().toStringList();
        QCOMPARE(selected, (QStringList{"release", "beta"}));
        filter->setProperty("values", selected);
        QCOMPARE(filter->property("text").toString(), QString("Channel: 2 selected"));
        auto *count = findItem(window.contentItem(), "choiceFilterCount");
        QVERIFY(count);
        QCOMPARE(count->property("text").toString(), QString("2 selected"));
        auto *clear = findItem(filter.get(), "choiceFilterClear");
        QVERIFY(clear);
        auto *popup = filter->property("popup").value<QObject *>();
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          clear->mapToScene(QPointF(clear->width() / 2, clear->height() / 2)).toPoint());
        QTRY_COMPARE(selection.count(), 2);
        QVERIFY(selection.last().first().value<QJSValue>().toVariant().toList().isEmpty());
        QVERIFY(!popup->property("visible").toBool());
    }
    void versionChoiceSearchIsBoundedAndPlatformsKeepDataPacks() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(620, 440);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/ChoiceFilter.qml"));
        QStringList versions;
        for (int i = 32; i > 0; --i)
            versions << QString("1.21.%1").arg(i);
        std::unique_ptr<QQuickItem> filter(qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"label", "Game version"}, {"choices", versions}, {"values", QStringList{"1.21.1"}}})));
        QVERIFY2(filter, qPrintable(component.errorString()));
        filter->setParentItem(window.contentItem());
        filter->setPosition(QPointF(12, 12));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          filter->mapToScene(QPointF(50, 18)).toPoint());
        QTRY_VERIFY(findItem(window.contentItem(), "choiceFilterSearch"));
        auto *search = findItem(window.contentItem(), "choiceFilterSearch");
        auto *options = findItem(window.contentItem(), "choiceFilterOptions");
        QVERIFY(search && options);
        QVERIFY(options->height() <= 280);
        QVERIFY(options->property("contentHeight").toReal() > options->height());
        QTest::qWait(60);
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/version-dropdown.png"));
        search->setProperty("text", "1211");
        QTRY_COMPARE(options->property("model").toStringList().first(), QString("1.21.1"));
        search->setProperty("text", "");
        for (const auto character : QByteArray("12132"))
            QTest::keyClick(&window, static_cast<Qt::Key>(character));
        QTRY_COMPARE(options->property("count").toInt(), 1);
        QVERIFY(findItem(window.contentItem(), "releaseFilterOption-1.21.32"));
        auto *popup = filter->property("popup").value<QObject *>();
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        filter->setProperty("loaderNames", true);
        filter->setProperty("label", "Platform");
        filter->setProperty("choices", QStringList{"datapack", "neoforge", "fabric", "forge", "quilt"});
        filter->setProperty("values", QStringList{"datapack", "neoforge"});
        QCOMPARE(filter->property("text").toString(), QString("Platform: 2 selected"));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          filter->mapToScene(QPointF(50, 18)).toPoint());
        QTRY_VERIFY(findItem(window.contentItem(), "releaseFilterOption-datapack"));
        auto *data = findItem(window.contentItem(), "releaseFilterOption-datapack");
        QCOMPARE(data->property("text").toString(), QString("Data Pack"));
        QVERIFY(data->property("selected").toBool());
        QTest::qWait(60);
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/platform-dropdown.png"));
    }
    void compactCardsKeepEqualArtworkInsetsAndFilteredOrderedBadges() {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(900, 200);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/ProjectCard.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> card(qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"project", QVariantMap{{"title", "Example"},
                                     {"author", "Author"},
                                     {"summary", "Description"},
                                     {"provider", "modrinth"},
                                     {"sources", QStringList{"modrinth"}},
                                     {"loaders", QStringList{"neoforge", "fabric", "quilt"}},
                                     {"environment", "client"},
                                     {"categories", QStringList{"optimization"}}}},
             {"minecraft", "1.21.1"},
             {"selectedLoaders", QStringList{"neoforge"}},
             {"installState", QVariantMap{{"canInstall", true}, {"label", "Install"}}}})));
        QVERIFY2(card, qPrintable(component.errorString()));
        card->setParentItem(window.contentItem());
        card->setWidth(900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *appearance =
            engine.singletonInstance<studio::Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"));
        QVERIFY(appearance);
        for (const int scale : {100, 125, 75}) {
            QVERIFY(appearance->setValue("contentScale", scale));
            QCoreApplication::processEvents();
            auto *art = card->findChild<QQuickItem *>("browseArtwork");
            QVERIFY(art);
            QCOMPARE(art->x(), art->y());
            auto *title = findItem(card.get(), "browseTitle"), *author = findItem(card.get(), "browseAuthor");
            QVERIFY(title && author);
            QVERIFY(qAbs(title->mapToScene(QPointF(0, title->baselineOffset())).y() -
                         author->mapToScene(QPointF(0, author->baselineOffset())).y()) < 1);
            QVERIFY(qAbs(card->height() - art->height() - 2 * art->x()) < 0.1);
            auto *loader = findItem(card.get(), "browseLoaderBadge");
            QVERIFY(loader);
            int loaderCount = 0;
            for (auto *item : loader->parentItem()->childItems())
                loaderCount += item->objectName() == "browseLoaderBadge";
            QCOMPARE(loaderCount, 1);
            QCOMPARE(loader->property("brand").toString(), QString("neoforge"));
            auto *platform = findItem(card.get(), "browsePlatformBadge");
            auto *version = findItem(card.get(), "browseVersionBadge");
            auto *tag = findItem(card.get(), "browseCategoryBadge");
            QVERIFY(platform && version && tag);
            QVERIFY(platform->x() < loader->x());
            QVERIFY(loader->x() < version->x());
            QVERIFY(version->x() < tag->x());
            auto *badges = card->findChild<QQuickItem *>("browseBadges");
            QCOMPARE(badges->y() + badges->height(), art->y() + art->height());
            auto *menu = card->findChild<QQuickItem *>("browseMoreButton");
            QVERIFY(qAbs(card->width() - menu->x() - menu->width() - art->x()) < 0.1);
        }
        appearance->reset();
        QVERIFY(card->height() < 130);
        card->setProperty("installState",
                          QVariantMap{{"canInstall", false}, {"installed", false}, {"label", "Unavailable"}});
        QTRY_COMPARE(card->opacity(), 1.0);
        card->setProperty("installState",
                          QVariantMap{{"canInstall", false}, {"installed", true}, {"label", "Installed"}});
        QTRY_COMPARE(card->opacity(), 0.5);
        QVERIFY(appearance->setValue("installedOpacity", 70));
        QTRY_COMPARE(card->opacity(), 0.7);
        card->setProperty("installState",
                          QVariantMap{{"canInstall", true}, {"installed", false}, {"label", "Install"}});
        QTRY_COMPARE(card->opacity(), 1.0);
        appearance->reset();
        auto metadata = card->property("project").toMap();
        for (const auto &type : {"resourcepack", "datapack"}) {
            metadata["type"] = type;
            metadata["loaders"] = QStringList{"datapack", "fabric"};
            card->setProperty("project", metadata);
            QTRY_COMPARE(card->property("supportedLoaders").value<QJSValue>().toVariant().toStringList(),
                         (QStringList{"datapack", "fabric"}));
        }
    }
    void browseCardInstallClickDoesNotOpenTheProject() {
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/ProjectCard.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        auto *card = qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"project", QVariantMap{{"title", "Example"},
                                     {"author", "Author"},
                                     {"summary", "A test project"},
                                     {"loaders", QStringList{"fabric", "neoforge"}},
                                     {"downloadStats", QVariantList{QVariantMap{{"provider", "modrinth"},
                                                                                {"count", "123K"}}}}}},
             {"installState", QVariantMap{{"canInstall", true}, {"label", "Install"}}}}));
        QVERIFY2(card, qPrintable(component.errorString()));
        QQuickWindow window;
        window.setWidth(1100);
        window.setHeight(190);
        card->setParent(&window);
        card->setParentItem(window.contentItem());
        card->setWidth(1100);
        card->setHeight(170);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QSignalSpy opens(card, SIGNAL(opened()));
        QSignalSpy installs(card, SIGNAL(installRequested()));
        auto *button = findItem(window.contentItem(), "browseInstallButton");
        QVERIFY(button);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_COMPARE(installs.count(), 1);
        QCOMPARE(opens.count(), 0);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          card->mapToScene(QPointF(160, 30)).toPoint());
        QTRY_COMPARE(opens.count(), 1);
        QSignalSpy visibility(card, SIGNAL(visibilityRequested(bool)));
        auto *more = findItem(window.contentItem(), "browseMoreButton");
        QVERIFY(more);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          more->mapToScene(QPointF(more->width() / 2, more->height() / 2)).toPoint());
        auto *popup = more->property("popup").value<QObject *>();
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          more->mapToScene(QPointF(more->width() / 2, more->height() / 2)).toPoint());
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          more->mapToScene(QPointF(more->width() / 2, more->height() / 2)).toPoint());
        QTRY_VERIFY(popup->property("opened").toBool());
        QTRY_VERIFY(findItem(window.contentItem(), "browseHideContent"));
        auto *hide = findItem(window.contentItem(), "browseHideContent");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          hide->mapToScene(QPointF(hide->width() / 2, hide->height() / 2)).toPoint());
        QTRY_COMPARE(visibility.count(), 1);
        QCOMPARE(opens.count(), 1);
        QCOMPARE(installs.count(), 1);
    }
    void configCardsShowTheActualFilePath() {
        QTemporaryDir temp;
        ConfigLibrary model(std::make_shared<SettingsConfigOwnershipStore>(temp.path() + "/owners.ini"));
        model.setContext({{"path", "pack"}}, {QVariantMap{{"category", "configs"},
                                                          {"relativePath", "config/example-client.toml"},
                                                          {"fileName", "example-client.toml"},
                                                          {"relatedModId", "example"},
                                                          {"relatedModTitle", "Example"},
                                                          {"association", "Matched by file name"}}});
        QQmlEngine engine;
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/pack/ConfigPanel.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        auto *panel = qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"library", QVariant::fromValue(&model)}}));
        QVERIFY(panel);
        QQuickWindow window;
        window.setWidth(1100);
        window.setHeight(700);
        panel->setParent(&window);
        panel->setParentItem(window.contentItem());
        panel->setWidth(1100);
        panel->setHeight(700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(findItem(window.contentItem(), "configFilePath"));
        const auto label = findItem(window.contentItem(), "configFilePath")->property("text").toString();
        QVERIFY(label.startsWith("config/example-client.toml"));
    }
    void artworkFillsRoundedFrameWithoutPadding() {
        QImage image(32, 32, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/ContentIcon.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        auto *icon = qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"source", QString::fromLatin1("data:image/png;base64," + data.toBase64())}, {"size", 64}}));
        QVERIFY(icon);
        QQuickWindow window;
        window.setColor(Qt::white);
        window.setWidth(64);
        window.setHeight(64);
        icon->setParent(&window);
        icon->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(window.grabWindow().pixelColor(2, 32).red() > 240 &&
                    window.grabWindow().pixelColor(2, 32).green() < 10);
        const auto rendered = window.grabWindow();
        QVERIFY(rendered.pixelColor(32, 32).green() < 10);
        QVERIFY(rendered.pixelColor(0, 0).green() > 200);
    }
    void rowClickAndActionClicksHaveSeparateBehavior() {
        QTemporaryDir temp;
        ContentActions model(std::make_shared<ContentFileFixture>(),
                             std::make_shared<ContentCatalogueFixture>(),
                             std::make_shared<ContentInstallerFixture>(),
                             std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        model.setContext(
            {{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}},
            {QVariantMap{
                {"title", "Example"},
                {"relativePath", "mods/example.jar"},
                {"fileName", "example.jar"},
                {"category", "mods"},
                {"sha1", "hash"},
                {"enabled", true},
                {"version", "1.0"},
                {"author", "First, Second"},
                {"authors",
                 QVariantList{
                     QVariantMap{{"name", "First"}, {"ids", QVariantMap{{"modrinth", "first-id"}}}},
                     QVariantMap{{"name", "Second"}, {"ids", QVariantMap{{"curseforge", "second-id"}}}}}},
                {"projectId", "project"},
                {"sourceIds", QVariantMap{{"modrinth", "project"}}},
                {"projectUrl", "https://modrinth.com/mod/project"}}});
        QTRY_VERIFY(!model.busy());
        QQmlEngine engine;
        QQmlComponent component(
            &engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/pack/ContentPanel.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window;
        window.setWidth(1100);
        window.setHeight(700);
        auto *panel = qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"controller", QVariant::fromValue(&model)}}));
        QVERIFY2(panel, qPrintable(component.errorString()));
        panel->setParent(&window);
        panel->setParentItem(window.contentItem());
        panel->setWidth(1100);
        panel->setHeight(700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        model.setFilter("updates", true);
        QTRY_VERIFY(findItem(window.contentItem(), "clearContentFilters"));
        QSignalSpy frames(&window, &QQuickWindow::frameSwapped);
        window.update();
        QTRY_VERIFY(!frames.isEmpty());
        auto *clearFilters = findItem(window.contentItem(), "clearContentFilters");
        QTest::mouseClick(
            &window, Qt::LeftButton, Qt::NoModifier,
            clearFilters->mapToScene(QPointF(clearFilters->width() / 2, clearFilters->height() / 2))
                .toPoint());
        QTRY_VERIFY(!model.filters().value("updates").toBool());
        model.setFilter("hidden", true);
        QTRY_VERIFY(findItem(window.contentItem(), "clearContentFilters"));
        frames.clear();
        window.update();
        QTRY_VERIFY(!frames.isEmpty());
        clearFilters = findItem(window.contentItem(), "clearContentFilters");
        QTest::mouseClick(
            &window, Qt::LeftButton, Qt::NoModifier,
            clearFilters->mapToScene(QPointF(clearFilters->width() / 2, clearFilters->height() / 2))
                .toPoint());
        QTRY_VERIFY(!model.filters().value("hidden").toBool());
        QTRY_VERIFY(findItem(window.contentItem(), "contentRow"));
        QSignalSpy showcase(panel, SIGNAL(showcaseRequested(QString, QString)));
        auto *hidden = findItem(window.contentItem(), "contentHiddenFilter");
        auto *updates = findItem(window.contentItem(), "contentUpdatesFilter");
        auto *all = findItem(window.contentItem(), "contentUpdateAll");
        auto *refresh = findItem(window.contentItem(), "contentRefresh");
        QVERIFY(hidden && updates && all && refresh);
        QVERIFY(hidden->mapToScene(QPointF{}).x() < updates->mapToScene(QPointF{}).x());
        QVERIFY(updates->mapToScene(QPointF{}).x() < all->mapToScene(QPointF{}).x());
        QVERIFY(qAbs(refresh->mapToScene(QPointF(refresh->width(), 0)).x() - panel->width()) < 2);
        auto *row = findItem(window.contentItem(), "contentRow");
        QVERIFY(row);
        QCOMPARE(row->property("radius").toReal(), 0.0);
        auto *table = findItem(window.contentItem(), "contentTable");
        QVERIFY(table);
        QCOMPARE(QQmlProperty(table, "border.width").read().toInt(), 0);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          row->mapToScene(QPointF(150, 24)).toPoint());
        QTRY_COMPARE(showcase.count(), 1);
        QCOMPARE(showcase.first().first().toString(), QString("mods/example.jar"));
        const auto tableOrigin = table->mapToScene(QPointF{});
        const auto listSize = findItem(panel, "instanceContentList")->size();
        auto *nameLink = findItem(window.contentItem(), "contentNameLink");
        auto *versionLink = findItem(window.contentItem(), "contentVersionLink");
        QVERIFY(nameLink && versionLink);
        QTest::mouseMove(&window, nameLink->mapToScene(QPointF(20, 8)).toPoint());
        QTRY_VERIFY(nameLink->property("font").value<QFont>().underline());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          versionLink->mapToScene(QPointF(20, 8)).toPoint());
        QTRY_COMPARE(showcase.count(), 2);
        QCOMPARE(showcase.last().at(1).toString(), QString("versions"));
        showcase.clear();
        QSignalSpy authorsRequested(panel, SIGNAL(authorRequested(QVariant)));
        auto *authorGroup = findItem(panel, "contentAuthorLink");
        QVERIFY(authorGroup);
        QList<QQuickItem *> authorNames;
        std::function<void(QQuickItem *)> collectAuthors = [&](QQuickItem *item) {
            if (item->objectName() == "authorNameLink")
                authorNames << item;
            for (auto *child : item->childItems())
                collectAuthors(child);
        };
        collectAuthors(authorGroup);
        QCOMPARE(authorNames.size(), 2);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          authorNames[1]
                              ->mapToScene(QPointF(authorNames[1]->width() / 2, authorNames[1]->height() / 2))
                              .toPoint());
        QTRY_COMPARE(authorsRequested.size(), 1);
        QCOMPARE(showcase.size(), 0);
        const auto authorValue = authorsRequested[0][0];
        const auto requestedAuthor = authorValue.metaType() == QMetaType::fromType<QJSValue>()
                                         ? authorValue.value<QJSValue>().toVariant().toMap()
                                         : authorValue.toMap();
        QCOMPARE(requestedAuthor.value("name").toString(), QString("Second"));
        QCOMPARE(requestedAuthor.value("ids").toMap(), QVariantMap({{"curseforge", "second-id"}}));
        auto *selection = findItem(window.contentItem(), "contentSelection");
        QVERIFY(selection);
        QTest::mouseClick(
            &window, Qt::LeftButton, Qt::NoModifier,
            selection->mapToScene(QPointF(selection->width() / 2, selection->height() / 2)).toPoint());
        QTRY_COMPARE(model.selected().size(), 1);
        QCOMPARE(table->mapToScene(QPointF{}), tableOrigin);
        QCOMPARE(findItem(panel, "instanceContentList")->size(), listSize);
        QVERIFY(findItem(panel, "contentSelectionBar"));
        QVERIFY(findItem(panel, "instanceContentList")->property("bottomInset").toReal() > 48);
        QTest::qWait(50);
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/floating-content-selection.png"));
        QCOMPARE(showcase.count(), 0);
        auto *more = findItem(window.contentItem(), "contentMore");
        QVERIFY(more);
        QTRY_VERIFY(more->mapToScene(QPointF(more->width(), 0)).x() <= window.width());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          more->mapToScene(QPointF(more->width() / 2, more->height() / 2)).toPoint());
        auto *popup = more->property("popup").value<QObject *>();
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          more->mapToScene(QPointF(more->width() / 2, more->height() / 2)).toPoint());
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          more->mapToScene(QPointF(more->width() / 2, more->height() / 2)).toPoint());
        QTRY_VERIFY(popup->property("opened").toBool());
        QTRY_VERIFY(findItem(window.contentItem(), "copyContentLink"));
        auto *copy = findItem(window.contentItem(), "copyContentLink");
        QTRY_VERIFY(copy->mapToScene(QPointF(copy->width(), 0)).x() <= window.width());
        QSignalSpy copied(&model, &ContentActions::copyRequested);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          copy->mapToScene(QPointF(copy->width() / 2, copy->height() / 2)).toPoint());
        QTRY_COMPARE(copied.count(), 1);
        QCOMPARE(copied.first().first().toString(), QString("https://modrinth.com/mod/project"));
        QCOMPARE(showcase.count(), 0);
        auto *originalToggle = findItem(panel, "contentToggle");
        QVERIFY(originalToggle);
        for (int i = 0; i < 5; ++i)
            model.setProgress("Background work");
        QCOMPARE(findItem(panel, "contentToggle"), originalToggle);
        QTest::mouseClick(
            &window, Qt::LeftButton, Qt::NoModifier,
            originalToggle->mapToScene(QPointF(originalToggle->width() / 2, originalToggle->height() / 2))
                .toPoint());
        QVERIFY(!model.rows().first().toMap().value("enabled").toBool());
        QCOMPARE(findItem(panel, "contentToggle"), originalToggle);
        QTRY_VERIFY(!model.operationsBusy());
        QCOMPARE(findItem(panel, "contentToggle"), originalToggle);
        model.setLocked(model.rows().first().toMap().value("relativePath").toString(), true);
        QVERIFY(!originalToggle->isEnabled());
    }
};
QTEST_MAIN(ContentInteractionTests)
#include "ContentInteractionTests.moc"
