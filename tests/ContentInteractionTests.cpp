#include "ContentFixtures.h"
#include "features/content/ContentActions.h"
#include "features/preferences/ContentPreferences.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)
class ContentInteractionTests : public QObject {
    Q_OBJECT
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
        auto *row = findItem(window.contentItem(), "contentRow");
        QVERIFY(row);
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
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          more->mapToScene(QPointF(more->width() / 2, more->height() / 2)).toPoint());
        QTRY_VERIFY(findItem(window.contentItem(), "copyContentLink"));
        auto *copy = findItem(window.contentItem(), "copyContentLink");
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
