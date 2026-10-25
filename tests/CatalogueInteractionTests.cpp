#include "CatalogueFixtures.h"
#include "ContentFixtures.h"
#include "features/explore/BrowseCatalogue.h"
#include "features/explore/BrowseInstalls.h"
#include "features/preferences/ContentVisibility.h"
#include "theme/Appearance.h"
#include "theme/TextSearch.h"
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScreen>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)
using namespace studio;
class CatalogueInteractionTests final : public QObject {
    Q_OBJECT
    QTemporaryDir storage;
    static QQuickItem *item(QQuickItem *root, const QString &name) {
        if (root->objectName() == name && root->isVisible())
            return root;
        for (auto *child : root->childItems())
            if (auto *found = item(child, name))
                return found;
        return nullptr;
    }
    static QPoint center(QQuickItem *item) {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }
    static QByteArray wrapper() {
#ifndef Q_MOC_RUN
        return R"QML(import QtQuick
import QtQuick.Controls
import Conflux.Style
import ")QML" + QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/pages").toString().toUtf8() +
               R"QML("
import ")QML" +
               QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/components/IconPaths.js")
                   .toString()
                   .toUtf8() +
               R"QML(" as Glyphs
Item {
    id: shell
    required property var catalogueModel
    property real uiScale: 1
    property var browseModel: null
    function hasGlyph(name) { return !!Glyphs.markup[name] }
    Rectangle { anchors.fill: parent; color: Theme.colors.background }
    QtObject { id: fakeApp; property var libraryPacks: [{title:"Fixture",path:"fixture",provider:"modrinth"}]; property bool navigationBusy: false }
    QtObject { id: fakeBrowse; property bool busy: false; property bool ready: true; property bool connectorAvailable: false; property var pack: ({loader:"neoforge"}); function state(project) { return {label:"Install",canInstall:true,installed:false} } }
    QtObject { id: fakeContent; property int lockRevision: 0; function sourceLock(project) { return {} } function projectLocked(project) { return false } }
    DiscoverPage {
        objectName: "pageUnderTest"
        x: 216 * shell.uiScale; y: 40 * shell.uiScale
        width: shell.width / shell.uiScale - 216; height: shell.height / shell.uiScale - 40
        scale: shell.uiScale; transformOrigin: Item.TopLeft
        app: fakeApp; content: fakeContent; browse: shell.browseModel || fakeBrowse; catalogue: shell.catalogueModel; worlds: null; targetPath: "fixture"
    }
})QML";
#endif
    }
    static std::unique_ptr<QQuickItem> page(QQmlEngine &engine, BrowseCatalogue &model, QQuickWindow &window,
                                            qreal scale) {
        engine.singletonInstance<Appearance *>("Conflux.Style", "Appearance")->reset();
        QQmlComponent component(&engine);
        component.setData(wrapper(), QUrl("file:///catalogue-fixture.qml"));
        if (!component.isReady())
            qWarning().noquote() << component.errorString();
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"catalogueModel", QVariant::fromValue(&model)}, {"uiScale", scale}})));
        if (root) {
            root->setParentItem(window.contentItem());
            root->setSize(window.size());
        }
        return root;
    }
  private slots:
    void sharedQmlSearchRanksLiteralChoicesWithoutTypoMatches() {
        TextSearch search;
        const QStringList choices{"Compact", "Incompatible", "Compatibility", "Another Compat", "Compat"};
        QCOMPARE(search.filter(choices, "compat"),
                 (QStringList{"Compat", "Another Compat", "Compatibility", "Incompatible"}));
        QCOMPARE(search.filter({"Create Aeronautics", "Aerodynamic", "Aero"}, "aero"),
                 (QStringList{"Aero", "Create Aeronautics", "Aerodynamic"}));
        QCOMPARE(search.filter({"1.21.4", "1.21.1", "1.21"}, "1211"), QStringList{"1.21.1"});
        QCOMPARE(search.filter(choices, ""), choices);
        QVERIFY(!search.matches("Compact", "compat"));
        QVERIFY(!search.matches("ModExample", "ModXample"));
        QVERIFY(search.filter({"ModExample", "Sodium"}, "Sodum").isEmpty());
    }

    void dropdownRowsFillTheirPopupAndOnlySelectionHasAnOutline() {
#ifndef Q_MOC_RUN
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            R"QML(import QtQuick
import "components"
Item { SelectBox { objectName:"sizeSelect";x:20;y:20;width:220;model:["10 per page","20 per page","50 per page","100 per page"];currentIndex:1 } })QML",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/MenuSizingFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(root);
        QQuickWindow window;
        window.resize(600, 400);
        root->setParentItem(window.contentItem());
        root->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *select = item(root.get(), "sizeSelect");
        QVERIFY(select);
        QSignalSpy activated(select, SIGNAL(activated(int)));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(select));
        QTRY_VERIFY(item(window.contentItem(), "selectBoxOption-0") &&
                    item(window.contentItem(), "selectBoxOption-1") &&
                    item(window.contentItem(), "selectBoxOption-2"));
        auto *option = item(window.contentItem(), "selectBoxOption-0"),
             *selected = item(window.contentItem(), "selectBoxOption-1");
        QVERIFY(selected);
        auto *background = qvariant_cast<QQuickItem *>(option->property("background"));
        auto *selectedBackground = qvariant_cast<QQuickItem *>(selected->property("background"));
        QVERIFY(background && selectedBackground);
        QCOMPARE(QQmlProperty(background, "border.width").read().toInt(), 0);
        QCOMPARE(QQmlProperty(selectedBackground, "border.width").read().toInt(), 1);
        QCOMPARE(option->width(), option->parentItem()->parentItem()->width());
        auto *text = qvariant_cast<QQuickItem *>(option->property("contentItem"));
        QVERIFY(text);
        QVERIFY(!text->property("truncated").toBool());
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_TEST_ARTIFACT_DIR) +
                                         "/shared-dropdown-options.png"));
        auto *next = item(window.contentItem(), "selectBoxOption-2");
        QVERIFY(next);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(next));
        QTRY_COMPARE(select->property("currentIndex").toInt(), 2);
        QCOMPARE(activated.count(), 1);
