#include "DesktopSession.h"
#include "features/config/ConfigLibrary.h"
#include "features/content/ConfigAssociation.h"
#include "platform/ConfigOwnershipStore.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
using namespace studio;
class FeatureTests : public QObject {
    Q_OBJECT
    static void write(const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
  private slots:
    void globalProjectLinksOpenEvenWhenAnotherInstanceIsLoaded() {
        QTemporaryDir temp;
        DesktopSession session(nullptr, temp.path() + "/settings.ini", temp.path() + "/backups");
        session.setPack({{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}});
        QTRY_VERIFY(!session.workspace()->busy());
        QVERIFY(session.navigation()->go(
            {{"page", "showcase"},
             {"project", QVariantMap{{"provider", "modrinth"}, {"id", "example"}, {"type", "mod"}}}}));
        QCOMPARE(session.showcase()->requestKey(), QString("modrinth:example"));
    }
    void authorNavigationIsDistinctButShowcaseTabChangesStayWithinThePage() {
        NavigationHistory history;
        const QVariantMap browse{{"page", "discover"}, {"packPath", "instance"}};
        QVERIFY(history.go(browse));
        const QVariantMap author{{"page", "discover"},
                                 {"packPath", "instance"},
                                 {"author", QVariantMap{{"ids", QVariantMap{{"modrinth", "exact"}}}}}};
        QVERIFY(history.go(author));
        history.back();
        QVERIFY(!history.route().contains("author"));
        history.forward();
        QVERIFY(history.route().contains("author"));
        QVariantMap project{
            {"page", "showcase"}, {"packPath", "instance"}, {"modPath", "resourcepacks/a.zip"}};
        QVERIFY(history.go(project));
        project["showcaseTab"] = "versions";
        QVERIFY(history.go(project));
        history.back();
        QVERIFY(history.route().contains("author"));
    }
    void historyAndUnsavedGuard() {
        NavigationHistory history;
        QVERIFY(!history.canBack());
        QVERIFY(history.go({{"page", "workspace"}, {"packPath", "a"}, {"section", "configs"}}));
        bool dirty = true;
        history.setLeaveGuard([&] { return !dirty; });
        QSignalSpy blocked(&history, &NavigationHistory::blocked);
        history.back();
        QCOMPARE(blocked.count(), 1);
        QCOMPARE(history.route().value("page").toString(), QString("workspace"));
        QVERIFY(history.hasPending());
        dirty = false;
        history.resumePending();
        QCOMPARE(history.route().value("page").toString(), QString("library"));
        history.forward();
        QCOMPARE(history.route().value("packPath").toString(), QString("a"));
        history.back();
        history.go({{"page", "settings"}});
        QVERIFY(!history.canForward());
    }
    void tabsReplaceCurrentPageWithoutAddingHistory() {
        NavigationHistory history;
        history.go({{"page", "workspace"}, {"packPath", "a"}, {"section", "content"}});
        history.go({{"page", "workspace"}, {"packPath", "a"}, {"section", "configs"}});
        history.go({{"page", "workspace"}, {"packPath", "a"}, {"section", "worlds"}});
        history.back();
        QCOMPARE(history.route().value("page").toString(), QString("library"));
        history.forward();
        QCOMPARE(history.route().value("section").toString(), QString("worlds"));
        history.go({{"page", "showcase"}, {"packPath", "a"}, {"modPath", "mods/a.jar"}});
        history.go({{"page", "showcase"},
                    {"packPath", "a"},
                    {"modPath", "mods/a.jar"},
                    {"showcaseTab", "versions"}});
        history.back();
        QCOMPARE(history.route().value("page").toString(), QString("workspace"));
        history.go({{"page", "workspace"}, {"packPath", "b"}});
        history.back();
        QCOMPARE(history.route().value("packPath").toString(), QString("a"));
        history.go({{"page", "settings"}, {"packPath", "a"}, {"settingsTab", "appearance"}});
        history.go({{"page", "settings"}, {"settingsTab", "warnings"}});
        history.back();
        QCOMPARE(history.route().value("page").toString(), QString("workspace"));
    }
    void explicitConfigOwnerPersistsAndMissingModNeverEnablesEditing() {
        QTemporaryDir temp;
        const auto store = std::make_shared<SettingsConfigOwnershipStore>(temp.path() + "/owners.ini");
        ConfigLibrary configs(store);
        QVariantList files{QVariantMap{{"category", "mods"}, {"modId", "example"}, {"title", "Example"}},
                           QVariantMap{{"category", "configs"},
                                       {"relativePath", "config/odd.json"},
                                       {"fileName", "odd.json"}}};
        configs.setContext({{"path", "pack"}}, files);
        QVERIFY(!configs.file("config/odd.json").value("editable").toBool());
        QVERIFY(!configs.identify("config/odd.json", "unknown"));
        QVERIFY(configs.identify("config/odd.json", "example"));
        QVERIFY(configs.file("config/odd.json").value("editable").toBool());
        ConfigLibrary reopened(std::make_shared<SettingsConfigOwnershipStore>(temp.path() + "/owners.ini"));
        reopened.setContext({{"path", "pack"}}, files);
        QCOMPARE(reopened.file("config/odd.json").value("association").toString(),
                 QString("Mod selected by you"));
        reopened.setContext({{"path", "pack"}}, {files.last()});
        QVERIFY(!reopened.file("config/odd.json").value("editable").toBool());
    }
    void mouseHistoryButtons() {
        NavigationHistory history;
        NavigationInput input(history);
        QQuickWindow window;
        input.attach(window);
        history.go({{"page", "discover"}});
        QMouseEvent back(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10), Qt::BackButton,
                         Qt::BackButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &back);
        QCOMPARE(history.route().value("page").toString(), QString("library"));
        QMouseEvent forward(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10), Qt::ForwardButton,
                            Qt::ForwardButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &forward);
        QCOMPARE(history.route().value("page").toString(), QString("discover"));
    }
    void warningPreferencesPersist() {
        QTemporaryDir temp;
        const auto file = temp.path() + "/prefs.ini";
        {
            WarningPreferences model(std::make_shared<SettingsWarningPreferenceStore>(file));
            model.setEnabled("orphan", false);
            QVERIFY(!model.enabledCategories().contains("orphan"));
            QVERIFY(model.enabledCategories().contains("loader"));
        }
        WarningPreferences reopened(std::make_shared<SettingsWarningPreferenceStore>(file));
        QVERIFY(!reopened.enabledCategories().contains("orphan"));
        reopened.reset();
        QVERIFY(reopened.enabledCategories().contains("orphan"));
    }
    void jsonTypedRoundTrip() {
        JsonConfigCodec codec;
        const QByteArray source =
            R"({"general":{"enabled":true,"distance":12,"large":9007199254740993,"names":["a","b"],"complex":[{"preserve":1}]}})";
        const auto parsed = codec.decode(source);
        QVERIFY(parsed.error.isEmpty());
        QCOMPARE(parsed.fields.size(), 4);
        QCOMPARE(parsed.unsupported, 1);
        const auto result = codec.encode(source, {{"/general/enabled", false}, {"/general/distance", "18"}});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const auto object = QJsonDocument::fromJson(result.bytes).object().value("general").toObject();
        QVERIFY(!object.value("enabled").toBool());
        QCOMPARE(object.value("large").toInteger(), qint64(9007199254740993LL));
        QCOMPARE(object.value("complex").toArray().size(), 1);
        QVERIFY(!codec.encode(source, {{"/general/distance", "invalid"}}).error.isEmpty());
        QVERIFY(!codec.encode(source, {{"/unknown", false}}).error.isEmpty());
    }
    void tomlCommentsUnicodeAndRanges() {
        TomlConfigCodec codec;
        const QByteArray source = "# Keep this header\r\n[general]\r\n# Enable it\r\nenabled = true # "
                                  "inline\r\n# Range: 1 ~ 32\r\ndistance = 12\r\nname = "
                                  "\"caf\xc3\xa9\"\r\n\"\xcf\x80\" = \"old\"\r\ndate = 2026-10-19\r\n";
        const auto parsed = codec.decode(source);
        QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
        QCOMPARE(parsed.fields.size(), 4);
        QCOMPARE(parsed.unsupported, 1);
        const auto result = codec.encode(source, {{"/general/enabled", false},
                                                  {"/general/distance", "24"},
                                                  {QString::fromUtf8("/general/\xcf\x80"), "new"}});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.bytes.contains("enabled = false # inline\r\n"));
        QVERIFY(result.bytes.contains("# Keep this header\r\n"));
        QVERIFY(result.bytes.contains("name = \"caf\xc3\xa9\""));
        QVERIFY(result.bytes.contains("date = 2026-10-19"));
        QVERIFY(result.bytes.contains("\"\xcf\x80\" = \"new\""));
        QVERIFY(!codec.encode(source, {{"/general/distance", "99"}}).error.isEmpty());
    }
    void propertiesPreserveCommentsAndEscapes() {
        PropertiesConfigCodec codec;
        const QByteArray source = "# Keep\r\nenabled=true\r\nlabel = old\\nvalue\r\ncount:12\r\n";
        const auto parsed = codec.decode(source);
        QCOMPARE(parsed.fields.size(), 3);
        const auto result = codec.encode(source, {{"enabled", false}, {"label", "new\nvalue"}});
        QVERIFY(result.error.isEmpty());
        QVERIFY(result.bytes.contains("# Keep\r\n"));
        QVERIFY(result.bytes.contains("enabled=false\r\n"));
        QVERIFY(result.bytes.contains("label = new\\nvalue\r\n"));
    }
    void listTypesAndDuplicatePropertiesRemainSafe() {
        JsonConfigCodec json;
        QVERIFY(!json.encode(R"({"names":["one"]})", {{"/names", "[1]"}}).error.isEmpty());
        TomlConfigCodec toml;
        QVERIFY(!toml.encode("names=[\"one\"]\n", {{"/names", "[1]"}}).error.isEmpty());
        PropertiesConfigCodec properties;
        const auto duplicate = properties.decode("key=first\nkey=second\n");
        QVERIFY(duplicate.fields.isEmpty());
        QVERIFY(!properties.encode("key=first\nkey=second\n", {{"key", "new"}}).error.isEmpty());
    }
    void atomicBackupAndExternalChanges() {
        QTemporaryDir temp;
        const auto root = temp.path() + "/pack";
        write(root + "/config/example.json", R"({"enabled":true})");
        auto store = std::make_shared<NativePackFileStore>(temp.path() + "/backups");
        ConfigRepository repository(store, store, defaultConfigCodecs());
        ConfigTarget target{root, "config/example.json", "example", "Example", {}, "Matched by file name"};
        const auto document = repository.load(target);
        QVERIFY(document.decoded.error.isEmpty());
        const auto result = repository.save(document, {{"/enabled", false}});
        QVERIFY2(result.ok, qPrintable(result.error));
        QFile backup(result.backupPath);
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), document.original);
        QVERIFY(!repository.save(document, {{"/enabled", false}}).ok);
        QVERIFY(!store->writeConfig(root, "../escape", {}, "bad").ok);
        QVERIFY(!store->writeConfig(root, "mods/example.jar", {}, "bad").ok);
        target.modId.clear();
        QVERIFY(!repository.load(target).decoded.error.isEmpty());
    }
    void ambiguousOwnersNeverEnableEditing() {
        const QVariantList mods{QVariantMap{{"modId", "first"}, {"title", "Shared Config"}},
                                QVariantMap{{"modId", "second"}, {"title", "Shared Config"}}};
        const ConfigAssociation associations(mods);
        QVERIFY(associations.identify("config/shared-config.toml").isEmpty());
        QCOMPARE(associations.identify("config/first-client.toml").value("relatedModId").toString(),
                 QString("first"));
    }
    void backupFailurePreservesSource() {
        QTemporaryDir temp;
        const auto root = temp.path() + "/pack";
        write(root + "/config/example.json", "{\"enabled\":true}");
        write(temp.path() + "/backup-blocker", "file");
        NativePackFileStore store(temp.path() + "/backup-blocker/subfolder");
        const auto result =
            store.writeConfig(root, "config/example.json", "{\"enabled\":true}", "{\"enabled\":false}");
        QVERIFY(!result.ok);
        QFile source(root + "/config/example.json");
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), QByteArray("{\"enabled\":true}"));
    }
    void independentFileLogAndWorldModelsReadOnly() {
        QTemporaryDir temp;
        const auto root = temp.path() + "/pack";
        write(root + "/logs/latest.log", "<b>plain log text</b>\n");
        write(root + "/saves/Example/level.dat", "fixture");
        auto store = std::make_shared<NativePackFileStore>(temp.path() + "/backups");
        PackLogViewer logs(store);
        PackWorldList worlds(store);
        PackFileBrowser files(store);
        logs.setRoot(root);
        worlds.setRoot(root);
        files.setRoot(root);
        QTRY_VERIFY(!logs.busy() && !worlds.busy() && !files.busy());
        QCOMPARE(logs.selected(), QString("logs/latest.log"));
        QCOMPARE(logs.text(), QString("<b>plain log text</b>\n"));
        QCOMPARE(worlds.entries().size(), 1);
        QCOMPARE(files.entries().size(), 2);
        files.open("../outside");
        QTRY_VERIFY(!files.busy());
        QVERIFY(files.entries().isEmpty());
        QFile original(root + "/logs/latest.log");
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("<b>plain log text</b>\n"));
    }
    void configEditorPendingEditsSurviveConflict() {
        QTemporaryDir temp;
        const auto root = temp.path() + "/pack";
        write(root + "/config/example.toml", "enabled = true\n");
        auto store = std::make_shared<NativePackFileStore>(temp.path() + "/backups");
        auto repository = std::make_shared<ConfigRepository>(store, store, defaultConfigCodecs());
        ConfigEditor editor(repository);
        editor.open({root, "config/example.toml", "example", "Example", {}, "Matched by file name"});
        QTRY_VERIFY(!editor.busy());
        editor.setField("/enabled", false);
        QVERIFY(editor.dirty());
        QCOMPARE(editor.fieldValue("/enabled").toBool(), false);
        write(root + "/config/example.toml", "enabled = true\n# external\n");
        editor.save();
        QTRY_VERIFY(!editor.busy());
        QVERIFY(editor.dirty());
        QVERIFY(editor.error().contains("changed outside"));
        editor.discard();
        editor.reload();
        QTRY_VERIFY(!editor.busy());
        QVERIFY(!editor.dirty());
    }
};
QTEST_MAIN(FeatureTests)
#include "FeatureTests.moc"
