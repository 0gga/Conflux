#include <QDir>
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>
#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)

class LibraryRowTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
#ifdef Q_OS_WIN
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/segoeui.ttf");
#endif
    }
    void columnsRemainAlignedAcrossProvidersAndWindowWidths() {
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/components/LibraryRow.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window; window.setHeight(300); window.setColor(QColor("#111613"));
        QList<QQuickItem*> rows;
        const QStringList titles{"Fabric Perfection", "Create: Takeoff", "A very long modpack title that should be elided instead of moving the other columns"};
        const QStringList sources{"modrinth", "conflux", "curseforge"};
        for (int index = 0; index < titles.size(); ++index) {
            auto* row = qobject_cast<QQuickItem*>(component.create()); QVERIFY(row);
            row->setParentItem(window.contentItem()); row->setParent(&window); row->setY(8 + index * 94); row->setX(8); row->setHeight(84);
            row->setProperty("titleText", titles.at(index)); row->setProperty("source", sources.at(index));
            row->setProperty("detailText", index == 0 ? "Minecraft version unknown · 61 mod files" : "1.21.1 · neoforge-21.1.234 · 310 mod files");
            rows << row;
        }
        window.show();
        for (const int width : {720, 1100, 2000}) {
            window.setWidth(width + 16);
            for (auto* row : rows) row->setWidth(width);
            QTest::qWait(30);
            for (auto* row : rows) {
                auto* title = row->findChild<QQuickItem*>("libraryRowTitle");
                auto* detail = row->findChild<QQuickItem*>("libraryRowDetails");
                auto* badge = row->findChild<QQuickItem*>("libraryRowSourceColumn");
                auto* action = row->findChild<QQuickItem*>("libraryRowAction");
                auto* explore = row->findChild<QQuickItem*>("libraryRowExplore");
                auto* hide = row->findChild<QQuickItem*>("libraryRowHide");
                QVERIFY(title && detail && badge && action && explore && hide);
                QCOMPARE(title->mapToItem(row, QPointF{}).x(), 80.0); QCOMPARE(detail->mapToItem(row, QPointF{}).x(), 80.0);
                QCOMPARE(hide->x() + hide->width(), width - 16.0);
                QVERIFY(explore->x() + explore->width() < action->x());
                QVERIFY(action->x() + action->width() < hide->x());
                QCOMPARE(badge->x() + badge->width(), explore->x() - 20.0);
                QVERIFY(title->width() > 0); QVERIFY(title->mapToItem(row, QPointF{}).x() + title->width() <= badge->x() - 20.0);
            }
            const auto artifacts = qEnvironmentVariable("CONFLUX_UI_ARTIFACTS");
            if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); QVERIFY(window.grabWindow().save(QDir(artifacts).filePath("library-rows-" + QString::number(width) + ".png"))); }
        }
    }
};
QTEST_MAIN(LibraryRowTests)
#include "LibraryRowTests.moc"