#endif
    }
    void creationUsesSearchableVersionsAndIndependentLoaderIcons() {
#ifndef Q_MOC_RUN
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            R"QML(import QtQuick
import QtQuick.Controls
import "components"
Item {
 id:root
 property var created:[]
 property var versions:["1.21.11","1.21.1","1.21","1.20.1","1.19.4","1.18.2","1.17.1","1.16.5","1.15.2","1.14.4","1.13.2"]
 QtObject { id:applicationModel;property bool repositoryBusy:false;function createPack(name,game,loader){root.created=[name,game,loader]} }
 QtObject { id:catalogueModel;property var options:({versions:root.versions});function loadOptions(){} }
 CreatePackDialog { id:dialog;parent:Overlay.overlay;app:applicationModel;catalogue:catalogueModel }
 function display(){dialog.open()}
})QML",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/CreationFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(root, qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(1080, 720);
        root->setParentItem(window.contentItem());
        root->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(QMetaObject::invokeMethod(root.get(), "display"));
        QTRY_VERIFY(item(window.contentItem(), "packNameInput"));
        auto *name = item(window.contentItem(), "packNameInput");
        name->forceActiveFocus();
        for (char c : QByteArray("Friendly pack"))
            QTest::keyClick(&window, c);
        auto *version = item(window.contentItem(), "createPackVersion");
        QVERIFY(version);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(version));
        QTRY_VERIFY(item(window.contentItem(), "searchableSelectSearch"));
        auto *search = item(window.contentItem(), "searchableSelectSearch");
        for (char c : QByteArray("1211"))
            QTest::keyClick(&window, c);
        QTRY_VERIFY(item(window.contentItem(), "searchableSelectOption-1.21.1"));
        QCOMPARE(search->property("text").toString(), QString("1211"));
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_COMPARE(version->property("value").toString(), QString("1.21.1"));
        auto *loader = item(window.contentItem(), "createPackLoader");
        QVERIFY(loader);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(loader));
        QTRY_VERIFY(item(window.contentItem(), "searchableSelectOption-neoforge"));
        auto *neo = item(window.contentItem(), "searchableSelectOption-neoforge");
        QVERIFY(item(neo, "brandBadgeImage"));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(neo));
        QTRY_COMPARE(loader->property("value").toString(), QString("neoforge"));
        auto *confirm = item(window.contentItem(), "createPackConfirm");
        QVERIFY(confirm);
        QVERIFY(confirm->isEnabled());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(confirm));
        QTRY_COMPARE(root->property("created").value<QJSValue>().toVariant().toList(),
                     QVariantList({"Friendly pack", "1.21.1", "neoforge"}));
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_TEST_ARTIFACT_DIR) +
                                         "/library-create-dialog.png"));
#endif
    }
    void installedContentVersionPickerAcceptsDownloadableCurseforgeVersions() {
#ifndef Q_MOC_RUN
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(import QtQuick
import QtQuick.Controls
import "pack"
Item {
    required property var releaseRows
    QtObject { id:controller;property var versionTarget:({title:"CF fixture",locked:false,frozen:false});property var versions:releaseRows;property var versionGroups:[];property string versionError:"";property bool versionsBusy:false;function installVersion(index){} }
    VersionPicker { id:picker;objectName:"picker";controller:controller;parent:Overlay.overlay }
    function display() { picker.open();picker.selectedVersion=0 }
})QML",
                          QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/PickerFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QVariantMap release{{"provider", "curseforge"},
                            {"url", "https://example.invalid/file.jar"},
                            {"sha1", QString(40, 'a')}};
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"releaseRows", QVariantList{release}}})));
        QVERIFY(root);
        QQuickWindow window;
        window.resize(1100, 800);
        root->setParentItem(window.contentItem());
        root->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(QMetaObject::invokeMethod(root.get(), "display"));
        QTRY_VERIFY(item(window.contentItem(), "versionPickerInstall"));
        auto *button = item(window.contentItem(), "versionPickerInstall");
        QVERIFY(button->isEnabled());
        release["url"] = "";
        root->setProperty("releaseRows", QVariantList{release});
        QTRY_VERIFY(!button->isEnabled());
