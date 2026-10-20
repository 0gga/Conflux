#include "ContentFixtures.h"
#include "features/config/ConfigLibrary.h"
#include "features/content/ContentActions.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/ConfigOwnershipStore.h"
#include <QBuffer>
#include <QImage>
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
