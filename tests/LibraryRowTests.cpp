#include <QDir>
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)

class LibraryRowTests : public QObject {
    Q_OBJECT
    static QQuickItem *visibleItem(QQuickItem *root, const QString &name) {
        if (root->objectName() == name && root->isVisible())
            return root;
        for (auto *child : root->childItems())
            if (auto *found = visibleItem(child, name))
                return found;
        return nullptr;
    }
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
#ifdef Q_OS_WIN
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR", "C:/Windows") +
                                          "/Fonts/segoeui.ttf");
#endif
    }
    void columnsRemainAlignedAcrossProvidersAndWindowWidths() {
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/LibraryRow.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window;
        window.setHeight(300);
        window.setColor(QColor("#111613"));
        QList<QQuickItem *> rows;
        const QStringList titles{
            "Fabric Perfection", "Create: Takeoff",
            "A very long modpack title that should be elided instead of moving the other columns"};
        const QStringList sources{"modrinth", "conflux", "curseforge"};
        for (int index = 0; index < titles.size(); ++index) {
            auto *row = qobject_cast<QQuickItem *>(component.create());
            QVERIFY(row);
            row->setParentItem(window.contentItem());
            row->setParent(&window);
            row->setY(8 + index * 94);
            row->setX(8);
            row->setHeight(84);
            row->setProperty("titleText", titles.at(index));
            row->setProperty("source", sources.at(index));
            row->setProperty("detailText", "310 mods");
            row->setProperty("minecraft", index == 0 ? "" : "1.21.1");
            row->setProperty("loader", index == 0 ? "fabric" : "neoforge");
            rows << row;
        }
        window.show();
        for (const int width : {720, 1100, 2000}) {
            window.setWidth(width + 16);
            for (auto *row : rows)
                row->setWidth(width);
            QTest::qWait(30);
            for (auto *row : rows) {
                auto *title = row->findChild<QQuickItem *>("libraryRowTitle");
                auto *detail = row->findChild<QQuickItem *>("libraryRowDetails");
                auto *badge = row->findChild<QQuickItem *>("libraryRowSourceColumn");
                auto *explore = row->findChild<QQuickItem *>("libraryRowExplore");
                auto *hide = row->findChild<QQuickItem *>("libraryRowMore");
                auto *compatibility = row->findChild<QQuickItem *>("libraryRowCompatibility");
                auto *minecraft = row->findChild<QQuickItem *>("compatibilityMinecraft");
                auto *loader = row->findChild<QQuickItem *>("compatibilityLoader");
                QVERIFY(title && detail && badge && explore && hide && compatibility && minecraft && loader);
                QCOMPARE(title->mapToItem(row, QPointF{}).x(), 80.0);
                QCOMPARE(compatibility->mapToItem(row, QPointF{}).x(), 80.0);
                QCOMPARE(loader->x(), minecraft->width() + 6.0);
                QCOMPARE(loader->y(), minecraft->y());
                QCOMPARE(loader->height(), minecraft->height());
                QVERIFY(compatibility->mapToItem(row, QPointF{}).x() + compatibility->width() <=
                        badge->x() - 20.0);
                QVERIFY(detail->mapToItem(row, QPointF{}).x() >=
                        compatibility->mapToItem(row, QPointF{}).x() + compatibility->width());
                QCOMPARE(hide->x() + hide->width(), width - 16.0);
                QVERIFY(explore->x() + explore->width() < hide->x());
                QCOMPARE(badge->x() + badge->width(), explore->x() - 20.0);
                QVERIFY(title->width() > 0);
                QVERIFY(title->mapToItem(row, QPointF{}).x() + title->width() <= badge->x() - 20.0);
            }
            const auto artifacts = qEnvironmentVariable("CONFLUX_UI_ARTIFACTS");
            if (!artifacts.isEmpty()) {
                QDir().mkpath(artifacts);
                QVERIFY(window.grabWindow().save(
                    QDir(artifacts).filePath("library-rows-" + QString::number(width) + ".png")));
            }
        }
    }
    void libraryEdgesAndInstanceMenuStayConsistent() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            R"QML(import QtQuick