#endif
    }
    void installedCardReactsWhenOnlyInventoryRevisionChanges() {
        auto source = std::make_shared<catalogue_test::Search>(1);
        BrowseCatalogue catalogue(
            source, std::make_shared<ContentVisibility>(std::make_shared<catalogue_test::Settings>()));
        catalogue.setContext("fixture", {}, {});
        QTRY_VERIFY(!catalogue.busy());
        BrowseInstalls browse(std::make_shared<ContentCatalogueFixture>());
        browse.setTargetPath("fixture");
        const QVariantMap pack{{"path", "fixture"}, {"loader", "neoforge"}};
        browse.setContext(pack, {}, false);
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1366, 768);
        auto root = page(engine, catalogue, window, 1);
        QVERIFY(root);
        root->setProperty("browseModel", QVariant::fromValue(&browse));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(item(root.get(), "browseInstallButton"));
        auto *button = item(root.get(), "browseInstallButton");
        QCOMPARE(button->property("text").toString(), QString("Install"));
        QVERIFY(button->isEnabled());
        const QVariantMap row{{"category", "mods"},
                              {"relativePath", "mods/fixture.jar.disabled"},
                              {"sha1", "exact-binary"},
                              {"enabled", false},
                              {"sourceIds", QVariantMap{{"modrinth", "0"}, {"curseforge", "42"}}}};
        browse.setContext(pack, {row}, false); // ready and busy deliberately remain unchanged.
        QTRY_COMPARE(button->property("text").toString(), QString("Installed \u00b7 disabled"));
        QVERIFY(!button->isEnabled());
        browse.setContext(pack, {}, false);
        QTRY_COMPARE(button->property("text").toString(), QString("Install"));
        QVERIFY(button->isEnabled());
    }
    void delayedResultsDoNotReplaceAnUnsubmittedSearchDraft() {
        auto source = std::make_shared<catalogue_test::Search>();
        BrowseCatalogue model(
            source, std::make_shared<ContentVisibility>(std::make_shared<catalogue_test::Settings>()));
        model.setContext("fixture", {}, {});
        QTRY_VERIFY(!model.busy());
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1366, 768);
        auto root = page(engine, model, window, 1);
        QVERIFY(root);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *search = item(root.get(), "modSearchInput");
        QVERIFY(search);
        search->forceActiveFocus();
        source->delayed = true;
        QTest::keyClick(&window, Qt::Key_P);
        QTRY_COMPARE(model.filters().value("query").toString(), QString("p"));
        QTest::keyClick(&window, Qt::Key_R);
        QCOMPARE(search->property("text").toString(), QString("pr"));
        source->deliver(source->calls.size() - 1);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(search->property("text").toString(), QString("pr"));
        QTest::keyClick(&window, Qt::Key_O);
        model.configure({{"hideInstalled", true}});
        QCOMPARE(search->property("text").toString(), QString("pro"));
        QTRY_COMPARE(model.filters().value("query").toString(), QString("pro"));
        model.configure({{"query", "restored"}});
        QTRY_COMPARE(search->property("text").toString(), QString("restored"));
    }
    void enlargedGalleryUsesAnOriginalResolutionImageAtEveryZoom() {
#ifndef Q_MOC_RUN
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            R"QML(import QtQuick
import QtQuick.Controls
import "showcase"
Item { GalleryViewer { id:viewer;objectName:"viewer" } function display(urls) { viewer.show(urls,0) } })QML",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/GalleryFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(root);
        QImage original(1800, 1000, QImage::Format_RGB32);
        original.fill(Qt::cyan);
        const auto path = storage.path() + "/original-gallery.png";
        QVERIFY(original.save(path));
        QStringList urls{QUrl::fromLocalFile(path).toString()};
        for (const auto &name : {"second", "third"}) {
            original.fill(name == QString("second") ? Qt::magenta : Qt::green);
            const auto otherPath = storage.path() + '/' + name + "-gallery.png";
            QVERIFY(original.save(otherPath));
            urls << QUrl::fromLocalFile(otherPath).toString();
        }
        QQuickWindow window;
        window.resize(900, 700);
        root->setParentItem(window.contentItem());
        root->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(QMetaObject::invokeMethod(root.get(), "display", Q_ARG(QVariant, urls)));
        QTRY_VERIFY(item(window.contentItem(), "galleryEnlargedImage"));
        auto *picture = item(window.contentItem(), "galleryEnlargedImage");
        QTRY_COMPARE(picture->property("status").toInt(), 1);
        QCOMPARE(picture->property("naturalSize").toSize(), original.size());
        QVERIFY(picture->inherits("QQuickImage"));
        const auto fitted = picture->width();
        auto *viewer = root->findChild<QObject *>("viewer");
        QVERIFY(viewer);
        viewer->setProperty("zoom", 3.);
        QTRY_COMPARE(picture->width(), fitted * 3);
        QCOMPARE(picture->property("naturalSize").toSize(), original.size());
        QCOMPARE(picture->property("source").toUrl(), QUrl::fromLocalFile(path));
        auto *viewport = item(window.contentItem(), "galleryEnlargedViewport");
        QVERIFY(viewport);
        const auto wheel = [&](int delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                               QPoint pixels = {}) {
            const QPointF pos = center(viewport);
            QWheelEvent event(pos, window.mapToGlobal(pos.toPoint()), pixels, QPoint(0, delta), Qt::NoButton,
                              modifiers, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&window, &event);
        };
        wheel(120); // The first-image boundary preserves the existing zoom.
        QCOMPARE(viewer->property("currentIndex").toInt(), 0);
        QCOMPARE(viewer->property("zoom").toReal(), 3.);
        wheel(-120);
        QCOMPARE(viewer->property("currentIndex").toInt(), 1);
        QCOMPARE(viewer->property("zoom").toReal(), 1.);
        QCOMPARE(picture->property("source").toUrl(), QUrl(urls[1]));
        wheel(120, Qt::ControlModifier);
        QCOMPARE(viewer->property("currentIndex").toInt(), 1);
        QVERIFY(qAbs(viewer->property("zoom").toReal() - 1.2) < 0.0001);
        wheel(-120, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(viewer->property("currentIndex").toInt(), 1);
        QVERIFY(qAbs(viewer->property("zoom").toReal() - 1.) < 0.0001);
        wheel(0, Qt::NoModifier, QPoint(0, -40));
        QCOMPARE(viewer->property("currentIndex").toInt(), 2);
        viewer->setProperty("zoom", 3.);
        wheel(-120); // The last-image boundary does not wrap or reset zoom.
        QCOMPARE(viewer->property("currentIndex").toInt(), 2);
        QCOMPARE(viewer->property("zoom").toReal(), 3.);
        wheel(0);
        QCOMPARE(viewer->property("zoom").toReal(), 3.);
        wheel(120);
        QCOMPARE(viewer->property("currentIndex").toInt(), 1);
        QCOMPARE(picture->property("source").toUrl(), QUrl(urls[1]));
#endif
    }
    void initTestCase() {
        const auto fonts = qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/";
        for (const auto &file : {"segoeui.ttf", "seguisb.ttf", "segoeuib.ttf", "seguisym.ttf"})
            QVERIFY(QFontDatabase::addApplicationFont(fonts + file) >= 0);
        QGuiApplication::setFont(QFont("Segoe UI"));
        QQuickStyle::setStyle("Basic");
        qputenv("CONFLUX_STYLE_SETTINGS_DIR", storage.path().toUtf8());
    }
    void init() {
        QTest::failOnWarning(QRegularExpression(
            ".*(recursive rearrange|binding loop|TypeError|ReferenceError|Cannot assign|Unable to assign).*",
            QRegularExpression::CaseInsensitiveOption));
    }
    void controlsStayInsideViewport_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<qreal>("scale");
        QTest::addColumn<int>("filterScale");
        for (const auto &size : {QSize(1920, 1080), QSize(1366, 768), QSize(1280, 720)})
            for (qreal scale : {1., 1.25, 1.5})
                for (int filterScale : {75, 100, 150})
                    QTest::newRow(qPrintable(QString("%1x%2-%3-filter%4")
                                                 .arg(size.width())
                                                 .arg(size.height())
                                                 .arg(scale)
                                                 .arg(filterScale)))
                        << size << scale << filterScale;
    }
    void controlsStayInsideViewport() {
        QFETCH(QSize, size);
        QFETCH(qreal, scale);
        QFETCH(int, filterScale);
        auto source = std::make_shared<catalogue_test::Search>(24000);
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<catalogue_test::Settings>());
        BrowseCatalogue model(source, visibility);
        model.setContext("fixture", {"1.21.1"}, {"neoforge"});
        QTRY_VERIFY(!model.busy());
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(size);
        auto root = page(engine, model, window, scale);
        QVERIFY(root);
        auto *appearance = engine.singletonInstance<Appearance *>("Conflux.Style", "Appearance");
        QVERIFY(appearance);
        QVERIFY(appearance->setValue("filterScale", filterScale));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (int pageNumber : {1, 50, 1200}) {
            model.goToPage(pageNumber);
            QTRY_VERIFY(!model.busy());
            QTest::qWait(40);
            QStringList controls{"browseContentTypes", "browseInstanceSelector", "modSearchInput",
                                 "browseSortSelector", "browseSizeSelector",     "browseSourceSelector",
                                 "browsePageNumber",   "browseRefreshButton",    "browseResults"};
            for (const auto &name : controls) {
                auto *control = item(root.get(), name);
                QVERIFY2(control, qPrintable(name));
                auto bounds = control->mapRectToScene(QRectF(0, 0, control->width(), control->height()));
                QVERIFY2(bounds.left() >= 216 * scale - 1 && bounds.right() <= size.width() + 1 &&
                             bounds.top() >= -1 && bounds.bottom() <= size.height() + 1,
                         qPrintable(name + " " +
                                    QString("%1,%2 %3x%4")
                                        .arg(bounds.x())
                                        .arg(bounds.y())
                                        .arg(bounds.width())
                                        .arg(bounds.height())));
            }
            auto *results = item(root.get(), "browseResults");
            QVERIFY(results->height() >= 100);
            if (auto *panel = item(root.get(), "browseFilterPanel")) {
                auto resultsRight = results->mapToScene(QPointF(results->width(), 0)).x();
                QVERIFY(resultsRight < panel->mapToScene(QPointF()).x());
                const auto panelBounds = panel->mapRectToScene(QRectF(0, 0, panel->width(), panel->height()));
                for (const auto &name : {"browseHideInstalled", "browseShowHidden"}) {
                    auto *control = item(root.get(), name);
                    QVERIFY(control);
                    const auto rect =
                        control->mapRectToScene(QRectF(0, 0, control->width(), control->height()));
                    QVERIFY2(rect.left() >= panelBounds.left() && rect.right() <= panelBounds.right() + 1,
                             qPrintable(QString(name) + " escaped filter panel"));
                }
            } else {
                QVERIFY(item(root.get(), "browseFiltersButton"));
            }
        }
        if (scale == 1.5 && size.width() == 1920 && filterScale == 100)
            window.grabWindow().save(QStringLiteral(CONFLUX_TEST_ARTIFACT_DIR) + "/catalogue-1080-150.png");
    }
    void realHiddenAndInstalledControlsRemainConsistentDuringRequests() {
        auto source = std::make_shared<catalogue_test::Search>(5);
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<catalogue_test::Settings>());
        visibility->setHidden(source->projects[0].toVariant(), true);
        BrowseCatalogue model(source, visibility);
        connect(visibility.get(), &ContentVisibility::changed, &model, &BrowseCatalogue::refresh);
        model.setContext("fixture", {"1.21.1"}, {"neoforge"});
        model.setInstalled(
            {QVariantMap{{"sourceIds", QVariantMap{{"modrinth", "0"}}}, {"category", "mods"}},
             QVariantMap{{"sourceIds", QVariantMap{{"modrinth", "1"}}}, {"category", "mods"}}});
        QTRY_VERIFY(!model.busy());
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1920, 1080);
        auto root = page(engine, model, window, 1);
        QVERIFY(root);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(40);
        auto *hide = item(root.get(), "browseHideInstalled");
        auto *hidden = item(root.get(), "browseShowHidden");
        QVERIFY(hide);
        QVERIFY(hidden);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(hide));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.hideInstalledActive());
        QCOMPARE(model.projects().size(), 3);
        source->delayed = true;
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(hidden));
        QVERIFY(model.busy());
        QVERIFY(!model.hideInstalledActive());
        QVERIFY(!hide->isEnabled());
        const int pending = source->calls.size() - 1;
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(hidden));
        const int last = source->calls.size() - 1;
        QVERIFY(last > pending);
        source->deliver(last);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 3);
        source->deliver(pending);
        QCoreApplication::processEvents();
        QCOMPARE(model.projects().size(), 3);
        source->delayed = false;
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(hidden));
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 1);
        QCOMPARE(model.projects().first().toMap().value("id").toString(), QString("0"));
        QVERIFY(!item(root.get(), "activeHideInstalledFilter"));
    }
    void compactFiltersCloseOnEscapeAndRetainOneFilterInstanceAcrossResize() {
        auto source = std::make_shared<catalogue_test::Search>();
        BrowseCatalogue model(
            source, std::make_shared<ContentVisibility>(std::make_shared<catalogue_test::Settings>()));
        model.setContext("fixture", {}, {});
        QTRY_VERIFY(!model.busy());
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1280, 720);
        auto root = page(engine, model, window, 1.5);
        QVERIFY(root);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(40);
        auto *button = item(root.get(), "browseFiltersButton");
        QVERIFY(button);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(button));
        QTest::qWait(40);
        auto *filters = item(window.contentItem(), "browseFilters");
        QVERIFY(filters);
        auto bounds = filters->mapRectToScene(QRectF(0, 0, filters->width(), filters->height()));
        QVERIFY(bounds.left() >= 0 && bounds.right() <= 1281 && bounds.bottom() <= 721);
        auto *toggle = item(window.contentItem(), "browseHideInstalled");
        QVERIFY(toggle);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(toggle));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.hideInstalledActive());
        QTest::keyClick(&window, Qt::Key_Escape);
        QTest::qWait(30);
        QVERIFY(!item(window.contentItem(), "browseFilters"));
        QVERIFY(button->hasActiveFocus());
        root->setSize(QSizeF(2400, 1400));
        window.resize(2400, 1400);
        QTest::qWait(40);
        QCOMPARE(item(root.get(), "browseFilters"), filters);
        QVERIFY(!item(root.get(), "browseFiltersButton"));
        QVERIFY(item(root.get(), "browseHideInstalled")->property("checked").toBool());
    }
    void changingPagesDuringSmoothScrollingDoesNotOverwriteNewPagePosition() {
        auto source = std::make_shared<catalogue_test::Search>(80);
        BrowseCatalogue model(
            source, std::make_shared<ContentVisibility>(std::make_shared<catalogue_test::Settings>()));
        model.setContext("fixture", {}, {});
        QTRY_VERIFY(!model.busy());
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1366, 768);
        auto root = page(engine, model, window, 1);
        QVERIFY(root);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(40);
        auto *results = item(root.get(), "browseResults");
        QVERIFY(results);
        auto wheel = [&] {
            const QPointF pos = center(results);
            QWheelEvent event(pos, window.mapToGlobal(pos.toPoint()), QPoint(), QPoint(0, -120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&window, &event);
        };
        // First page -> newly fetched page, then back -> cached page, while the old animation is active.
        for (int round = 0; round < 2; ++round) {
            wheel();
            QTRY_VERIFY(results->property("contentY").toReal() > 0);
            QVERIFY(results->property("moving").toBool());
            model.next();
            QTRY_VERIFY(!model.busy());
            QTest::qWait(230);
            QCOMPARE(model.page(), 2);
            QCOMPARE(model.scrollPosition(), qreal(0));
            QCOMPARE(results->property("contentY").toReal(), qreal(0));
            model.previous();
            QTRY_VERIFY(!model.busy());
            QTest::qWait(40);
            QCOMPARE(model.page(), 1);
        }
    }
    void cardsAndActualVersionRowsUseTheSameRecentTimeAndWrapWithinTheirRows_data() {
        QTest::addColumn<int>("width");
        QTest::newRow("narrow") << 640;
        QTest::newRow("wide") << 1100;
    }
    void contentPageProgressivelyReplacesSkeletonAndBuildsOnlySelectedTabs() {
#ifndef Q_MOC_RUN
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            R"QML(
import QtQuick
import "pages"
Item {
    id:root
    property bool loading:true
    property bool galleryLoading:true
    property var images:[]
    property var blocks:[]
    property string selectedTab:"description"
    QtObject { id:model;property var project:({title:"Instant title",sources:[]});property var gallery:root.images;property bool galleryBusy:root.galleryLoading;property bool busy:root.loading;property string notice:"";property var descriptionBlocks:root.blocks;property var changelogVersions:[];property var changelogBlocks:[];property var changelogRelease:({});property string changelogNotice:"";property bool changelogBusy:false;property int changelogIndex:-1;property var changelogModel:null;signal changed();function selectChangelog(release){} function requestChangelog(index,retry){} }
    QtObject { id:browser;property bool ready:true;property bool busy:false;property string notice:"";function installed(release,type){return false} function state(project){return ({locked:false})} }
    QtObject { id:applicationModel;property bool navigationBusy:false;property bool curseforgeEnabled:true;function openWeb(url){} }
    QtObject { id:content;property string notice:"";property int lockRevision:0;function projectLocked(project){return false} function sourceLock(project){return ({})} }
    QtObject { id:filters;property var rows:[];property var options:({channels:[],versions:[],loaders:[]});property var filters:({channels:[],versions:[],loaders:[],source:"all"});function configure(values){} function clear(){} }
    ShowcasePage { anchors.fill:parent;showcase:model;browse:browser;app:applicationModel;contentController:content;releaseFilters:filters;tab:root.selectedTab;onTabRequested:function(key){root.selectedTab=key} }
})QML",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/LoadingFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(root, qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(1080, 720);
        root->setParentItem(window.contentItem());
        root->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(item(root.get(), "descriptionSkeleton"));
        QVERIFY(!root->findChild<QQuickItem *>("showcaseGallery"));
        QVERIFY(!root->findChild<QQuickItem *>("versionChannelFilter"));
        QVERIFY(!root->findChild<QQuickItem *>("changelogView"));
        root->setProperty("blocks",
                          QVariantList{QVariantMap{{"kind", "text"}, {"html", "<p>Progressive body</p>"}}});
        QTRY_VERIFY(item(root.get(), "descriptionTextBlock"));
        QVERIFY(!item(root.get(), "descriptionSkeleton"));
        root->setProperty("selectedTab", "gallery");
        root->setProperty("loading", false);
        QTest::qWait(30);
        QCOMPARE(root->property("selectedTab").toString(),
                 QString("gallery")); // Validation has not finished.
        const auto path = storage.filePath("loading-gallery.png");
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        QVERIFY(image.save(path));
        root->setProperty("images", QStringList{QUrl::fromLocalFile(path).toString()});
        root->setProperty("galleryLoading", false);
        QTRY_VERIFY(item(root.get(), "galleryThumbnail"));
        root->setProperty("selectedTab", "versions");
        QTRY_VERIFY(item(root.get(), "versionChannelFilter"));
        QTRY_VERIFY(!root->findChild<QQuickItem *>("showcaseGallery"));
        root->setProperty("selectedTab", "changelog");
        QTRY_VERIFY(item(root.get(), "changelogView"));
        QTRY_VERIFY(!root->findChild<QQuickItem *>("versionChannelFilter"));
        root->setProperty("selectedTab", "description");
        QTRY_VERIFY(item(root.get(), "descriptionTextBlock"));
        QTRY_VERIFY(!root->findChild<QQuickItem *>("changelogView"));
