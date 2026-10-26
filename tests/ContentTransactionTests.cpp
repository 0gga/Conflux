#include "platform/ContentAccess.h"
#include "platform/LauncherContentState.h"
#include "platform/NativeContentStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QSemaphore>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrent>
#include <QtTest>
#include <filesystem>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
using namespace studio;
class ContentTransactionTests : public QObject {
    Q_OBJECT
    static void write(const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
    static QString hash(const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
    }
    static QVariantMap sourcePack(const QString &root, const QByteArray &bytes) {
        const auto app = root + "/ModrinthApp", path = app + "/profiles/Fixture";
        QDir().mkpath(path + "/resourcepacks");
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(app + "/app.db");
            if (!db.open())
                return {};
            QSqlQuery query(db);
            for (const auto &sql :
                 {"CREATE TABLE instances(id TEXT,path TEXT)",
                  "CREATE TABLE instance_files(id TEXT,instance_id TEXT,relative_path TEXT,sha1 TEXT,enabled "
                  "INTEGER)",
                  "CREATE TABLE instance_content_entries(file_id TEXT,project_id TEXT,version_id TEXT)",
                  "CREATE TABLE instance_content_locks(file_id TEXT)",
                  "INSERT INTO instances VALUES('instance','Fixture')",
                  "INSERT INTO instance_content_entries VALUES('file','locked-project','locked-version')",
                  "INSERT INTO instance_content_locks VALUES('file')"})
                if (!query.exec(sql))
                    return {};
            query.prepare(
                "INSERT INTO instance_files VALUES('file','instance','resourcepacks/locked.zip',?,1)");
            query.addBindValue(hash(bytes));
            if (!query.exec())
                return {};
        }
        QSqlDatabase::removeDatabase(connection);
        return {{"path", path}, {"provider", "modrinth"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
    }
  private slots:
    void nativeRefreshPreservesEverySourcePathLock_data() {
        QTest::addColumn<QString>("state");
        QTest::addColumn<bool>("disabled");
        QTest::addColumn<bool>("upper");
        QTest::addColumn<bool>("inspect");
        for (const auto &state : {"locked-first", "locked-last", "all-unlocked"})
            for (bool disabled : {false, true})
                for (bool upper : {false, true})
                    for (bool inspect : {false, true})
                        QTest::newRow(qPrintable(QString("%1-disabled-%2-upper-%3-inspect-%4")
                                                     .arg(state)
                                                     .arg(disabled)
                                                     .arg(upper)
                                                     .arg(inspect)))
                            << QString(state) << disabled << upper << inspect;
    }
    void nativeRefreshPreservesEverySourcePathLock() {
        QFETCH(QString, state);
        QFETCH(bool, disabled);
        QFETCH(bool, upper);
        QFETCH(bool, inspect);
        QTemporaryDir temp;
        const QByteArray bytes = "installed file";
        const auto relative = QString("mods/Example.jar") + (disabled ? ".disabled" : "");
        write(temp.filePath(relative), bytes);
        QJsonObject first{
            {"addonID", 11},
            {"isLocked", state == "locked-first"},
            {"isEnabled", !disabled},
            {"fileNameOnDisk", upper ? "EXAMPLE.JAR" : "Example.jar"},
            {"modFolderPath", temp.filePath("mods")},
            {"installedFile",
             QJsonObject{{"id", 101},
                         {"hashes", QJsonArray{QJsonObject{{"algo", 1}, {"value", hash(bytes)}}}}}}};
        auto last = first;
        last["addonID"] = 22;
        last["isLocked"] = state == "locked-last";
        const auto metadata =
            QJsonDocument(QJsonObject{{"installedAddons", QJsonArray{first, last}}}).toJson();
        write(temp.filePath("minecraftinstance.json"), metadata);
        const QVariantMap pack{{"path", temp.path()}, {"provider", "curseforge"}};
        const QVariantMap row{{"relativePath", relative}, {"category", "mods"},    {"sha1", hash(bytes)},
                              {"enabled", !disabled},     {"title", "Keep title"}, {"lockPending", true}};
        NativeContentStore store(nullptr, temp.filePath("backups"));
        const auto result = store.reload(pack, {row}, inspect);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.rows.size(), 1);
        bool samePath = !upper;
#ifdef Q_OS_WIN
        samePath = true;
#endif
        const auto actual = result.rows.first().toMap();
        QCOMPARE(actual.value("launcherLocked").toBool(), samePath && state != "all-unlocked");
        QCOMPARE(actual.value("sha1").toString(), hash(bytes));
        QCOMPARE(actual.value("relativePath").toString(), relative);
        if (!inspect) {
            auto expected = row;
            expected.remove("lockPending");
            expected["launcherLocked"] = samePath && state != "all-unlocked";
            QCOMPARE(actual, expected);
        }
        QCOMPARE(read(temp.filePath("minecraftinstance.json")), metadata);
        QCOMPARE(read(temp.filePath(relative)), bytes);
    }
    void missingSourceLockStateCannotAuthorizeFileChanges_data() {
        QTest::addColumn<QString>("action");
        QTest::addColumn<bool>("missingDatabase");
        for (const auto &action : {"reload", "remove", "disable", "import", "install-asset"})
            for (bool missing : {true, false})
                QTest::newRow(qPrintable(QString("%1-missing-db-%2").arg(action).arg(missing)))
                    << QString(action) << missing;
    }
    void missingSourceLockStateCannotAuthorizeFileChanges() {
        QFETCH(QString, action);
        QFETCH(bool, missingDatabase);
        QTemporaryDir temp;
        const QByteArray original = "locked original", replacement = "new content";
        auto pack = sourcePack(temp.path(), original);
        QVERIFY(!pack.isEmpty());
        const auto root = pack.value("path").toString();
        const auto file = root + "/resourcepacks/locked.zip";
        write(file, original);
        const QVariantMap row{{"relativePath", "resourcepacks/locked.zip"},
                              {"category", "resourcepacks"},
                              {"sha1", hash(original)},
                              {"enabled", true},
                              {"sourceIds", QVariantMap{{"modrinth", "locked-project"}}}};
        if (missingDatabase)
            QVERIFY(QFile::remove(temp.path() + "/ModrinthApp/app.db"));
        else
            pack["launcherInstanceId"] = "untracked-instance";
        const auto databaseBytes = read(temp.path() + "/ModrinthApp/app.db");
        NativeContentStore store(nullptr, temp.path() + "/backups");
        if (action == "reload") {
            const auto result = store.reload(pack, {row});
            QVERIFY(!result.error.isEmpty());
            QVERIFY(result.rows.isEmpty());
        } else {
            ContentWriteResult result;
            if (action == "remove")
                result = store.remove(pack, {row});
            else if (action == "disable")
                result = store.setEnabled(pack, row, false);
            else {
                const auto source = temp.path() + "/upload/locked.zip";
                write(source, replacement);
                if (action == "import")
                    result = store.importFiles(pack, {QUrl::fromLocalFile(source)}, "resourcepacks");
                else {
                    ContentInstallRequest request;
                    request.contentType = "resourcepack";
                    request.release.provider = "modrinth";
                    request.release.projectId = "locked-project";
                    request.release.filename = "locked.zip";
                    request.release.sha1 = hash(replacement);
                    result = store.installAsset(pack, {row}, request, source);
                }
            }
            QVERIFY(!result.ok);
            QVERIFY(!result.error.isEmpty());
        }
        QCOMPARE(read(file), original);
        QVERIFY(!QFileInfo::exists(file + ".disabled"));
        QCOMPARE(read(temp.path() + "/ModrinthApp/app.db"), databaseBytes);
    }
    void exactLockedRecoveryPreservesSourceMetadata_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<bool>("missingFolder");
        QTest::addColumn<bool>("uppercaseHash");
        for (const auto &provider : {"modrinth", "curseforge"})
            for (bool missing : {false, true})
                for (bool uppercase : {false, true})
                    QTest::newRow(qPrintable(
                        QString("%1-missing-%2-uppercase-%3").arg(provider).arg(missing).arg(uppercase)))
                        << QString(provider) << missing << uppercase;
    }
    void exactLockedRecoveryPreservesSourceMetadata() {
        QFETCH(QString, provider);
        QFETCH(bool, missingFolder);
        QFETCH(bool, uppercaseHash);
        QTemporaryDir temp;
        const QByteArray original = "exact locked version";
        QVariantMap pack;
        QString metadata;
        if (provider == "modrinth") {
            pack = sourcePack(temp.path(), original);
            metadata = temp.path() + "/ModrinthApp/app.db";
        } else {
            const auto path = temp.path() + "/CurseForge/Instances/Fixture";
            QVERIFY(QDir().mkpath(path + "/resourcepacks"));
            metadata = path + "/minecraftinstance.json";
            const QJsonObject addon{
                {"addonID", 11},
                {"isLocked", true},
                {"isEnabled", true},
                {"fileNameOnDisk", "locked.zip"},
                {"modFolderPath", path + "/resourcepacks"},
                {"installedFile",
                 QJsonObject{{"id", 101},
                             {"hashes", QJsonArray{QJsonObject{{"algo", 1}, {"value", hash(original)}}}}}}};
            write(metadata, QJsonDocument(QJsonObject{{"installedAddons", QJsonArray{addon}}}).toJson());
            pack = {{"path", path}, {"provider", provider}};
        }
        QVERIFY(!pack.isEmpty());
        const auto sourceBytes = read(metadata);
        QVERIFY(!sourceBytes.isEmpty());
        const auto root = pack.value("path").toString();
        if (missingFolder)
            QVERIFY(QDir().rmdir(root + "/resourcepacks"));
        auto locks = LauncherContentState::read(pack);
        QCOMPARE(locks.size(), 1);
        auto request = locks.first().toMap();
        if (uppercaseHash)
            request["sha1"] = request.value("sha1").toString().toUpper();
        write(temp.path() + "/backups/good/resourcepacks/locked.zip", original);
        NativeContentStore store(nullptr, temp.path() + "/backups");
        const auto result = store.restoreLocked(pack, request);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(root + "/resourcepacks/locked.zip"), original);
        QCOMPARE(read(metadata), sourceBytes);
        QVERIFY(LauncherContentState::read(pack).first().toMap().value("launcherLocked").toBool());
        QVERIFY(!store.restoreLocked(pack, request).ok);
        QCOMPARE(read(root + "/resourcepacks/locked.zip"), original);
    }
    void lockedRecoveryRequiresTheExactRequestedIdentity_data() {
        QTest::addColumn<QString>("fault");
        for (const auto &fault : {"provider", "project", "hash", "missing-hash", "path", "missing-path"})
            QTest::newRow(fault) << QString(fault);
    }
    void lockedRecoveryRequiresTheExactRequestedIdentity() {
        QFETCH(QString, fault);
        QTemporaryDir temp;
        const QByteArray original = "locked version";
        const auto pack = sourcePack(temp.path(), original);
        QVERIFY(!pack.isEmpty());
        auto request = LauncherContentState::read(pack).first().toMap();
        write(temp.path() + "/backups/good/resourcepacks/locked.zip", original);
        if (fault == "provider")
            request["provider"] = "curseforge";
        if (fault == "project")
            request["projectId"] = "different-project";
        if (fault == "hash")
            request["sha1"] = hash("another version");
        if (fault == "missing-hash")
            request.remove("sha1");
        if (fault == "path")
            request["relativePath"] = "resourcepacks/different.zip";
        if (fault == "missing-path")
            request.remove("relativePath");
        NativeContentStore store(nullptr, temp.path() + "/backups");
        const auto result = store.restoreLocked(pack, request);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!QFileInfo::exists(pack.value("path").toString() + "/resourcepacks/locked.zip"));
        QCOMPARE(read(temp.path() + "/backups/good/resourcepacks/locked.zip"), original);
    }
    void lockedRecoveryRejectsLinkedFolders_data() {
        QTest::addColumn<bool>("targetLink");
        QTest::newRow("linked-destination-folder") << true;
        QTest::newRow("linked-backup-folder") << false;
    }
    void lockedRecoveryRejectsLinkedFolders() {
        QFETCH(bool, targetLink);
        QTemporaryDir temp, outside;
        const QByteArray original = "locked version";
        const auto pack = sourcePack(temp.path(), original);
        QVERIFY(!pack.isEmpty());
        const auto root = pack.value("path").toString();
        const auto request = LauncherContentState::read(pack).first().toMap();
        const auto backups = temp.path() + "/backups";
        const auto link = targetLink ? root + "/resourcepacks" : backups + "/linked";
        if (targetLink) {
            QVERIFY(QDir().rmdir(link));
            write(backups + "/good/resourcepacks/locked.zip", original);
        } else {
            QVERIFY(QDir().mkpath(backups));
            write(outside.path() + "/resourcepacks/locked.zip", original);
        }
        write(outside.path() + "/sentinel", "preserved");
#ifdef Q_OS_WIN
        QProcess junction;
        junction.setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
        junction.setStandardOutputFile(QProcess::nullDevice());
        junction.setStandardErrorFile(QProcess::nullDevice());
        junction.start("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(link),
                                   QDir::toNativeSeparators(outside.path())});
        QVERIFY(junction.waitForFinished(5000));
        QCOMPARE(junction.exitCode(), 0);
        QVERIFY(QFileInfo(link).isJunction());