import Conflux.Style
import "pages"
Item {
 id:root
 Rectangle { anchors.fill:parent;color:Theme.colors.background }
 property string openedFolder:""
 property int opened:0
 QtObject { id:applicationModel;property var libraryPacks:[{title:"Create: Takeoff",path:"fixture",provider:"modrinth",gameVersion:"1.21.1",loader:"neoforge",modCount:310,hidden:false}];property int visiblePackCount:libraryPacks.filter(function(p){return !p.hidden}).length;property int hiddenPackCount:libraryPacks.length-visiblePackCount;property bool libraryBusy:false;property bool repositoryBusy:false;function refreshLibrary(){} function openFolder(path){root.openedFolder=path} function setPackHidden(path,hidden){const rows=libraryPacks.slice();rows[0]=Object.assign({},rows[0],{hidden:hidden});libraryPacks=rows} }
 LibraryPage { id:page;objectName:"libraryPage";anchors.fill:parent;app:applicationModel;onPackRequested:function(path,explore){root.opened++} }
})QML",
            QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/LibraryFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(root, qPrintable(component.errorString()));
        QQuickWindow window;
        root->setParentItem(window.contentItem());
        window.resize(1080, 720);
        root->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (int width : {540, 720, 1080, 1600}) {
            window.setWidth(width);
            root->setWidth(width);
            QTest::qWait(30);
            auto *more = visibleItem(root.get(), "libraryRowMore"),
                 *search = visibleItem(root.get(), "librarySearch"),
                 *refresh = visibleItem(root.get(), "libraryRefresh");
            QVERIFY(more && search && refresh && visibleItem(root.get(), "libraryCreate"));
            auto *row = more->parentItem();
            const auto edge = row->mapToItem(root.get(), QPointF(row->width(), 0)).x();
            QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                             "/../artifacts/library-page-" + QString::number(width) +
                                             ".png"));
            QVERIFY(qAbs(edge - search->mapToItem(root.get(), QPointF(search->width(), 0)).x()) < 1);
            QVERIFY(qAbs(edge - refresh->mapToItem(root.get(), QPointF(refresh->width(), 0)).x()) < 1);
            for (const auto &source : {"all", "conflux", "modrinth", "curseforge"}) {
                auto *choice = visibleItem(root.get(), QString("librarySource-") + source);
                QVERIFY(choice);
                const auto *content = qvariant_cast<QQuickItem *>(choice->property("contentItem"));
                QVERIFY(content);
                QVERIFY(content->width() >= content->implicitWidth());
            }
            QVERIFY(edge <= width - 31);
        }
        auto *more = visibleItem(root.get(), "libraryRowMore");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          more->mapToScene(QPointF(20, 20)).toPoint());
        QTRY_VERIFY(visibleItem(window.contentItem(), "libraryRowFolder"));
        QCOMPARE(root->property("opened").toInt(), 0);
        auto *folder = visibleItem(window.contentItem(), "libraryRowFolder");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          folder->mapToScene(QPointF(40, 20)).toPoint());
        QTRY_COMPARE(root->property("openedFolder").toString(), QString("fixture"));
        QCOMPARE(root->property("opened").toInt(), 0);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          more->mapToScene(QPointF(20, 20)).toPoint());
        QTRY_VERIFY(visibleItem(window.contentItem(), "libraryRowHide"));
        auto *hide = visibleItem(window.contentItem(), "libraryRowHide");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          hide->mapToScene(QPointF(40, 20)).toPoint());
        QTRY_VERIFY(!visibleItem(root.get(), "libraryRowMore"));
        auto *page = root->findChild<QQuickItem *>("libraryPage");
        QVERIFY(page);
        page->setProperty("hiddenTab", true);
        QTRY_VERIFY(visibleItem(root.get(), "libraryRowMore"));
        more = visibleItem(root.get(), "libraryRowMore");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          more->mapToScene(QPointF(20, 20)).toPoint());
        QTRY_VERIFY(visibleItem(window.contentItem(), "libraryRowHide"));
        QCOMPARE(visibleItem(window.contentItem(), "libraryRowHide")->property("text").toString(),
                 QString("Restore modpack"));
    }
};
QTEST_MAIN(LibraryRowTests)
#include "LibraryRowTests.moc"