#endif
    }

    void cardsAndActualVersionRowsUseTheSameRecentTimeAndWrapWithinTheirRows() {
#ifndef Q_MOC_RUN
        QFETCH(int, width);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            R"QML(
import QtQuick
import "pages"
import "components"
Item {
    id:root
    required property var releaseRows
    required property string timestamp
    property var sourceProject: ({title:"Example",updated:timestamp,sources:[]})
    QtObject { id:model;property var project:root.sourceProject;property var gallery:[];property bool busy:false;property string notice:"";property var descriptionBlocks:[];property var changelogVersions:[];property var changelogBlocks:[];property var changelogRelease:({});property string changelogNotice:"";property bool changelogBusy:false;signal changed() }
    QtObject { id:browser;property bool ready:true;property bool busy:false;property string notice:"";function installed(release,type){return false} function state(project){return ({locked:false})} function installVersion(release,project){} }
    QtObject { id:applicationModel;property bool navigationBusy:false;property bool curseforgeEnabled:true;function openWeb(url){} }
    QtObject { id:content;property string notice:"";property int lockRevision:0;function projectLocked(project){return false} function sourceLock(project){return ({})} }
    QtObject { id:filters;property var rows:root.releaseRows;property var options:({channels:[],versions:[],loaders:[]});property var filters:({channels:[],versions:[],loaders:[],source:"all"}) }
    ProjectCard { id:card;objectName:"dateCard";width:parent.width;project:root.sourceProject;installState:({canInstall:false,label:"Install"}) }
    ShowcasePage { x:0;y:card.height+12;width:parent.width;height:parent.height-y;showcase:model;browse:browser;app:applicationModel;contentController:content;releaseFilters:filters;tab:"versions" }
})QML",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/RecentTimeFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        Release release;
        release.provider = "modrinth";
        release.id = "recent";
        release.version = "1.0";
        release.published = QDateTime::currentDateTimeUtc().addSecs(-3 * 3600).toString(Qt::ISODateWithMs);
        release.gameVersions = {"1.21.1"};
        release.loaders = {"neoforge"};
        auto groups = [&] {
            return QVariantList{
                QVariantMap{{"label", "1.0"}, {"variants", QVariantList{release.toVariant()}}}};
        };
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.createWithInitialProperties(
            {{"releaseRows", groups()}, {"timestamp", release.published}})));
        QVERIFY2(root, qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(width, 900);
        root->setParentItem(window.contentItem());
        root->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(item(root.get(), "releasePublished"));
        auto *published = item(root.get(), "releasePublished");
        auto *updated = item(root.get(), "browseUpdated");
        QVERIFY(updated);
        QCOMPARE(updated->property("text").toString(), QString("3 hours ago"));
        QVERIFY(updated->property("hint").toString().endsWith(" · 3 hours ago"));
        QVERIFY(published->property("text").toString().endsWith(" · 3 hours ago"));
        auto *row = published->parentItem()->parentItem()->parentItem();
        const auto bounds =
            published->mapRectToItem(row, QRectF(0, 0, published->width(), published->height()));
        QVERIFY(bounds.left() >= 0 && bounds.right() <= row->width() && bounds.bottom() <= row->height());
        release.provider = "curseforge";
        release.url = "https://example.invalid/release.jar";
        release.sha1 = QString(40, 'a');
        root->setProperty("releaseRows", groups());
        QTRY_VERIFY(item(root.get(), "installReleaseButton"));
        QVERIFY(item(root.get(), "installReleaseButton")->isEnabled());
        // Legacy providers may supply only a day: never infer midnight as a release time.
        release.published = QDate::currentDate().toString(Qt::ISODate);
        root->setProperty("releaseRows", groups());
        root->setProperty("timestamp", release.published);
        QTRY_VERIFY(!item(root.get(), "releasePublished")->property("text").toString().contains("ago"));
        QVERIFY(!updated->property("text").toString().contains("ago"));
#endif
    }
    void loaderPillsHaveTheSameSelectionBehaviorAsTheirCheckboxes() {
        auto source = std::make_shared<catalogue_test::Search>();
        BrowseCatalogue model(
            source, std::make_shared<ContentVisibility>(std::make_shared<catalogue_test::Settings>()));
        model.setContext("fixture", {}, {"neoforge"});
        QTRY_VERIFY(!model.busy());
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1920, 1080);
        auto root = page(engine, model, window, 1);
        QVERIFY(root);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(40);
        auto *fabric = item(root.get(), "browseLoader_fabric");
        QVERIFY(fabric);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(fabric));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.filters().value("loaders").toStringList().contains("fabric"));
        QVERIFY(model.filters().value("loaders").toStringList().contains("neoforge"));
        const int requests = source->calls.size();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(fabric));
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.filters().value("loaders").toStringList(), QStringList{"neoforge"});
        QCOMPARE(source->calls.size(), requests + 1);
    }
    void realAppearancePreviewImplementsTheSameEffectiveVisibilityContract() {
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/settings/AppearancePreview.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> preview(
            qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"section", "filters"}})));
        QVERIFY(preview);
        QQuickWindow window;
        window.resize(1100, 700);
        preview->setParentItem(window.contentItem());
        preview->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(40);
        auto *hide = item(preview.get(), "browseHideInstalled");
        auto *hidden = item(preview.get(), "browseShowHidden");
        QVERIFY(hide);
        QVERIFY(hidden);
        QVERIFY(!hide->property("checked").toBool());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(hide));
        QVERIFY(hide->property("checked").toBool());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(hidden));
        QVERIFY(!hide->isEnabled());
        QVERIFY(!hide->property("checked").toBool());
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(hidden));
        QVERIFY(hide->isEnabled());
        QVERIFY(hide->property("checked").toBool());
    }
    void visibleDiscoverIconsHaveExplicitDefinitionsInsteadOfFileFallbacks() {
        auto source = std::make_shared<catalogue_test::Search>();
        BrowseCatalogue model(
            source, std::make_shared<ContentVisibility>(std::make_shared<catalogue_test::Settings>()));
        model.setContext("fixture", {}, {"neoforge"});
        QTRY_VERIFY(!model.busy());
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(1280, 720);
        auto root = page(engine, model, window, 1.5);
        QVERIFY(root);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(40);
        QVERIFY(item(root.get(), "browseFiltersButton"));
        int checked = 0;
        std::function<void(QQuickItem *)> verify = [&](QQuickItem *node) {
            if (node->isVisible() && QByteArray(node->metaObject()->className()).startsWith("Icon_QMLTYPE")) {
                const auto name = node->property("name");
                QVariant defined;
                QVERIFY(QMetaObject::invokeMethod(root.get(), "hasGlyph", Q_RETURN_ARG(QVariant, defined),
                                                  Q_ARG(QVariant, name)));
                QVERIFY2(defined.toBool(), qPrintable("Missing explicit icon: " + name.toString()));
                ++checked;
            }
            for (auto *child : node->childItems())
                verify(child);
        };
        verify(root.get());
        QVERIFY(checked > 10);
    }
    void viewportCornersRemainRounded_data() {
        QTest::addColumn<qreal>("scale");
        for (qreal scale : {1., 1.25, 1.5})
            QTest::newRow(qPrintable(QString::number(scale))) << scale;
    }
    void viewportCornersRemainRounded() {
#ifndef Q_MOC_RUN
        QFETCH(qreal, scale);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        const QByteArray source =
            R"QML(import QtQuick
import ")QML" +
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/components").toString().toUtf8() +
            R"QML("
Rectangle { width:400; height:300; color:"#102030"
    property alias list: list
    ContentListView { id:list; objectName:"list"; x:20;y:20;width:300;height:180; outsideColor:"#102030"; cornerRadius:20; spacing:8; model:20; delegate:Rectangle { required property int index; width:list.viewportWidth; height:115; color:"#df4321" } }
})QML";
        component.setData(source, QUrl("file:///corner-fixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(root);
        QQuickWindow window;
        if (qEnvironmentVariableIsSet("CONFLUX_CATALOGUE_NATIVE_RENDER")) {
            const auto screens = QGuiApplication::screens();
            auto screen = std::find_if(screens.cbegin(), screens.cend(),
                                       [](QScreen *s) { return s != QGuiApplication::primaryScreen(); });
            QVERIFY2(screen != screens.cend(), "Native validation requires a secondary monitor");
            window.setFlags(Qt::Tool | Qt::WindowDoesNotAcceptFocus);
            window.setScreen(*screen);
            window.setPosition((*screen)->availableGeometry().topLeft() + QPoint(40, 40));
        }
        window.resize(qRound(400 * scale), qRound(300 * scale));
        root->setParentItem(window.contentItem());
        root->setScale(scale);
        root->setTransformOrigin(QQuickItem::TopLeft);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        if (qEnvironmentVariableIsSet("CONFLUX_CATALOGUE_NATIVE_RENDER")) {
            const auto api = window.rendererInterface()->graphicsApi();
            qInfo() << "Native renderer" << api << "screen" << window.screen()->name();
            QVERIFY(api != QSGRendererInterface::Software && api != QSGRendererInterface::Unknown);
        }
        auto *list = item(root.get(), "list");
        QVERIFY(list);
        for (int step = 0; step < 5; ++step) {
            list->setProperty("contentY", step * 107);
            if (step == 1)
                list->setHeight(207);
            if (step == 2) {
                root->setVisible(false);
                QCoreApplication::processEvents();
                root->setVisible(true);
            }
            if (step == 3)
                list->setProperty("model", 30);
            if (step == 4)
                list->setWidth(271);
            QTest::qWait(50);
            auto image = window.grabWindow();
            QVERIFY(!image.isNull());
            auto *viewport = item(list, "contentListViewport");
            QVERIFY(viewport);
            const QRectF rect = viewport->mapRectToScene(QRectF(0, 0, viewport->width(), viewport->height()));
            const qreal ratio = image.width() / qreal(window.width());
            for (auto point : {rect.topLeft(), rect.topRight() + QPointF(-1, 0),
                               rect.bottomLeft() + QPointF(0, -1), rect.bottomRight() + QPointF(-1, -1)}) {
                QCOMPARE(image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio)),
                         QColor("#102030"));
            }
        }
#endif
    }
};
QTEST_MAIN(CatalogueInteractionTests)
#include "CatalogueInteractionTests.moc"