#else
        std::error_code error;
        std::filesystem::create_directory_symlink(outside.path().toStdString(), link.toStdString(), error);
        QVERIFY2(!error, qPrintable(QString::fromStdString(error.message())));
#endif
        NativeContentStore store(nullptr, backups);
        const auto result = store.restoreLocked(pack, request);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(read(outside.path() + "/sentinel"), QByteArray("preserved"));
        if (targetLink)
            QVERIFY(!QFileInfo::exists(outside.path() + "/locked.zip"));
        else {
            QVERIFY(!QFileInfo::exists(root + "/resourcepacks/locked.zip"));
            QCOMPARE(read(outside.path() + "/resourcepacks/locked.zip"), original);
        }
    }
    void disabledDependenciesNeverBlockStagedInstall_data() {
        QTest::addColumn<QString>("identity");
        QTest::addColumn<QString>("dependencyForm");
        QTest::addColumn<bool>("related");
        for (const auto &identity : {"modrinth", "curseforge", "unidentified"})
            for (const auto &form : {"project", "version", "both"})
                for (bool related : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(identity, form).arg(related)))
                        << QString(identity) << QString(form) << related;
    }
    void disabledDependenciesNeverBlockStagedInstall() {
        QFETCH(QString, identity);
        QFETCH(QString, dependencyForm);
        QFETCH(bool, related);
        QTemporaryDir temp, stage;
        const QVariantMap pack{{"path", temp.path()}};
        const QByteArray disabled = "existing disabled dependency", incoming = "selected content";
        const QString disabledPath = "mods/retained.jar.disabled";
        write(temp.path() + '/' + disabledPath, disabled);
        write(stage.path() + "/mods/selected.jar", incoming);
        NativeContentStore files(nullptr, temp.path() + "/backups");
        auto rows = files.snapshot(pack, {QVariantMap{{"relativePath", disabledPath}, {"category", "mods"}}});
        QCOMPARE(rows.size(), 1);
        auto retained = rows.first().toMap();
        retained["locked"] = true;
        if (identity != "unidentified") {
            retained["sourceIds"] = QVariantMap{{identity, "disabled-project"}};
            retained["installedRelease"] = QVariantMap{{"id", "disabled-version"}};
        }
        rows[0] = retained;
        QJsonObject dependency{{"dependency_type", "required"}};
        if (dependencyForm != "version")
            dependency["project_id"] = related ? "disabled-project" : "unrelated-project";
        if (dependencyForm != "project")
            dependency["version_id"] = related ? "disabled-version" : "unrelated-version";
        const QJsonObject manifest{
            {"mods", QJsonArray{QJsonObject{{"provider", "modrinth"},
                                            {"projectId", "selected"},
                                            {"sha1", hash(incoming)},
                                            {"filename", "selected.jar"},
                                            {"installPath", "mods/selected.jar"},
                                            {"dependencyRefs", QJsonArray{dependency}}}}}};
        const auto result = files.commitStagedContent(pack, rows, stage.path(), manifest, {});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(temp.path() + "/mods/selected.jar"), incoming);
        QCOMPARE(read(temp.path() + '/' + disabledPath), disabled);
        QVERIFY(!QFileInfo::exists(temp.path() + "/mods/retained.jar"));
    }
    void retainedFilesAreNotReplacedOrReidentified_data() {
        QTest::addColumn<bool>("duplicateIdentity");
        QTest::newRow("locked-without-project-metadata") << false;
        QTest::newRow("unrelated-files-with-one-project") << true;
    }
    void retainedFilesAreNotReplacedOrReidentified() {
        QFETCH(bool, duplicateIdentity);
        QTemporaryDir temp, stage;
        const QVariantMap pack{{"path", temp.path()}};
        QVariantList rows;
        QJsonArray refs;
        for (int i = 0; i < 2; ++i) {
            const auto bytes = QByteArray("retained") + QByteArray::number(i);
            const auto path = QString("mods/original%1.jar").arg(i);
            const auto stagedPath = "mods/" + hash(bytes) + ".jar";
            write(temp.path() + '/' + path, bytes);
            write(stage.path() + '/' + stagedPath, bytes);
            rows << QVariantMap{
                {"category", "mods"},
                {"relativePath", path},
                {"sha1", hash(bytes)},
                {"enabled", true},
                {"locked", true},
                {"sourceIds", duplicateIdentity ? QVariantMap{{"modrinth", "retained"}} : QVariantMap{}}};
            refs << QJsonObject{{"provider", "modrinth"},    {"projectId", "retained"},
                                {"sha1", hash(bytes)},       {"filename", path.section('/', -1)},
                                {"installPath", stagedPath}, {"confluxRetainedPath", path}};
        }
        write(stage.path() + "/mods/new.jar", "new");
        refs << QJsonObject{{"provider", "modrinth"},
                            {"projectId", "new"},
                            {"sha1", hash("new")},
                            {"filename", "new.jar"},
                            {"installPath", "mods/new.jar"}};
        auto locks = std::make_shared<ContentLockRegistry>();
        locks->setRows(temp.path(), rows);
        NativeContentStore files(nullptr, temp.path() + "/backups", locks);
        const auto result = files.commitStagedContent(pack, rows, stage.path(), {{"mods", refs}}, {"new"});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(temp.path() + "/mods/new.jar"), QByteArray("new"));
        for (int i = 0; i < 2; ++i) {
            const auto bytes = QByteArray("retained") + QByteArray::number(i);
            QCOMPARE(read(temp.path() + QString("/mods/original%1.jar").arg(i)), bytes);
            QVERIFY(!QFileInfo::exists(temp.path() + "/mods/" + hash(bytes) + ".jar"));
        }
    }
    void unlockedUpdatesIgnoreUnrelatedLauncherLocks() {
        QTemporaryDir temp, stage;
        const QByteArray protectedBytes = "protected pack", oldBytes = "old mod", newBytes = "new mod";
        const auto pack = sourcePack(temp.path(), protectedBytes);
        const auto root = pack.value("path").toString();
        write(root + "/resourcepacks/locked.zip", protectedBytes);
        write(root + "/mods/old.jar", oldBytes);
        write(stage.path() + "/mods/new.jar", newBytes);
        NativeContentStore files(nullptr, temp.path() + "/backups");
        auto rows = files.snapshot(
            pack, {QVariantMap{{"relativePath", "mods/old.jar"}, {"category", "mods"}},
                   QVariantMap{{"relativePath", "resourcepacks/locked.zip"}, {"category", "resourcepacks"}}});
        auto mod = rows[0].toMap();
        mod["sourceIds"] = QVariantMap{{"modrinth", "unlocked-project"}};
        rows[0] = mod;
        const QJsonObject manifest{{"mods", QJsonArray{QJsonObject{{"provider", "modrinth"},
                                                                   {"projectId", "unlocked-project"},
                                                                   {"sha1", hash(newBytes)},
                                                                   {"filename", "new.jar"},
                                                                   {"installPath", "mods/new.jar"}}}}};
        const auto result =
            files.commitStagedContent(pack, rows, stage.path(), manifest, {"unlocked-project"});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(root + "/mods/new.jar"), newBytes);
        QVERIFY(!QFileInfo::exists(root + "/mods/old.jar"));
        QCOMPARE(read(root + "/resourcepacks/locked.zip"), protectedBytes);
    }
    void changedOrAmbiguousRetainedOriginsCannotAuthorizeWrites_data() {
        QTest::addColumn<QString>("fault");
        for (const auto &fault :
             {"hash", "category", "missing", "duplicate", "also-replaced", "replaced-first"})
            QTest::newRow(fault) << QString(fault);
    }
    void changedOrAmbiguousRetainedOriginsCannotAuthorizeWrites() {
        QFETCH(QString, fault);
        QTemporaryDir temp, stage;
        write(temp.path() + "/mods/original.jar", "old");
        write(stage.path() + "/mods/retained.jar", fault == "hash" ? "changed" : "old");
        write(stage.path() + "/mods/new.jar", "new");
        const QVariantList rows{QVariantMap{{"relativePath", "mods/original.jar"},
                                            {"category", "mods"},
                                            {"sha1", hash("old")},
                                            {"enabled", true},
                                            {"sourceIds", QVariantMap{{"modrinth", "project"}}}}};
        QJsonObject ref{
            {"provider", "modrinth"},
            {"projectId", "project"},
            {"sha1", hash(fault == "hash" ? "changed" : "old")},
            {"filename", "original.jar"},
            {"installPath", "mods/retained.jar"},
            {"confluxRetainedPath", fault == "missing" ? "mods/missing.jar" : "mods/original.jar"}};
        QJsonObject manifest{{"mods", QJsonArray{ref}}};
        if (fault == "category") {
            ref["category"] = "resourcepacks";
            manifest = {{"assets", QJsonArray{ref}}};
        }
        if (fault == "duplicate")
            manifest["mods"] = QJsonArray{ref, ref};
        if (fault == "also-replaced" || fault == "replaced-first") {
            QJsonObject replacement{{"provider", "modrinth"},
                                    {"projectId", "project"},
                                    {"sha1", hash("new")},
                                    {"filename", "new.jar"},
                                    {"installPath", "mods/new.jar"}};
            manifest["mods"] =
                fault == "also-replaced" ? QJsonArray{ref, replacement} : QJsonArray{replacement, ref};
        }
        NativeContentStore files(nullptr, temp.path() + "/backups");
        const auto result =
            files.commitStagedContent({{"path", temp.path()}}, rows, stage.path(), manifest, {"project"});
        QVERIFY(!result.ok);
        QCOMPARE(read(temp.path() + "/mods/original.jar"), QByteArray("old"));
        QVERIFY(!QFileInfo::exists(temp.path() + "/mods/new.jar"));
    }
    void launcherLocksProtectAssetsAndRestoreOnlyTheExactMissingBinary() {
        QTemporaryDir temp;
        const QByteArray original = "original locked resource pack";
        const auto pack = sourcePack(temp.path(), original);
        QVERIFY(!pack.isEmpty());
        const auto path = pack.value("path").toString();
        write(path + "/resourcepacks/locked.zip", original);
        const auto database = temp.path() + "/ModrinthApp/app.db";
        const auto databaseHash = hash(read(database));
        auto locks = LauncherContentState::read(pack);
        QCOMPARE(locks.size(), 1);
        QVERIFY(locks.first().toMap().value("launcherLocked").toBool());
        const QVariantMap file{{"relativePath", "resourcepacks/locked.zip"}, {"category", "resourcepacks"}};
        NativeContentStore store(nullptr, temp.path() + "/backups");
        auto rows = store.snapshot(pack, {file});
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows.first().toMap().value("launcherLocked").toBool());
        QVERIFY(!store.remove(pack, rows).ok);
        QCOMPARE(read(path + "/resourcepacks/locked.zip"), original);
        write(temp.path() + "/backups/good/resourcepacks/locked.zip", original);
        write(temp.path() + "/backups/wrong/resourcepacks/locked.zip", "different version");
        QVERIFY(QFile::remove(path + "/resourcepacks/locked.zip"));
        QVERIFY(store.snapshot(pack, {file}).isEmpty());
        const auto result = store.restoreLocked(pack, locks.first().toMap());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(path + "/resourcepacks/locked.zip"), original);
        QCOMPARE(hash(read(database)), databaseHash);
        QVERIFY(LauncherContentState::read(pack).first().toMap().value("launcherLocked").toBool());
    }
    void failedLauncherPolicyReadsDoNotAuthorizeWrites() {
        QTemporaryDir temp;
        auto pack = sourcePack(temp.path(), "original");
        const auto path = pack.value("path").toString();
        write(path + "/resourcepacks/locked.zip", "original");
        QFile database(temp.path() + "/ModrinthApp/app.db");
        QVERIFY(database.open(QIODevice::WriteOnly | QIODevice::Truncate));
        database.write("invalid database");
        database.close();
        NativeContentStore store(nullptr, temp.path() + "/backups");
        const auto result = store.remove(pack, {QVariantMap{{"relativePath", "resourcepacks/locked.zip"},
                                                            {"category", "resourcepacks"},
                                                            {"sha1", hash("original")}}});
        QVERIFY(!result.ok);
        QCOMPARE(read(path + "/resourcepacks/locked.zip"), QByteArray("original"));
    }
    void archiveReadersWaitForAWholeInstanceTransaction() {
        QTemporaryDir temp;
        write(temp.path() + "/resourcepacks/one.zip", "old");
        NativeContentStore store(nullptr, temp.path() + "/backups");
        const QVariantMap pack{{"path", temp.path()}},
            row{{"relativePath", "resourcepacks/one.zip"}, {"category", "resourcepacks"}};
        QFuture<QVariantList> reader;
        QSemaphore entered;
        {
            ContentAccess transaction(temp.path());
            reader = QtConcurrent::run([&] {
                entered.release();
                return store.snapshot(pack, {row});
            });
            QVERIFY(entered.tryAcquire(1, 1000));
            QTest::qWait(40);
            QVERIFY(!reader.isFinished());
            write(temp.path() + "/resourcepacks/one.zip", "new complete version");
        }
        reader.waitForFinished();
        QCOMPARE(reader.result().size(), 1);
        QCOMPARE(reader.result().first().toMap().value("sha1").toString(), hash("new complete version"));
    }
    void anotherConfluxProcessCannotReadOrMutateAnInstanceMidTransaction() {
        QTemporaryDir temp;
        write(temp.path() + "/resourcepacks/one.zip", "original");
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {"--hold-content-lease", temp.path()});
        QVERIFY(process.waitForStarted(3000));
        QVERIFY(process.waitForReadyRead(3000));
        NativeContentStore store(nullptr, temp.path() + "/backups");
        const auto result =
            store.remove({{"path", temp.path()}}, {QVariantMap{{"relativePath", "resourcepacks/one.zip"},
                                                               {"sha1", hash("original")},
                                                               {"category", "resourcepacks"}}});
        process.kill();
        process.waitForFinished(2000);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("operation"));
        QCOMPARE(read(temp.path() + "/resourcepacks/one.zip"), QByteArray("original"));
    }
    void failedBatchRemovalRestoresEarlierFilesBeforeReadersResume() {
#ifndef Q_OS_WIN
        QSKIP("Native sharing-mode failure injection");
#else
        QTemporaryDir temp;
        write(temp.path() + "/resourcepacks/first.zip", "first");
        write(temp.path() + "/resourcepacks/second.zip", "second");
        NativeContentStore store(nullptr, temp.path() + "/backups");
        const QVariantMap pack{{"path", temp.path()}};
        QVariantList rows;
        for (const auto &name : {QString("first"), QString("second")})
            rows << QVariantMap{{"relativePath", "resourcepacks/" + name + ".zip"},
                                {"category", "resourcepacks"},
                                {"sha1", hash(name.toUtf8())}};
        const auto handle = CreateFileW(
            reinterpret_cast<LPCWSTR>((temp.path() + "/resourcepacks/second.zip").utf16()), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
        const auto result = store.remove(pack, rows);
        CloseHandle(handle);
        QVERIFY(!result.ok);
        QCOMPARE(read(temp.path() + "/resourcepacks/first.zip"), QByteArray("first"));
        QCOMPARE(read(temp.path() + "/resourcepacks/second.zip"), QByteArray("second"));
        QCOMPARE(store.snapshot(pack, rows).size(), 2);
#endif
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--hold-content-lease")) {
        ContentAccess lease(app.arguments().last());
        if (!lease.ok())
            return 2;
        QFile output;
        if (!output.open(stdout, QIODevice::WriteOnly))
            return 3;
        output.write("ready\n");
        output.flush();
        QTimer::singleShot(15000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    ContentTransactionTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "ContentTransactionTests.moc"
