#include "ContentFixtures.h"
#include "features/config/ConfigLibrary.h"
#include "features/content/ContentActions.h"
#include "features/preferences/ContentPreferences.h"
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
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY(!viewer->property("visible").toBool());
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
        QTRY_COMPARE(card->opacity(), 0.5);
        QVERIFY(appearance->setValue("unavailableOpacity", 70));
        QTRY_COMPARE(card->opacity(), 0.7);
        card->setProperty("installState",
                          QVariantMap{{"canInstall", false}, {"installed", true}, {"label", "Installed"}});
        QTRY_COMPARE(card->opacity(), 1.0);
        appearance->reset();
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
        model.setContext({{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}},
                         {QVariantMap{{"title", "Example"},
                                      {"relativePath", "mods/example.jar"},
                                      {"fileName", "example.jar"},
                                      {"category", "mods"},
                                      {"sha1", "hash"},
                                      {"enabled", true},
                                      {"version", "1.0"},
                                      {"author", "Author"},
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
        QSignalSpy showcase(panel, SIGNAL(showcaseRequested(QString)));
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
        QVERIFY(row->property("radius").toReal() > 0);
        auto *table = findItem(window.contentItem(), "contentTable");
        QVERIFY(table);
        QCOMPARE(QQmlProperty(table, "border.width").read().toInt(), 0);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          row->mapToScene(QPointF(150, 24)).toPoint());
        QTRY_COMPARE(showcase.count(), 1);
        QCOMPARE(showcase.first().first().toString(), QString("mods/example.jar"));
        auto *selection = findItem(window.contentItem(), "contentSelection");
        QVERIFY(selection);
        QTest::mouseClick(
            &window, Qt::LeftButton, Qt::NoModifier,
            selection->mapToScene(QPointF(selection->width() / 2, selection->height() / 2)).toPoint());
        QTRY_COMPARE(model.selected().size(), 1);
        QCOMPARE(showcase.count(), 1);
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
        QCOMPARE(showcase.count(), 1);
    }
};
QTEST_MAIN(ContentInteractionTests)
#include "ContentInteractionTests.moc"
