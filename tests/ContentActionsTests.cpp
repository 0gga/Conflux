#include "ContentFixtures.h"
#include "domain/TextSearch.h"
#include "features/content/ContentActions.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/ContentInstaller.h"
#include "platform/InstalledModIdentity.h"
#include "platform/NativeContentStore.h"
#include "platform/PackChangeMonitor.h"
#include "services/ContentCatalogue.h"
#include "services/PackRepository.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPersistentModelIndex>
#include <QSaveFile>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <stdexcept>
using namespace studio;
class ContentActionsTests : public QObject {
    Q_OBJECT
    static void write(const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(bytes), bytes.size());
    }
    static QByteArray read(const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
    }
    static QString hash(const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
    }
    static QVariantMap row(const QString &name, const QString &author, bool enabled = true) {
        Release next;
        next.provider = "modrinth";
        next.id = "next-" + name;
        next.projectId = name;
        next.version = "2";
        return {{"title", name},
                {"fileName", name + ".jar"},
                {"relativePath", "mods/" + name + ".jar"},
                {"category", "mods"},
                {"sha1", name},
                {"author", author},
                {"enabled", enabled},
                {"environment", enabled ? "client" : "server"},
                {"openSource", enabled},
                {"external", !enabled},
                {"sourceIds", QVariantMap{{"modrinth", name}}},
                {"updateAvailable", true},
                {"updateRelease", next.toVariant()}};
    }
  private slots:

    void updateDoesNotSwitchLoaderEvenWhenConnectorAllowsFabricInstalls() {
        QTemporaryDir temp;
        auto files = std::make_shared<NativeContentStore>(nullptr, temp.path() + "/backups");
        ContentInstaller installer(files);
        QVariantMap pack{{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "neoforge"}};
        QVariantList rows{
            QVariantMap{{"category", "mods"},
                        {"sourceIds", QVariantMap{{"modrinth", "root"}}},
                        {"installedRelease", QVariantMap{{"loaders", QStringList{"neoforge"}}}}},
            QVariantMap{{"enabled", true}, {"connectorBridge", true}}};
        ContentInstallRequest request;
        request.release.provider = "modrinth";
        request.release.projectId = "root";
        request.release.gameVersions = {"1.21.1"};
        request.release.loaders = {"fabric"};
        request.sourceIds = {{"modrinth", "root"}};
        request.resolveDependencies = false;
        bool called = false;
        ContentWriteResult result;
        installer.install(pack, rows, {request}, [&](ContentWriteResult value) {
            called = true;
            result = value;
        });
        QVERIFY(called);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("loader"));
        request.release.loaders = {"neoforge"};
        request.release.gameVersions = {"1.20.1"};
        called = false;
        installer.install(pack, rows, {request}, [&](ContentWriteResult value) {
            called = true;
            result = value;
        });
        QVERIFY(called);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("Minecraft"));
    }
    void forgivingSearchIgnoresSeparatorsAndSmallOmissions() {
        QElapsedTimer benchmark;
        benchmark.start();
        TextQuery matcher("ModXample");
        int matches = 0;
        for (int i = 0; i < 10000; ++i)
            if (matcher.matches("ModExample by Author 1.21.1"))
                ++matches;
        QCOMPARE(matches, 10000);
        qInfo() << "Forgiving matcher 10000 rows ms:" << benchmark.elapsed();
        QVERIFY(textMatches("Minecraft 1.21.1", "1211"));
        QVERIFY(textMatches("ModExample", "ModXample"));
        QVERIFY(textMatches("Mod Example", "modexample"));
        QVERIFY(textMatches("Café library", "cafe"));
        QVERIFY(!textMatches("Minecraft 1.21.4", "1211"));
        QVERIFY(!textMatches("Unrelated Project", "ModXample"));
        QVERIFY(!textMatches("Amazing Other Module Long Example", "modxample"));
    }

    void fileActionsAreOptimisticSerializedAndRollbackOnlyTheFailedIntent() {
        class Files final : public IContentFileStore {
          public:
            mutable QSemaphore entered, proceed;
            mutable std::atomic<int> calls{0}, active{0}, peak{0};
            QVariantList snapshot(const QVariantMap &, const QVariantList &rows) const override {
                return rows;
            }
            ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const override {
                return {};
            }
            ContentWriteResult setEnabledBatch(const QVariantMap &, const QVariantList &,
                                               bool) const override {
                const int call = ++calls;
                const int running = ++active;
                peak.store(qMax(peak.load(), running));
                entered.release();
                proceed.tryAcquire(1, 5000);
                --active;
                if (call == 1)
                    throw std::runtime_error("Fixture write prevented");
                return ContentWriteResult{true, {}};
            }
            ContentWriteResult remove(const QVariantMap &, const QVariantList &) const override {
                return {};
            }
            ContentWriteResult importFiles(const QVariantMap &, const QList<QUrl> &,
                                           const QString &) const override {
                return {};
            }
        };
        QTemporaryDir temp;
        auto files = std::make_shared<Files>();
        ContentActions content(files, std::make_shared<ContentCatalogueFixture>(),
                               std::make_shared<ContentInstallerFixture>(),
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        content.setContext({{"path", temp.path()}},
                           {row("Alpha", "Author"), row("Bravo", "Author"), row("Charlie", "Author")});
        QTRY_VERIFY(!content.busy());
        const QPersistentModelIndex untouched(content.model()->index(2, 0));
        QSignalSpy reset(content.model(), &QAbstractItemModel::modelReset),
            changed(&content, &ContentActions::rowsChanged),
            errors(&content, &ContentActions::operationFailed);
        content.setEnabled("mods/Alpha.jar", false);
        content.setEnabled("mods/Bravo.jar", false);
        QVERIFY(!content.rows()[0].toMap().value("enabled").toBool());
        QVERIFY(!content.rows()[1].toMap().value("enabled").toBool());
        QTRY_COMPARE(files->entered.available(), 1);
        files->entered.acquire();
        QCOMPARE(files->calls.load(), 1);
        const int notifications = changed.count();
        for (int i = 0; i < 5; ++i)
            content.setProgress("Background work");
        QCOMPARE(changed.count(), notifications);
        QCOMPARE(reset.count(), 0);
        files->proceed.release();
        QTRY_COMPARE(files->entered.available(), 1);
        files->entered.acquire();
        QVERIFY(content.rows()[0].toMap().value("enabled").toBool());
        QVERIFY(!content.rows()[1].toMap().value("enabled").toBool());
        QCOMPARE(errors.count(), 1);
        QCOMPARE(files->peak.load(), 1);
        files->proceed.release();
        QTRY_VERIFY(!content.busy());
        QVERIFY(untouched.isValid());
        QCOMPARE(untouched.data(Qt::UserRole + 1).toMap().value("title").toString(), QString("Charlie"));
        QCOMPARE(reset.count(), 0);
    }
    void overlappingChangesAreRejectedAndUnrelatedContentStillQueues() {
        class Files final : public IContentFileStore {
          public:
            mutable QSemaphore entered, proceed;
            mutable QStringList paths;
            QVariantList snapshot(const QVariantMap &, const QVariantList &rows) const override {
                return rows;
            }
            ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const override {
                return {};
            }
            ContentWriteResult setEnabledBatch(const QVariantMap &, const QVariantList &rows,
                                               bool) const override {
                paths << rows.first().toMap().value("relativePath").toString();
                entered.release();
                proceed.tryAcquire(1, 5000);
                return {true, {}};
            }
            ContentWriteResult remove(const QVariantMap &, const QVariantList &) const override {
                return {};
            }
            ContentWriteResult importFiles(const QVariantMap &, const QList<QUrl> &,
                                           const QString &) const override {
                return {};
            }
        };
        QTemporaryDir temp;
        auto files = std::make_shared<Files>();
        ContentActions content(files, std::make_shared<ContentCatalogueFixture>(),
                               std::make_shared<ContentInstallerFixture>(),
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        content.setContext({{"path", temp.path()}}, {row("Alpha", "Author"), row("Bravo", "Author")});
        QTRY_VERIFY(!content.busy());
        content.setEnabled("mods/Alpha.jar", false);
        QTRY_COMPARE(files->entered.available(), 1);
        files->entered.acquire();
        content.setEnabled("mods/Alpha.jar", true);
        content.setEnabled("mods/Bravo.jar", false);
        content.setLocked("mods/Bravo.jar", true);
        QVERIFY(!content.rows()[0].toMap().value("enabled").toBool());
        files->proceed.release();
        QTRY_COMPARE(files->entered.available(), 1);
        files->entered.acquire();
        QCOMPARE(files->paths, QStringList({"mods/Alpha.jar", "mods/Bravo.jar"}));
        files->proceed.release();
        QTRY_VERIFY(!content.busy());
        QVERIFY(!content.rows()[0].toMap().value("enabled").toBool());
        QVERIFY(!content.rows()[1].toMap().value("enabled").toBool());
        QVERIFY(!content.rows()[1].toMap().value("locked").toBool());
        QVERIFY(content.operations()->failedCount() >= 2);
        content.setLocked("mods/Bravo.jar.disabled", true);
        QVERIFY(content.rows()[1].toMap().value("locked").toBool());
        QCOMPARE(files->paths.size(), 2);
    }
    void backgroundReadExceptionsKeepRowsAndReportAnError() {
        class Files : public IContentFileStore {
          public:
            mutable bool fail = false;
            QVariantList snapshot(const QVariantMap &, const QVariantList &rows) const override {
                if (fail)
                    throw std::runtime_error("Fixture read failure");
                return rows;
            }
            ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const override {
                return {};
            }
            ContentWriteResult remove(const QVariantMap &, const QVariantList &) const override {
                return {};
            }
            ContentWriteResult importFiles(const QVariantMap &, const QList<QUrl> &,
                                           const QString &) const override {
                return {};
            }
        };
        QTemporaryDir temp;
        auto files = std::make_shared<Files>();
        ContentActions content(files, std::make_shared<ContentCatalogueFixture>(),
                               std::make_shared<ContentInstallerFixture>(),
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        content.setContext({{"path", temp.path()}}, {row("Alpha", "Author")});
        QTRY_VERIFY(!content.busy());
        const auto before = content.rows();
        QSignalSpy errors(&content, &ContentActions::operationFailed);
        files->fail = true;
        content.refresh(false);
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.rows(), before);
        QCOMPARE(errors.count(), 1);
        QVERIFY(content.notice().contains("Fixture read failure"));
        QCOMPARE(content.operations()->failedCount(), 1);
    }
    void renamedUpdateStaysOneEntryAndFailedWorkCanBeRetried() {
        QTemporaryDir temp;
        auto files = std::make_shared<ContentFileFixture>();
        auto installer = std::make_shared<ContentInstallerFixture>();
        ContentActions content(files, std::make_shared<ContentCatalogueFixture>(), installer,
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        auto original = row("Alpha", "Author");
        auto release = original.value("updateRelease").toMap();
        release["filename"] = "new-name.jar";
        release["sha1"] = QString(40, 'b');
        original["updateRelease"] = release;
        content.setContext({{"path", temp.path()}}, {original});
        QTRY_VERIFY(!content.busy());
        content.update("mods/Alpha.jar");
        QCOMPARE(content.rows().size(), 1);
        QCOMPARE(content.rows().first().toMap().value("relativePath").toString(),
                 QString("mods/new-name.jar"));
        content.remove("mods/new-name.jar");
        QCOMPARE(content.rows().size(), 1);
        installer->completion({false, "Fixture download failed"});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.rows().first().toMap().value("relativePath").toString(), QString("mods/Alpha.jar"));
        quint64 failed = 0;
        for (const auto &value : content.operations()->entries())
            if (value.toMap().value("retryable").toBool())
                failed = value.toMap().value("id").toULongLong();
        QVERIFY(failed);
        QVERIFY(content.operations()->details(failed).contains("Fixture download failed"));
        content.retryOperation(failed);
        QVERIFY(content.operationsBusy());
        installer->completion({true, {}});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.rows().size(), 1);
    }
    void locksPersistAndUpdatesRemainVisibleWithRecentUpdateFiltering() {
        QTemporaryDir temp;
        auto prefs = std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini");
        auto installer = std::make_shared<ContentInstallerFixture>();
        ContentActions content(std::make_shared<ContentFileFixture>(),
                               std::make_shared<ContentCatalogueFixture>(), installer, prefs);
        const QVariantMap pack{{"path", temp.path()}};
        auto alpha = row("Alpha", "Author"), bravo = row("Bravo", "Author");
        auto next = bravo.value("updateRelease").toMap();
        next["sha1"] = "new-binary";
        next["filename"] = "Bravo.jar";
        bravo["updateRelease"] = next;
        content.setContext(pack, {alpha, bravo});
        QTRY_VERIFY(!content.busy());
        content.setLocked("mods/Alpha.jar", true);
        QCOMPARE(content.updateCount(), 2);
        QCOMPARE(content.actionableUpdateCount(), 1);
        content.setEnabled("mods/Alpha.jar", false);
        content.remove("mods/Alpha.jar");
        QCOMPARE(content.rows().size(), 2);
        QVERIFY(content.rows()[0].toMap().value("enabled").toBool());
        content.updateAll();
        QCOMPARE(installer->requested.size(), 1);
        QCOMPARE(installer->requested.first().title, QString("Bravo"));
        installer->completion({true, {}});
        QTRY_VERIFY(!content.busy());
        content.setFilter("updates", true);
        QCOMPARE(content.rows().size(), 1);
        QCOMPARE(content.rows()[0].toMap().value("title").toString(), QString("Alpha"));
        content.clearFilters();
        content.setFilter("recent", true);
        QCOMPARE(content.rows().size(), 1);
        QCOMPARE(content.rows()[0].toMap().value("title").toString(), QString("Bravo"));
        content.clearFilters();
        content.setFilter("locked", true);
        QCOMPARE(content.rows().size(), 1);
        SettingsContentPreferences reopened(temp.path() + "/prefs.ini");
        QVERIFY(reopened.locked(temp.path(), "modrinth:Alpha"));
        QVERIFY(reopened.updatedAt(temp.path(), "modrinth:Bravo") > 0);
    }
    void nativeLocksGuardRenamesRemovalAndStagedDependencyReplacement() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/mods/old.jar";
        write(path, "old");
        auto locks = std::make_shared<ContentLockRegistry>();
        auto nativeFiles = std::make_shared<NativePackFileStore>();
        NativeContentStore files(std::make_shared<NativeInstalledModIdentity>(nativeFiles),
                                 temp.path() + "/backups", locks);
        const QVariantMap pack{{"path", temp.path()}};
        QVariantMap installed{{"category", "mods"},
                              {"relativePath", "mods/old.jar"},
                              {"sha1", hash("old")},
                              {"enabled", true},
                              {"sourceIds", QVariantMap{{"modrinth", "project"}}}};
        auto locked = installed;
        locked["locked"] = true;
        locks->setRows(temp.path(), {locked});
        QVERIFY(!files.setEnabled(pack, installed, false).ok);
        QVERIFY(!files.remove(pack, {installed}).ok);
        QTemporaryDir stage;
        write(stage.path() + "/mods/new.jar", "new");
        const QJsonObject manifest{{"mods", QJsonArray{QJsonObject{{"provider", "modrinth"},
                                                                   {"projectId", "project"},
                                                                   {"sha1", hash("new")},
                                                                   {"filename", "new.jar"},
                                                                   {"installPath", "mods/new.jar"}}}}};
        const auto result = files.applyStaged(pack, {installed}, stage.path(), manifest);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains("lock", Qt::CaseInsensitive), qPrintable(result.error));
        QCOMPARE(read(path), QByteArray("old"));
        QVERIFY(!QFileInfo::exists(temp.path() + "/mods/new.jar"));
    }
    void lockingPendingNewContentProtectsItsHashNamedDestination() {
        QTemporaryDir temp, stage;
        QDir().mkpath(temp.path() + "/mods");
        const auto nextHash = hash("new");
        write(stage.path() + "/mods/new.jar", "new");
        auto locks = std::make_shared<ContentLockRegistry>();
        const QVariantMap prediction{{"relativePath", "mods/new.jar"},
                                     {"category", "mods"},
                                     {"sha1", nextHash},
                                     {"locked", true},
                                     {"pending", true},
                                     {"sourceIds", QVariantMap{{"modrinth", "project"}}}};
        locks->setRows(temp.path(), {prediction});
        QVERIFY(locks->locked(temp.path(), "mods/" + nextHash + ".jar"));
        QVERIFY(locks->projectLocked(temp.path(), "modrinth", "project"));
        const auto nativeFiles = std::make_shared<NativePackFileStore>();
        NativeContentStore files(std::make_shared<NativeInstalledModIdentity>(nativeFiles),
                                 temp.path() + "/backups", locks);
        const QJsonObject manifest{{"mods", QJsonArray{QJsonObject{{"provider", "modrinth"},
                                                                   {"projectId", "project"},
                                                                   {"sha1", nextHash},
                                                                   {"filename", "new.jar"},
                                                                   {"installPath", "mods/new.jar"}}}}};
        const auto result = files.applyStaged({{"path", temp.path()}}, {}, stage.path(), manifest);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains("lock", Qt::CaseInsensitive), qPrintable(result.error));
        QVERIFY(QDir(temp.path() + "/mods").entryList(QDir::Files).isEmpty());
    }
    void switchingInstanceImmediatelyReplacesRowsWhileAcceptedWorkContinues() {
        QTemporaryDir temp;
        auto installer = std::make_shared<ContentInstallerFixture>();
        ContentActions content(std::make_shared<ContentFileFixture>(),
                               std::make_shared<ContentCatalogueFixture>(), installer,
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        const QVariantMap first{{"path", temp.path() + "/first"}}, second{{"path", temp.path() + "/second"}};
        content.setContext(first, {row("Alpha", "Author")});
        QTRY_VERIFY(!content.busy());
        content.update("mods/Alpha.jar");
        QVERIFY(content.operationsBusy());
        content.setContext(second, {});
        QVERIFY(content.rows().isEmpty());
        content.setContext(second, {row("Bravo", "Author")}, true);
        QTRY_COMPARE(content.rows().first().toMap().value("title").toString(), QString("Bravo"));
        installer->completion({true, {}});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.rows().size(), 1);
        QCOMPARE(content.rows().first().toMap().value("title").toString(), QString("Bravo"));
    }
    void unidentifiedContentLocksRemainScopedToTheirOwnPath() {
        QTemporaryDir temp;
        auto first = row("Alpha", "Author"), second = row("Bravo", "Author");
        for (auto *entry : {&first, &second}) {
            entry->remove("sha1");
            entry->remove("sourceIds");
            entry->remove("projectId");
        }
        ContentActions content(std::make_shared<ContentFileFixture>(),
                               std::make_shared<ContentCatalogueFixture>(),
                               std::make_shared<ContentInstallerFixture>(),
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        content.setContext({{"path", temp.path()}}, {first, second});
        QTRY_VERIFY(!content.busy());
        content.setLocked("mods/Alpha.jar", true);
        QVERIFY(content.rows()[0].toMap().value("locked").toBool());
        QVERIFY(!content.rows()[1].toMap().value("locked").toBool());
    }
    void refreshMetadataUsesExactCategoryAndHashIncludingRenames() {
        QTemporaryDir temp;
        auto catalogue = std::make_shared<ContentCatalogueFixture>();
        ContentActions content(std::make_shared<ContentFileFixture>(), catalogue,
                               std::make_shared<ContentInstallerFixture>(),
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        const QVariantMap pack{{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
        auto original = row("original", "Verified author");
        content.setContext(pack, {original});
        QTRY_VERIFY(!content.busy());
        const int checks = catalogue->refreshCount;
        auto renamed = original;
        renamed["relativePath"] = "mods/renamed.jar";
        renamed.remove("author");
        auto asset = renamed;
        asset["category"] = "resourcepacks";
        asset["relativePath"] = "resourcepacks/same-hash.zip";
        asset["author"] = "Asset author";
        content.select("mods/original.jar", true);
        content.setContext(pack, {renamed, asset});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(catalogue->refreshCount, checks + 1);
        QCOMPARE(catalogue->refreshed.size(), 1);
        QCOMPARE(catalogue->refreshed.first().toMap().value("category").toString(), QString("resourcepacks"));
        QCOMPARE(content.inventory().at(0).toMap().value("author").toString(), QString("Verified author"));
        QCOMPARE(content.inventory().at(1).toMap().value("author").toString(), QString("Asset author"));
        QVERIFY(content.selected().isEmpty());
        content.setContext(pack, {renamed, asset}, true);
        QTRY_VERIFY(!content.busy());
        QCOMPARE(catalogue->refreshCount, checks + 1);
    }
    void forcedContextRefreshDetectsChangesWithIdenticalInventory() {
        class Files final : public IContentFileStore {
          public:
            mutable int reads = 0;
            QVariantList snapshot(const QVariantMap &, const QVariantList &input) const override {
                ++reads;
                auto changed = input.first().toMap();
                changed["sha1"] = QString::number(reads);
                return {changed};
            }
            ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const override {
                return {};
            }
            ContentWriteResult remove(const QVariantMap &, const QVariantList &) const override {
                return {};
            }
            ContentWriteResult importFiles(const QVariantMap &, const QList<QUrl> &,
                                           const QString &) const override {
                return {};
            }
        };
        QTemporaryDir temp;
        auto files = std::make_shared<Files>();
        auto catalogue = std::make_shared<ContentCatalogueFixture>();
        ContentActions content(files, catalogue, std::make_shared<ContentInstallerFixture>(),
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        const QVariantMap pack{{"path", temp.path()}};
        const QVariantList inventory{row("same", "Author")};
        content.setContext(pack, inventory);
        QTRY_VERIFY(!content.busy());
        content.setContext(pack, inventory);
        QCOMPARE(files->reads, 1);
        content.setContext(pack, inventory, true);
        QTRY_VERIFY(!content.busy());
        QCOMPARE(files->reads, 2);
        QCOMPARE(content.inventory().first().toMap().value("sha1").toString(), QString("2"));
        QCOMPARE(catalogue->refreshCount, 2);
    }
    void connectorUpdatesPreserveInstalledLoader_data() {
        QTest::addColumn<QStringList>("installedLoaders");
        QTest::addColumn<QString>("expected");
        QTest::newRow("Barehanded-native-NeoForge") << QStringList{"neoforge"} << QString("neoforge");
        QTest::newRow("Fabric-mod-through-Connector") << QStringList{"fabric"} << QString("fabric");
        QTest::newRow("universal-mod-prefers-instance-loader")
            << QStringList{"fabric", "neoforge"} << QString("neoforge");
    }
    void connectorUpdatesPreserveInstalledLoader() {
        QFETCH(QStringList, installedLoaders);
        QFETCH(QString, expected);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto hash = QString(40, 'a');
        bool newer = false;
        QList<QJsonArray> queries;
        const auto makeRelease = [&](QString id, QStringList loaders, QString version, QString date,
                                     QString fileHash) {
            return QJsonObject{
                {"id", id},
                {"project_id", "NnSJuVRa"},
                {"version_number", version},
                {"date_published", date},
                {"version_type", "release"},
                {"game_versions", QJsonArray{"1.21.1"}},
                {"loaders", QJsonArray::fromStringList(loaders)},
                {"files", QJsonArray{QJsonObject{
                              {"filename", "barehanded-" + loaders.first() + "-" + version + ".jar"},
                              {"primary", true},
                              {"size", 100},
                              {"hashes", QJsonObject{{"sha1", fileHash}}}}}}};
        };
        const auto installed =
            makeRelease("VvSHjZXg", installedLoaders, "1.6.6", "2026-07-29T07:43:04.623658Z", hash);
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
                *buffer += socket->readAll();
                const int split = buffer->indexOf("\r\n\r\n");
                if (split < 0)
                    return;
                int length = 0;
                for (const auto &line : buffer->left(split).split('\n'))
                    if (line.toLower().startsWith("content-length:"))
                        length = line.mid(15).trimmed().toInt();
                if (buffer->size() < split + 4 + length)
                    return;
                const auto path = buffer->split(' ').value(1);
                QJsonDocument response;
                if (path == "/version_files")
                    response = QJsonDocument(QJsonObject{{hash, installed}});
                else if (path == "/version_files/update") {
                    const auto query = QJsonDocument::fromJson(buffer->mid(split + 4, length))
                                           .object()
                                           .value("loaders")
                                           .toArray();
                    queries << query;
                    auto candidate = installed;
                    if (query.size() > 1)
                        candidate = makeRelease("LUQiuCin", {"fabric"}, "1.6.6",
                                                "2026-07-29T07:43:41.211934Z", QString(40, 'b'));
                    else if (newer)
                        candidate = makeRelease("genuinely-new", {expected}, "1.6.7", "2026-08-01T00:00:00Z",
                                                QString(40, 'c'));
                    response = QJsonDocument(QJsonObject{{hash, candidate}});
                } else
                    response = QJsonDocument(QJsonArray{});
                const auto bytes = response.toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                              "\r\nConnection: close\r\n\r\n" + bytes);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        ContentCatalogue catalogue(nullptr,
                                   QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) + "/"));
        QVariantList indexed;
        QString error;
        bool done = false;
        const QVariantList rows{
            QVariantMap{{"category", "mods"}, {"relativePath", "mods/barehanded.jar"}, {"sha1", hash}}};
        auto check = [&] {
            done = false;
            catalogue.refresh(rows, "1.21.1", "neoforge|fabric", [&](QVariantList result, QString failure) {
                indexed = result;
                error = failure;
                done = true;
            });
        };
        check();
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(queries.last(), QJsonArray{expected});
        QVERIFY(!indexed.first().toMap().value("updateAvailable").toBool());
        newer = true;
        check();
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(error.isEmpty());
        QCOMPARE(queries.last(), QJsonArray{expected});
        QVERIFY(indexed.first().toMap().value("updateAvailable").toBool());
    }
    void catalogueChecksEveryContentTypeWithItsOwnPlatformAndClearsStaleUpdates() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        bool newer = true;
        QList<QJsonObject> updateQueries;
        const auto version = [](QString id, QString project, QString loader, QString extension,
                                QString date) {
            return QJsonObject{
                {"id", id},
                {"project_id", project},
                {"version_number", id},
                {"date_published", date},
                {"version_type", "release"},
                {"game_versions", QJsonArray{"1.21.1"}},
                {"loaders", QJsonArray{loader}},
                {"files", QJsonArray{QJsonObject{{"filename", project + extension},
                                                 {"primary", true},
                                                 {"size", 100},
                                                 {"url", "https://example.invalid/file"},
                                                 {"hashes", QJsonObject{{"sha1", QString(40, 'a')}}}}}}};
        };
        const QMap<QString, QString> loaders{{"mods", "fabric"},
                                             {"resourcepacks", "minecraft"},
                                             {"shaderpacks", "iris"},
                                             {"datapacks", "datapack"}};
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
                *buffer += socket->readAll();
                const int split = buffer->indexOf("\r\n\r\n");
                if (split < 0)
                    return;
                int length = 0;
                for (const auto &line : buffer->left(split).split('\n'))
                    if (line.toLower().startsWith("content-length:"))
                        length = line.mid(15).trimmed().toInt();
                if (buffer->size() < split + 4 + length)
                    return;
                const auto path = buffer->split(' ').value(1);
                const auto request = QJsonDocument::fromJson(buffer->mid(split + 4, length)).object();
                QJsonDocument response;
                QJsonObject values;
                if (path == "/version_files") {
                    for (auto it = loaders.cbegin(); it != loaders.cend(); ++it)
                        values[it.key()] =
                            version("1", it.key(), it.value(), it.key() == "mods" ? ".jar" : ".zip",
                                    "2026-01-01T00:00:00Z");
                    response = QJsonDocument(values);
                } else if (path == "/version_files/update") {
                    updateQueries << request;
                    for (const auto &hash : request.value("hashes").toArray()) {
                        const auto key = hash.toString();
                        values[key] = version(newer ? "2" : "1", key, loaders.value(key),
                                              key == "mods" ? ".jar" : ".zip",
                                              newer ? "2026-02-01T00:00:00Z" : "2026-01-01T00:00:00Z");
                    }
                    response = QJsonDocument(values);
                } else
                    response = QJsonDocument(QJsonArray{});
                const auto bytes = response.toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                              "\r\nConnection: close\r\n\r\n" + bytes);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        ContentCatalogue catalogue(nullptr,
                                   QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) + "/"));
        QVariantList rows;
        for (auto it = loaders.cbegin(); it != loaders.cend(); ++it)
            rows << QVariantMap{
                {"sha1", it.key()}, {"category", it.key()}, {"relativePath", it.key() + "/old.zip"}};
        bool done = false;
        QVariantList indexed;
        QString error;
        catalogue.refresh(rows, "1.21.1", "fabric", [&](QVariantList value, QString failure) {
            indexed = value;
            error = failure;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(updateQueries.size(), 4);
        for (const auto &value : indexed)
            QVERIFY(value.toMap().value("updateAvailable").toBool());
        for (const auto &query : updateQueries) {
            const auto key = query.value("hashes").toArray().first().toString();
            QCOMPARE(query.value("loaders").toArray(), QJsonArray{loaders.value(key)});
        }
        newer = false;
        done = false;
        catalogue.refresh(indexed, "1.21.1", "fabric", [&](QVariantList value, QString failure) {
            indexed = value;
            error = failure;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(error.isEmpty());
        for (const auto &value : indexed)
            QVERIFY(!value.toMap().value("updateAvailable").toBool());
    }
    void updateAllUsesOnlyKnownNewerUnfrozenContentAndPreservesTypes() {
        QTemporaryDir temp;
        auto installer = std::make_shared<ContentInstallerFixture>();
        auto catalogue = std::make_shared<ContentCatalogueFixture>();
        auto prefs = std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini");
        ContentActions content(std::make_shared<ContentFileFixture>(), catalogue, installer, prefs);
        QVariantList rows;
        for (const auto &category : {"mods", "resourcepacks", "shaderpacks", "datapacks"}) {
            auto item = row(category, "Author");
            item["category"] = category;
            item["relativePath"] = QString(category) + "/old.zip";
            rows << item;
        }
        auto unchanged = row("unchanged", "Author");
        unchanged["updateAvailable"] = false;
        rows << unchanged;
        content.setContext({{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}}, rows);
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.updateCount(), 4);
        const int checks = catalogue->refreshCount;
        content.freeze("shaderpacks/old.zip", true);
        content.updateAll();
        QCOMPARE(catalogue->refreshCount, checks);
        QCOMPARE(installer->requested.size(), 1);
        QCOMPARE(installer->requested[0].contentType, QString("mod"));
        content.cancel();
        QTRY_COMPARE(installer->requested.first().contentType, QString("resourcepack"));
        content.cancel();
        QTRY_COMPARE(installer->requested.first().contentType, QString("datapack"));
        content.cancel();
        QTRY_VERIFY(!content.busy());
        QVERIFY(content.notice().isEmpty());
        QCOMPARE(content.activity(), QString("cancelled"));
        content.setCategory("datapacks");
        QCOMPARE(content.rows().size(), 1);
        content.select("datapacks/old.zip", true);
        QSignalSpy copied(&content, &ContentActions::copyRequested);
        content.copySelected("files");
        QCOMPARE(copied.count(), 1);
        content.updateSelected();
        QCOMPARE(installer->requested.size(), 1);
        QCOMPARE(installer->requested.first().contentType, QString("datapack"));
        installer->completion({true, {}});
        QVERIFY(content.notice().isEmpty());
    }
    void implicitRefreshIndexesOnlyChangedFilesAcrossAllTypes() {
        QTemporaryDir temp;
        auto catalogue = std::make_shared<ContentCatalogueFixture>();
        ContentActions content(std::make_shared<ContentFileFixture>(), catalogue,
                               std::make_shared<ContentInstallerFixture>(),
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        const QVariantMap pack{{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
        const auto original = row("unchanged", "Author");
        content.setContext(pack, {original});
        QTRY_VERIFY(!content.busy());
        auto asset = row("new", "Author");
        asset["category"] = "resourcepacks";
        asset["relativePath"] = "resourcepacks/new.zip";
        content.setContext(pack, {original, asset});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(catalogue->refreshed.size(), 1);
        QCOMPARE(catalogue->refreshed.first().toMap().value("relativePath").toString(),
                 QString("resourcepacks/new.zip"));
        QCOMPARE(content.rows().size(), 2);
    }
    void filtersSelectionsAndFrozenBatchUpdates() {
        QTemporaryDir temp;
        auto installer = std::make_shared<ContentInstallerFixture>();
        auto prefs = std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini");
        ContentActions content(std::make_shared<ContentFileFixture>(),
                               std::make_shared<ContentCatalogueFixture>(), installer, prefs);
        content.setContext({{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}},
                           {row("Bravo", "Alex"), row("Alpha", "Sam", false)});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.rows().size(), 2);
        QCOMPARE(content.rows().first().toMap().value("title").toString(), QString("Alpha"));
        content.setFilter("author", "Alex");
        QCOMPARE(content.rows().size(), 1);
        content.selectVisible(true);
        QCOMPARE(content.selected(), QStringList{"mods/Bravo.jar"});
        content.clearFilters();
        content.setFilter("environment", "server");
        QCOMPARE(content.rows().size(), 1);
        content.setFilter("state", "enabled");
        QCOMPARE(content.rows().size(), 0);
        content.clearFilters();
        content.setFilter("openSource", true);
        QCOMPARE(content.rows().size(), 1);
        content.clearFilters();
        content.setFilter("external", true);
        QCOMPARE(content.rows().size(), 1);
        content.clearFilters();
        content.freeze("mods/Alpha.jar", true);
        QCOMPARE(content.updateCount(), 2);
        QCOMPARE(content.actionableUpdateCount(), 1);
        content.updateAll();
        QVERIFY(content.busy());
        QCOMPARE(installer->requested.size(), 1);
        QCOMPARE(installer->requested.first().release.projectId, QString("Bravo"));
        installer->completion({true, {}, {}});
        QTRY_VERIFY(!content.busy());
        SettingsContentPreferences reopened(temp.path() + "/prefs.ini");
        QVERIFY(reopened.frozen(temp.path(), "modrinth:Alpha"));
        content.clearSelection();
        QVERIFY(content.selected().isEmpty());
    }
    void explicitFileEditsAreBackedUpAndRejectStaleSnapshots() {
        QTemporaryDir temp;
        const auto root = temp.path() + "/pack";
        write(root + "/mods/local.jar", "original");
        write(root + "/saves/world/level.dat", "world");
        auto reader = std::make_shared<NativePackFileStore>();
        NativeContentStore files(std::make_shared<NativeInstalledModIdentity>(reader),
                                 temp.path() + "/backups");
        const QVariantMap pack{{"path", root}, {"provider", "modrinth"}};
        auto item = row("local", "author");
        item["sha1"] = hash("original");
        auto result = files.setEnabled(pack, item, false);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(root + "/mods/local.jar.disabled"), QByteArray("original"));
        QCOMPARE(read(result.backupPath + "/mods/local.jar"), QByteArray("original"));
        item["relativePath"] = "mods/local.jar.disabled";
        write(root + "/mods/local.jar.disabled", "external edit");
        result = files.remove(pack, {item});
        QVERIFY(!result.ok);
        QCOMPARE(read(root + "/mods/local.jar.disabled"), QByteArray("external edit"));
        item["sha1"] = hash("external edit");
        result = files.remove(pack, {item});
        QVERIFY(result.ok);
        QVERIFY(!QFileInfo::exists(root + "/mods/local.jar.disabled"));
        QCOMPARE(read(root + "/saves/world/level.dat"), QByteArray("world"));
        item["relativePath"] = "../escape.jar";
        QVERIFY(!files.remove(pack, {item}).ok);
        write(temp.path() + "/upload.jar", "upload");
        QVERIFY(files.importFiles(pack, {QUrl::fromLocalFile(temp.path() + "/upload.jar")}, "mods").ok);
        QVERIFY(!files.importFiles(pack, {QUrl::fromLocalFile(temp.path() + "/upload.jar")}, "mods").ok);
    }
    void managedToggleUpdatesTheExactPin() {
        QTemporaryDir temp;
        const auto created = PackRepository::create(temp.path(), "Managed", "1.21.1", "fabric");
        QVERIFY(created.ok);
        const auto sha = hash("managed");
        const auto relative = "mods/" + sha + ".jar";
        write(created.path + '/' + relative, "managed");
        const QJsonArray refs{QJsonObject{
            {"provider", "modrinth"}, {"projectId", "project"}, {"sha1", sha}, {"installPath", relative}}};
        QVERIFY(PackRepository::updateContentPins(created.path, created.manifest, refs).ok);
        auto reader = std::make_shared<NativePackFileStore>();
        NativeContentStore files(std::make_shared<NativeInstalledModIdentity>(reader),
                                 temp.path() + "/backups");
        const auto result = files.setEnabled({{"path", created.path}, {"managed", true}},
                                             {{"relativePath", relative}, {"sha1", sha}}, false);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto pin =
            PackRepository::inspect(created.path).manifest.value("mods").toArray().first().toObject();
        QCOMPARE(pin.value("installPath").toString(), relative + ".disabled");
        QVERIFY(!pin.value("enabled").toBool());
    }
    void stagedExternalUpdatePreservesNamesAndChecksEveryOriginal() {
        QTemporaryDir temp;
        const auto root = temp.path() + "/external", stage = temp.path() + "/stage";
        write(root + "/mods/old.jar", "old");
        write(root + "/minecraftinstance.json", "untouched metadata");
        write(stage + "/mods/new.jar", "new");
        auto reader = std::make_shared<NativePackFileStore>();
        NativeContentStore files(std::make_shared<NativeInstalledModIdentity>(reader),
                                 temp.path() + "/backups");
        QVariantMap old{{"relativePath", "mods/old.jar"},
                        {"sha1", hash("old")},
                        {"category", "mods"},
                        {"enabled", true},
                        {"sourceIds", QVariantMap{{"modrinth", "project"}}}};
        QJsonObject manifest{{"mods", QJsonArray{QJsonObject{{"provider", "modrinth"},
                                                             {"projectId", "project"},
                                                             {"filename", "new-name.jar"},
                                                             {"installPath", "mods/new.jar"},
                                                             {"sha1", hash("new")}}}}};
        auto result = files.applyStaged({{"path", root}}, {old}, stage, manifest);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!QFileInfo::exists(root + "/mods/old.jar"));
        QCOMPARE(read(root + "/mods/new-name.jar"), QByteArray("new"));
        QCOMPARE(read(root + "/minecraftinstance.json"), QByteArray("untouched metadata"));
        QVERIFY(!files.applyStaged({{"path", root}}, {old}, stage, manifest).ok);
    }
    void stagedManagedImportBecomesAnExactTrackedPin() {
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Imported", "1.21.1", "fabric");
        QVERIFY(pack.ok);
        write(pack.path + "/mods/human-name.jar", "old");
        const auto stage = temp.path() + "/stage";
        write(stage + "/mods/verified.jar", "new");
        auto reader = std::make_shared<NativePackFileStore>();
        NativeContentStore files(std::make_shared<NativeInstalledModIdentity>(reader),
                                 temp.path() + "/backups");
        const QVariantMap old{{"relativePath", "mods/human-name.jar"},
                              {"sha1", hash("old")},
                              {"category", "mods"},
                              {"enabled", false},
                              {"sourceIds", QVariantMap{{"modrinth", "project"}}}};
        const QJsonObject manifest{{"mods", QJsonArray{QJsonObject{{"provider", "modrinth"},
                                                                   {"projectId", "project"},
                                                                   {"id", "new-version"},
                                                                   {"filename", "new-name.jar"},
                                                                   {"installPath", "mods/verified.jar"},
                                                                   {"sha1", hash("new")}}}}};
        const auto result =
            files.applyStaged({{"path", pack.path}, {"managed", true}}, {old}, stage, manifest);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto current = PackRepository::inspect(pack.path);
        QVERIFY(current.ok);
        const auto pin = current.manifest.value("mods").toArray().first().toObject();
        QCOMPARE(pin.value("id").toString(), QString("new-version"));
        QCOMPARE(pin.value("installPath").toString(), "mods/" + hash("new") + ".jar.disabled");
        QCOMPARE(read(pack.path + '/' + pin.value("installPath").toString()), QByteArray("new"));
        QVERIFY(!QFileInfo::exists(pack.path + "/mods/human-name.jar"));
    }
    void filesystemWatchesSurviveRebuildAndAtomicReplacement() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/mods/one.jar";
        write(path, "original");
        PackChangeMonitor monitor;
        monitor.setRoot(temp.path());
        QSignalSpy changed(&monitor, &PackChangeMonitor::changed);
        for (int i = 0; i < 5; ++i)
            monitor.rebuild();
        write(path, "in-place");
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 3000);
        changed.clear();
        QSaveFile replacement(path);
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        QCOMPARE(replacement.write("replacement"), 11);
        QVERIFY(replacement.commit());
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 3000);
        changed.clear();
        write(path, "after replacement");
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 3000);
        // A root switch must drop both watches and a queued debounce from the old root.
        write(path, "queued old-root event");
        QTest::qWait(100);
        monitor.setRoot({});
        changed.clear();
        write(path, "unwatched");
        QTest::qWait(650);
        QCOMPARE(changed.count(), 0);
    }
    void filesystemEventsAreDebounced() {
        QTemporaryDir temp;
        QDir().mkpath(temp.path() + "/mods");
        PackChangeMonitor monitor;
        monitor.setRoot(temp.path());
        QSignalSpy changed(&monitor, &PackChangeMonitor::changed);
        write(temp.path() + "/mods/one.jar", "one");
        write(temp.path() + "/mods/two.jar", "two");
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 3000);
        QVERIFY(changed.count() < 4);
    }
};
QTEST_GUILESS_MAIN(ContentActionsTests)
#include "ContentActionsTests.moc"
