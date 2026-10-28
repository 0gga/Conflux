#include "TestPerformance.h"
#include "features/navigation/CommandPalette.h"
#include "features/navigation/NavigationHistory.h"
#include <QElapsedTimer>
#include <QtTest>
#include <memory>
using namespace studio;
namespace {
QVariantMap content(const QString &title, const QString &file) {
    return {{"title", title},
            {"relativePath", "mods/" + file + ".jar"},
            {"category", "mods"},
            {"version", "1.21.1"},
            {"author", "Separate Author"}};
}
QString key(const CommandPalette &model, int index = 0) {
    return model.rows().at(index).toMap().value("key").toString();
}
} // namespace
class CommandPaletteTests final : public QObject {
    Q_OBJECT
  private slots:
    void literalRankingUsesPreparedNamesAndMetadata_data() {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("titles");
        QTest::newRow("exact-first") << QString("compat")
                                     << QStringList{"Compat", "Compat Helper", "Compatibility"};
        QTest::newRow("word-inclusion") << QString("aero") << QStringList{"Aeronautics"};
        QTest::newRow("no-letter-rumble") << QString("aerontics") << QStringList{};
        QTest::newRow("version-punctuation")
            << QString("1211")
            << QStringList{"Aeronautics", "Compact", "Compat", "Compat Helper", "Compatibility"};
        QTest::newRow("author") << QString("separate author")
                                << QStringList{"Aeronautics", "Compact", "Compat", "Compat Helper",
                                               "Compatibility"};
        QTest::newRow("accent") << QString("cafe") << QStringList{"Café"};
    }
    void literalRankingUsesPreparedNamesAndMetadata() {
        QFETCH(QString, query);
        QFETCH(QStringList, titles);
        CommandPalette model;
        model.setContext({{"path", "instance"}},
                         {content("Compatibility", "one"), content("Compact", "two"),
                          content("Compat Helper", "three"), content("Compat", "four"),
                          content("Aeronautics", "five"),
                          QVariantMap{{"title", QString::fromUtf8("Café")},
                                      {"relativePath", "mods/cafe.jar"},
                                      {"category", "mods"}}},
                         {});
        model.open();
        model.setQuery(query);
        QStringList actual;
        for (const auto &value : model.rows())
            actual << value.toMap().value("title").toString();
        QCOMPARE(actual, titles);
    }
    void hiddenAndDuplicateInstancesAndInvalidPathsStayOut() {
        CommandPalette model;
        model.setLibraryPacks({QVariantMap{{"path", "first"}, {"title", "Visible"}},
                               QVariantMap{{"path", "first"}, {"title", "Duplicate"}},
                               QVariantMap{{"path", "hidden"}, {"title", "Secret"}, {"hidden", true}}});
        model.setContext({{"path", "instance"}},
                         {content("Safe", "safe"),
                          QVariantMap{{"title", "Escape"}, {"relativePath", "../outside.jar"}},
                          QVariantMap{{"title", "Absolute"}, {"relativePath", "C:/outside.jar"}}},
                         {});
        model.open();
        model.setQuery("visible");
        QCOMPARE(model.rows().size(), 1);
        model.setQuery("secret");
        QVERIFY(model.rows().isEmpty());
        model.setQuery("duplicate");
        QVERIFY(model.rows().isEmpty());
        model.setQuery("escape");
        QVERIFY(model.rows().isEmpty());
        model.setQuery("absolute");
        QVERIFY(model.rows().isEmpty());
        model.setQuery("safe");
        QCOMPARE(model.rows().size(), 1);
    }
    void routesKeepDistinctFilesAndConfigFolderIntent() {
        CommandPalette model;
        QVariantList routes;
        model.setNavigator([&](QVariantMap route) {
            routes << route;
            return true;
        });
        model.setContext({{"path", "original"}}, {content("Same", "one"), content("Same", "two")},
                         {QVariantMap{{"title", "Config"},
                                      {"relativePath", "config/client.toml"},
                                      {"category", "configs"},
                                      {"editable", true}},
                          QVariantMap{{"title", "Opaque"},
                                      {"relativePath", "config/opaque.bin"},
                                      {"category", "configs"}}});
        model.setFolderEntries(
            {QVariantMap{{"title", "Folder"}, {"relativePath", "logs"}, {"directory", true}}});
        model.open();
        model.setQuery("same");
        QCOMPARE(model.rows().size(), 2);
        const auto first = key(model), second = key(model, 1);
        QVERIFY(first != second);
        QVERIFY(model.activate(second));
        QCOMPARE(routes.last().toMap().value("packPath").toString(), QString("original"));
        QCOMPARE(routes.last().toMap().value("modPath").toString(), QString("mods/two.jar"));
        QVERIFY(!model.visible());
        QVERIFY(!model.activate(first));
        model.open();
        model.setQuery("config");
        const auto config = key(model);
        QVERIFY(model.activate(config));
        QCOMPARE(routes.last().toMap().value("configPath").toString(), QString("config/client.toml"));
        model.open();
        model.setQuery("opaque");
        QVERIFY(model.activate(key(model)));
        QCOMPARE(routes.last().toMap().value("section").toString(), QString("files"));
        QCOMPARE(routes.last().toMap().value("folder").toString(), QString("config"));
        model.open();
        model.setQuery("folder");
        QVERIFY(model.activate(key(model)));
        QCOMPARE(routes.last().toMap().value("folder").toString(), QString("logs"));
    }
    void contextChangesInvalidateOldSelectionsAndKeepQuery() {
        CommandPalette model;
        int routes = 0;
        model.setNavigator([&](auto) {
            ++routes;
            return true;
        });
        model.setContext({{"path", "old"}}, {content("Wanted", "old")}, {});
        model.open();
        model.setQuery("wanted");
        const auto stale = key(model);
        model.setContext({{"path", "new"}}, {content("Wanted", "new")}, {});
        QVERIFY(!model.activate(stale));
        QCOMPARE(model.query(), QString("wanted"));
        QCOMPARE(routes, 0);
        QVERIFY(model.activate(key(model)));
        QCOMPARE(routes, 1);
    }
    void commandRegistryUsesExistingUnsavedNavigationGuard() {
        NavigationHistory history;
        history.setLeaveGuard([] { return false; });
        CommandPalette model;
        model.setNavigator([&](auto route) { return history.go(route); });
        QCOMPARE(model.primaryNavigation().size(), 2);
        QVERIFY(!model.navigate("content"));
        QVERIFY(!model.navigate("unknown"));
        model.open();
        model.setQuery("settings");
        QVERIFY(!model.activate(key(model)));
        QVERIFY(!model.visible());
        QVERIFY(history.hasPending());
        QCOMPARE(history.route().value("page").toString(), QString("library"));
        history.setLeaveGuard({});
        history.resumePending();
        QCOMPARE(history.route().value("page").toString(), QString("settings"));
        model.setContext({{"path", "instance"}}, {}, {});
        QVERIFY(model.navigate("configs"));
        QCOMPARE(history.route().value("section").toString(), QString("configs"));
        QCOMPARE(history.route().value("packPath").toString(), QString("instance"));
    }
    void queryPublicationCanDisposeTheOwner() {
        auto model = std::make_unique<CommandPalette>();
        auto *original = model.get();
        connect(original, &CommandPalette::queryChanged, this, [&] { model.reset(); });
        original->setQuery("dispose");
        QVERIFY(!model);
    }
    void selectedIdentitySurvivesMetadataAndNeverRetargetsOnRemoval() {
        CommandPalette model;
        QVariantList rows{content("Aero", "one"), content("Aeronautics", "two")};
        model.setContext({{"path", "instance"}}, rows, {});
        model.open();
        model.setQuery("aero");
        model.moveSelection(1);
        const auto selected = key(model, 1);
        QCOMPARE(model.selectedKey(), selected);
        auto enriched = rows[1].toMap();
        enriched["author"] = "new metadata";
        rows[1] = enriched;
        model.setContext({{"path", "instance"}}, rows, {});
        QCoreApplication::processEvents();
        QCOMPARE(model.selectedKey(), selected);
        rows.removeLast();
        model.setContext({{"path", "instance"}}, rows, {});
        QCoreApplication::processEvents();
        QCOMPARE(model.selectedKey(), selected);
        QVERIFY(!model.activate(model.selectedKey()));
        QVERIFY(model.visible());
        QVERIFY(!model.select("unknown"));
        model.moveSelection(1);
        QCOMPARE(model.selectedKey(), key(model));
        model.moveSelection(-100);
        QCOMPARE(model.selectedKey(), key(model));
        model.setQuery("settings");
        QCOMPARE(model.selectedKey(), QString("command:settings"));
        model.dismiss();
        QVERIFY(!model.select("command:settings"));
        model.open();
        QCOMPARE(model.selectedKey(), key(model));
    }
    void closedPaletteDoesNotRebuildForBackgroundMetadataBatches() {
        CommandPalette model;
        QSignalSpy published(&model, &CommandPalette::changed);
        QVariantList rows;
        for (int i = 0; i < 20; ++i) {
            rows << content("Batch " + QString::number(i), QString::number(i));
            model.setContext({{"path", "instance"}}, rows, {});
            QCoreApplication::processEvents();
        }
        QCOMPARE(published.size(), 0);
        QVERIFY(model.rows().isEmpty());
        model.open();
        model.setQuery("batch");
        QCOMPARE(model.rows().size(), 20);
        published.clear();
        model.setContext({{"path", "new-instance"}}, {}, {});
        model.dismiss();
        QCoreApplication::processEvents();
        QCOMPARE(published.size(), 1);
        model.open();
        model.setQuery("batch");
        QVERIFY(model.rows().isEmpty());
    }
    void openingPublicationCanDisposeTheOwner() {
        auto model = std::make_unique<CommandPalette>();
        auto *original = model.get();
        connect(original, &CommandPalette::changed, this, [&] { model.reset(); });
        original->open();
        QVERIFY(!model);
    }
    void hardLimitPreservesCommandsAndStopsExcessEntries() {
        CommandPalette model;
        QVariantList rows;
        for (int i = 0; i < 20010; ++i)
            rows << content("Limit " + QString::number(i), QString::number(i));
        model.setContext({{"path", "instance"}}, rows, {});
        model.open();
        QVERIFY(model.rows().size() <= 50);
        model.setQuery("Limit 19991");
        QCOMPARE(model.rows().size(), 1);
        model.setQuery("Limit 20009");
        QVERIFY(model.rows().isEmpty());
        model.setQuery("Settings");
        QCOMPARE(model.rows().first().toMap().value("key").toString(), QString("command:settings"));
    }
    void indexedQueriesAreBoundedAndDoNotResetTheModel() {
        CommandPalette model;
        QVariantList rows;
        for (int i = 0; i < 10000; ++i)
            rows << content("Item " + QString::number(i), QString::number(i));
        model.setContext({{"path", "instance"}}, rows, {});
        QSignalSpy resets(model.model(), &QAbstractItemModel::modelReset);
        QElapsedTimer timer;
        timer.start();
        model.open();
        const auto prepare = timer.elapsed();
        timer.restart();
        for (int i = 0; i < 100; ++i) {
            model.setQuery("Item " + QString::number(i));
            QVERIFY(model.rows().size() <= 50);
        }
        const auto queries = timer.elapsed();
        qInfo() << "10000 local entries prepare/100 queries ms" << prepare << queries;
        QVERIFY(test_performance::releaseBudgetMet(prepare, 500));
        QVERIFY(test_performance::releaseBudgetMet(queries, 500));
        QCOMPARE(resets.size(), 0);
        model.setQuery(QString(1000, 'x'));
        QCOMPARE(model.query().size(), 256);
    }
};
QTEST_GUILESS_MAIN(CommandPaletteTests)
#include "CommandPaletteTests.moc"
