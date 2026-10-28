#include "DesktopSession.h"
#include "TestPerformance.h"
#include "features/config/ConfigLibrary.h"
#include "features/content/ConfigAssociation.h"
#include "features/diagnostics/DiagnosticRule.h"
#include "platform/ConfigOwnershipStore.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
using namespace studio;
class FeatureTests : public QObject {
    Q_OBJECT
    static void write(const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
#ifdef Q_OS_WIN
    static QString shortFileName(const QString &path) {
        auto native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
        if (!native.startsWith("\\\\?\\"))
            native = native.startsWith("\\\\") ? "\\\\?\\UNC\\" + native.mid(2) : "\\\\?\\" + native;
        wchar_t buffer[32768];
        const auto length = GetShortPathNameW(native.toStdWString().c_str(), buffer, 32768);
        return length > 0 && length < 32768
                   ? QFileInfo(QDir::fromNativeSeparators(QString::fromWCharArray(buffer, length))).fileName()
                   : QString{};
    }
#endif
  private slots:
    void diagnosticsExposeTypedScopedActions() {
        ContentSnapshot snapshot;
        snapshot.pack = {{"path", "original-instance"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
        const QString sha(40, 'a');
        QVariantMap child{{"title", "Child"}, {"fileName", "child.jar.disabled"},
                          {"modId", "child"}, {"relativePath", "mods/child.jar.disabled"},
                          {"sha1", sha},      {"category", "mods"},
                          {"enabled", false}};
        QVariantMap root{
            {"title", "Root"},
            {"fileName", "root.jar"},
            {"modId", "root"},
            {"relativePath", "mods/root.jar"},
            {"sha1", sha},
            {"category", "mods"},
            {"enabled", true},
            {"loaders", QStringList{"neoforge"}},
            {"runtimeDependencies", QVariantList{QVariantMap{{"id", "child"}, {"required", true}}}}};
        snapshot.files = {root, child};
        const auto evaluate = [&] {
            QVariantMap byCode;
            for (const auto &rule : defaultDiagnosticRules())
                for (const auto &value : rule->evaluate(snapshot)) {
                    const auto entry = value.toMap();
                    if (!entry.value("code").toString().isEmpty())
                        byCode[entry.value("code").toString()] = entry;
                }
            return byCode;
        };
        auto issues = evaluate();
        QCOMPARE(issues.size(), 2);
        const auto missing = issues.value("disabled-required-content").toMap();
        QCOMPARE(missing.value("category").toString(), QString("missing"));
        const auto enable = missing.value("fixes").toList();
        QCOMPARE(enable.size(), 1);
        QCOMPARE(enable.first().toMap().value("kind").toString(), QString("enable"));
        QCOMPARE(enable.first().toMap().value("pack").toMap(), snapshot.pack);
        QCOMPARE(enable.first().toMap().value("sha1").toString(), sha);
        const auto loader = issues.value("incompatible-loader").toMap().value("fixes").toList();
        QCOMPARE(loader.size(), 2);
        QCOMPARE(loader.first().toMap().value("kind").toString(), QString("disable"));
        QCOMPARE(loader.last().toMap().value("kind").toString(), QString("remove"));
        child["sha1"] = "unverified";
        snapshot.files = {root, child};
        QVERIFY(evaluate().value("disabled-required-content").toMap().value("fixes").toList().isEmpty());
        snapshot.pack["loader"] = "neoforge";
        QVERIFY(!evaluate().contains("incompatible-loader"));
        Release release;
        release.provider = "curseforge";
        release.projectId = "root";
        release.id = "root-version";
        release.sha1 = sha;
        release.dependencyRefs = {QJsonObject{{"modId", 22}, {"relationType", 3}},
                                  QJsonObject{{"modId", 33}, {"relationType", 3}}};
        root["installedRelease"] = release.toVariant();
        snapshot.files = {root};
        const auto absent = evaluate().value("missing-required-content").toMap();
        QVERIFY(absent.value("message").toString().contains("2 missing"));
        const auto review = absent.value("fixes").toList();
        QCOMPARE(review.size(), 1);
        QCOMPARE(review.first().toMap().value("kind").toString(), QString("review-dependencies"));
        QCOMPARE(review.first().toMap().value("pack").toMap(), snapshot.pack);
    }
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
    void configLibraryGettersReusePublishedSnapshot_data() {
        QTest::addColumn<int>("count");
        QTest::newRow("empty") << 0;
        QTest::newRow("ordinary") << 100;
        QTest::newRow("large-config-library") << 2816;
    }
    void configLibraryGettersReusePublishedSnapshot() {
        QFETCH(int, count);
        QTemporaryDir temp;
        struct OwnershipReads final : IConfigOwnershipStore {
            SettingsConfigOwnershipStore native;
            mutable int reads = 0;
            explicit OwnershipReads(const QString &path) : native(path) {}
            QString owner(const QString &pack, const QString &path) const override {
                ++reads;
                return native.owner(pack, path);
            }
            void setOwner(const QString &pack, const QString &path, const QString &id) override {
                native.setOwner(pack, path, id);
            }
        };
        auto store = std::make_shared<OwnershipReads>(temp.path() + "/owners.ini");
        const auto root = temp.path() + "/Instance";
        QVariantList inventory;
        QStringList paths;
        const int modCount = count == 0 ? 0 : 310;
        for (int i = 0; i < modCount; ++i)
            inventory << QVariantMap{{"category", "mods"},
                                     {"modId", QString("mod%1").arg(i)},
                                     {"title", QString("Mod %1").arg(i, 4, 10, QChar('0'))}};
        for (int i = 0; i < count; ++i) {
            const auto path = QString("config/group%1/settings-%2.%3")
                                  .arg(i % 16)
                                  .arg(i)
                                  .arg(QStringList{"json", "toml", "properties", "cfg"}[i % 4]);
            paths << path;
            inventory << QVariantMap{
                {"category", "configs"}, {"relativePath", path}, {"fileName", QFileInfo(path).fileName()}};
            if (i % 3 == 0)
                store->setOwner(root, path, QString("mod%1").arg(i % modCount));
        }
        ConfigLibrary library(store);
        QElapsedTimer clock;
        clock.start();
        library.setContext({{"path", root}}, inventory);
        const auto published = library.files();
        const auto publishedMods = library.mods();
        const auto initialMs = clock.elapsed();
        QCOMPARE(published.size(), count);
        QCOMPARE(publishedMods.size(), modCount);
        const int initialReads = store->reads;
        clock.restart();
        qsizetype fileCount = 0, modsCount = 0;
        for (int i = 0; i < 100; ++i) {
            fileCount += library.files().size();
            modsCount += library.mods().size();
            const auto path = count == 0 ? QString("missing.json") : paths[(i * 37) % count];
            const auto file = library.file(path);
            if (count == 0)
                QVERIFY(file.isEmpty());
            else
                QCOMPARE(file.value("relativePath").toString(), path);
        }
        const auto warmMs = clock.elapsed();
        qInfo() << count << "configs: initial ms" << initialMs << "initial ownership reads" << initialReads
                << "100 files/mods/path reads ms" << warmMs << "extra ownership reads"
                << store->reads - initialReads;
        QCOMPARE(fileCount, count * 100);
        QCOMPARE(modsCount, modCount * 100);
        QCOMPARE(store->reads - initialReads, 0);
        QVERIFY(test_performance::releaseBudgetMet(warmMs, 150));
        auto detached = library.files();
        if (!detached.isEmpty())
            detached.first() = QVariantMap{{"title", "Caller edit"}};
        QCOMPARE(library.files(), published);
        QCOMPARE(library.mods(), publishedMods);
    }
    void configLibraryPublishesOwnershipChangesAndRefreshes() {
        QTemporaryDir temp;
        auto store = std::make_shared<SettingsConfigOwnershipStore>(temp.path() + "/owners.ini");
        ConfigLibrary library(store);
        const QVariantMap packA{{"path", temp.path() + "/A"}}, packB{{"path", temp.path() + "/B"}};
        const QVariantMap alpha{
            {"category", "mods"}, {"modId", "alpha"}, {"title", "Alpha"}, {"icon", "alpha.png"}};
        const QVariantMap zulu{
            {"category", "mods"}, {"modId", "zulu"}, {"title", "Zulu"}, {"enabled", false}};
        const QVariantMap automatic{{"category", "configs"},      {"relativePath", "config/a.JSON"},
                                    {"fileName", "a.JSON"},       {"relatedModId", "alpha"},
                                    {"relatedModTitle", "Alpha"}, {"association", "Matched by file name"}};
        const QVariantMap unknown{
            {"category", "configs"}, {"relativePath", "config/b.toml"}, {"fileName", "b.toml"}};
        const QVariantMap unsupported{
            {"category", "configs"}, {"relativePath", "config/c.bin"}, {"fileName", "c.bin"}};
        QVariantList inventory{alpha,
                               zulu,
                               automatic,
                               unknown,
                               unsupported,
                               QVariantMap{{"category", "mods"}, {"modId", ""}, {"title", "Ignore"}},
                               QVariantMap{{"category", "worlds"}, {"modId", "world"}}};
        library.setContext(packA, inventory);
        QSignalSpy changed(&library, &ConfigLibrary::changed);
        connect(&library, &ConfigLibrary::changed, &library, [&] {
            for (const auto &value : library.files())
                QCOMPARE(library.file(value.toMap().value("relativePath").toString()), value.toMap());
        });
        QCOMPARE(library.mods().size(), 2);
        QCOMPARE(library.mods().first().toMap().value("modId").toString(), "alpha");
        QVERIFY(library.file("config/a.JSON").value("editable").toBool());
        QVERIFY(!library.identify("missing", "alpha"));
        QVERIFY(!library.identify("config/b.toml", "absent"));
        QCOMPARE(changed.size(), 0);
        QVERIFY(library.identify("config/b.toml", "zulu"));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(library.file("config/b.toml").value("relatedModTitle").toString(), "Zulu");
        QVERIFY(library.file("config/b.toml").value("editable").toBool());
        QVERIFY(library.identify("config/c.bin", "alpha"));
        QVERIFY(!library.file("config/c.bin").value("supported").toBool());
        QVERIFY(!library.file("config/c.bin").value("editable").toBool());
        const auto beforeRefresh = library.files();
        store->setOwner(packA.value("path").toString(), "config/b.toml", "alpha");
        QCOMPARE(library.files(), beforeRefresh);
        library.setContext(
            packA, inventory); // An explicit refresh accepts persisted changes even with identical input.
        QCOMPARE(library.file("config/b.toml").value("relatedModTitle").toString(), "Alpha");
        QCOMPARE(library.file("config/b.toml").value("icon").toString(), "alpha.png");
        library.setContext(packB, inventory);
        QVERIFY(!library.file("config/b.toml").value("editable").toBool());
        QCOMPARE(library.file("config/a.JSON").value("association").toString(), "Matched by file name");
        library.setContext(packA, inventory);
        QVERIFY(library.file("config/b.toml").value("editable").toBool());
        inventory.removeFirst(); // Missing owner must not inherit the cached editing permission.
        library.setContext(packA, inventory);
        QVERIFY(!library.file("config/b.toml").value("editable").toBool());
        library.setContext({}, {});
        QVERIFY(library.files().isEmpty());
        QVERIFY(library.mods().isEmpty());
        QVERIFY(library.file("config/b.toml").isEmpty());
    }
    void configLibraryPathLookupUsesFirstPublishedMatch() {
        QTemporaryDir temp;
        ConfigLibrary library(std::make_shared<SettingsConfigOwnershipStore>(temp.path() + "/owners.ini"));
        library.setContext(
            {{"path", "pack"}},
            {QVariantMap{{"category", "mods"}, {"modId", "id"}, {"title", "Old title"}},
             QVariantMap{{"category", "mods"}, {"modId", "id"}, {"title", "Last mod wins"}},
             QVariantMap{
                 {"category", "configs"}, {"relativePath", "config/same.json"}, {"fileName", "z.json"}},
             QVariantMap{
                 {"category", "configs"}, {"relativePath", "config/same.json"}, {"fileName", "a.json"}}});
        QCOMPARE(library.mods().size(), 1);
        QCOMPARE(library.mods().first().toMap().value("title").toString(), "Last mod wins");
        QCOMPARE(library.files().size(), 2);
        QCOMPARE(library.file("config/same.json"), library.files().first().toMap());
        QCOMPARE(library.file("config/same.json").value("fileName").toString(), "a.json");
        QVERIFY(library.identify("config/same.json", "id"));
        QCOMPARE(library.file("config/same.json").value("relatedModTitle").toString(), "Last mod wins");
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
    void privateEntriesCannotBeReadOrSavedThroughAliases_data() {
        QTest::addColumn<QString>("relative");
        QTest::addColumn<bool>("exists");
        QTest::newRow("git-literal") << QString("config/.git/settings.json") << true;
        QTest::newRow("env-literal") << QString("config/.env") << true;
#ifdef Q_OS_WIN
        for (const auto &path : {"config/.GiT/settings.json", "config/.GIT/settings.json",
                                 "config/.gIt/settings.json", "config/.git./settings.json", "config/.EnV",
                                 "config/.ENV", "config/.eNv", "config/.env.", "config/.EnV::$DATA"})
            QTest::newRow(path) << QString(path) << true;
        QTest::newRow("git-short") << QString("git-short") << true;
        QTest::newRow("env-short") << QString("env-short") << true;
        QTest::newRow("git-space") << QString("config/.git /settings.json") << false;
        QTest::newRow("env-space") << QString("config/.env ") << false;
#endif
    }
    void privateEntriesCannotBeReadOrSavedThroughAliases() {
        QFETCH(QString, relative);
        QFETCH(bool, exists);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = temp.path() + "/pack";
        const auto backups = temp.path() + "/backups";
#ifdef Q_OS_WIN
        const QString git = "config/.GiT", env = "config/.EnV";
#else
        const QString git = "config/.git", env = "config/.env";
#endif
        const QByteArray original = "test-owned private content";
        write(root + '/' + git + "/settings.json", original);
        write(root + '/' + env, original);
#ifdef Q_OS_WIN
        if (relative == "git-short" || relative == "env-short") {
            const bool directory = relative == "git-short";
            const auto name = shortFileName(root + '/' + (directory ? git : env));
            if (name.isEmpty() ||
                name.compare(QFileInfo(directory ? git : env).fileName(), Qt::CaseInsensitive) == 0)
                QSKIP("This temporary volume does not expose an alternate short name.");
            relative = "config/" + name + (directory ? "/settings.json" : "");
        }
#endif
        if (exists)
            QVERIFY2(QFileInfo::exists(root + '/' + relative), qPrintable(relative));
        NativePackFileStore store(backups);
        const auto resolved = NativePackFileStore::resolve(root, relative);
        const auto read = store.read(root, relative, 1024);
        const auto saved = store.writeConfig(root, relative, original, "replacement");
        qInfo() << relative << "resolved/read/write" << !resolved.isEmpty() << read.error.isEmpty()
                << saved.ok;
        QVERIFY(resolved.isEmpty());
        QVERIFY(!read.error.isEmpty());
        QVERIFY(!saved.ok);
        QVERIFY(!QFileInfo::exists(backups));
        QFile gitFile(root + '/' + git + "/settings.json"), envFile(root + '/' + env);
        QVERIFY(gitFile.open(QIODevice::ReadOnly));
        QVERIFY(envFile.open(QIODevice::ReadOnly));
        QCOMPARE(gitFile.readAll(), original);
        QCOMPARE(envFile.readAll(), original);
        QVERIFY(store.list(root, git).isEmpty());
    }
    void fileBrowserKeepsPrivateEntriesHiddenAndLookalikesVisible() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = temp.path() + "/pack";
#ifdef Q_OS_WIN
        write(root + "/config/.GiT/settings.json", "private");
        write(root + "/config/.EnV", "private");
#else
        write(root + "/config/.git/settings.json", "private");
        write(root + "/config/.env", "private");
#endif
        write(root + "/config/.github/allowed.json", "{}");
        write(root + "/config/.env.example", "example");
        write(root + "/config/allowed.json", "{}");
#ifdef Q_OS_WIN
        const auto lookalike = QString(".g%1t").arg(QChar(0x0131));
        write(root + "/config/" + lookalike + "/allowed.json", "{}");
#endif
        auto store = std::make_shared<NativePackFileStore>(temp.path() + "/backups");
        const auto entries = store->list(root, "config");
        QSet<QString> names;
        for (const auto &value : entries)
            names.insert(value.toMap().value("title").toString());
#ifdef Q_OS_WIN
        const QSet<QString> expected{".github", ".env.example", "allowed.json", lookalike};
#else
        const QSet<QString> expected{"allowed.json"};
#endif
        QCOMPARE(names, expected);
        QCOMPARE(store->read(root, "config/.github/allowed.json", 1024).bytes, QByteArray("{}"));
        QCOMPARE(store->read(root, "config/.env.example", 1024).bytes, QByteArray("example"));
#ifdef Q_OS_WIN
        QCOMPARE(store->read(root, "config/" + lookalike + "/allowed.json", 1024).bytes, QByteArray("{}"));
#endif
        PackFileBrowser browser(store);
        browser.setRoot(root);
        QTRY_VERIFY(!browser.busy());
        browser.open("config");
        QTRY_VERIFY(!browser.busy());
        names.clear();
        for (const auto &value : browser.entries())
            names.insert(value.toMap().value("title").toString());
        QCOMPARE(names, expected);
    }
    void allowedConfigAliasesPreserveTheOriginalLongFilename_data() {
        QTest::addColumn<QString>("category");
        QTest::addColumn<QString>("alias");
        QTest::addColumn<bool>("longRoot");
#ifdef Q_OS_WIN
        for (const auto &category : {QString("config"), QString("defaultconfigs")})
            for (const auto &alias :
                 {QString("case"), QString("short-file"), QString("short-directory"), QString("dot")})
                for (bool longRoot : {false, true})
                    QTest::newRow(qPrintable(category + '-' + alias + QString::number(longRoot)))
                        << category << alias << longRoot;
#else
        QTest::newRow("ordinary") << QString("config") << QString("ordinary") << false;
#endif
    }
    void allowedConfigAliasesPreserveTheOriginalLongFilename() {
        QFETCH(QString, category);
        QFETCH(QString, alias);
        QFETCH(bool, longRoot);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto root = temp.path() + "/pack";
        if (longRoot) {
            const auto component = QString(QChar(0x754c)).repeated(100);
            root += '/' + component + '/' + component + "/unicode-profile";
            QVERIFY(root.size() > 260);
        }
        const QString fileName = "A very long config filename.json";
        const auto directory = alias == "short-directory" && category == "config"
                                   ? category + "/A very long section folder"
                                   : category;
        const auto actual = root + '/' + directory + '/' + fileName;
        const QByteArray original = "{\"enabled\":true}", replacement = "{\"enabled\":false}";
        write(actual, original);
        QString relative = directory + '/' + fileName;
#ifdef Q_OS_WIN
        if (alias == "case")
            relative = relative.toUpper();
        else if (alias == "short-file") {
            const auto name = shortFileName(actual);
            if (name.isEmpty() || name.compare(fileName, Qt::CaseInsensitive) == 0)
                QSKIP("This temporary volume does not expose an alternate file short name.");
            relative = category + '/' + name;
        } else if (alias == "short-directory") {
            const auto name = shortFileName(root + '/' + directory);
            if (name.isEmpty() || name.compare(QFileInfo(directory).fileName(), Qt::CaseInsensitive) == 0)
                QSKIP("This directory already has a short-compatible name, or short names are unavailable.");
            relative = (category == "config" ? category + '/' : QString{}) + name + '/' + fileName;
        } else if (alias == "dot")
            relative = "./" + category + "/./" + fileName;
#endif
        QVERIFY2(QFileInfo::exists(root + '/' + relative), qPrintable(relative));
        NativePackFileStore store(temp.path() + "/backups");
        const auto result = store.writeConfig(root, relative, original, replacement);
        QVERIFY2(result.ok, qPrintable(result.error));
        QFile file(actual), backup(result.backupPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), replacement);
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), original);
        QCOMPARE(QDir(root + '/' + directory).entryList(QDir::Files | QDir::NoDotAndDotDot),
                 QStringList{fileName});
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
