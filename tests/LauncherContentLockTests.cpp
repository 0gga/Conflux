#include "ContentFixtures.h"
#include "features/content/ContentActions.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/CurseforgeContentState.h"
#include "platform/LauncherContentLocks.h"
#include "platform/LauncherContentState.h"
#include "platform/NativeContentStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <atomic>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
using namespace studio;
namespace {
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}
bool write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
struct Fixture {
    QTemporaryDir temp;
    QVariantMap pack, row;
    QString database, metadata, file;
    QByteArray original;
    bool sql(const QString &statement) const {
        const auto name = QUuid::createUuid().toString();
        bool ok;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(database);
            ok = db.open();
            if (ok) {
                QSqlQuery query(db);
                ok = query.exec(statement);
            }
        }
        QSqlDatabase::removeDatabase(name);
        return ok;
    }
    explicit Fixture(const QString &provider, bool enabled = true) {
        const auto root =
            temp.path() + (provider == "modrinth" ? "/ModrinthApp/profiles/Fixture" : "/CurseForge/Fixture");
        pack = {{"path", root}, {"provider", provider}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
        const auto relative = QString("mods/example.jar") + (enabled ? "" : ".disabled");
        file = root + '/' + relative;
        write(file, "original content");
        row = {{"relativePath", relative},
               {"category", "mods"},
               {"sha1", hash("original content")},
               {"sourceIds", QVariantMap{{provider, "11"}}},
               {"enabled", enabled},
               {"title", "Example"}};
        if (provider == "modrinth") {
            database = temp.path() + "/ModrinthApp/app.db";
            for (const auto &statement :
                 {"CREATE TABLE instances(id TEXT PRIMARY KEY,path TEXT)",
                  "CREATE TABLE instance_files(id TEXT PRIMARY KEY,instance_id TEXT,relative_path TEXT,sha1 "
                  "TEXT,enabled INTEGER)",
                  "CREATE TABLE instance_content_entries(file_id TEXT,project_id TEXT,version_id TEXT)",
                  "CREATE TABLE instance_content_locks(file_id TEXT PRIMARY KEY)",
                  "CREATE TABLE unrelated(value TEXT)", "INSERT INTO unrelated VALUES('keep this')",
                  "INSERT INTO instances VALUES('instance','Fixture')",
                  "INSERT INTO instance_content_entries VALUES('file','11','101')"})
                if (!sql(statement))
                    qFatal("Fixture database creation failed");
            if (!sql("INSERT INTO instance_files VALUES('file','instance','mods/example.jar','" +
                     row.value("sha1").toString() + "'," + (enabled ? "1" : "0") + ")"))
                qFatal("Fixture file creation failed");
            pack["launcherInstanceId"] = "instance";
        } else {
            metadata = root + "/minecraftinstance.json";
            const QJsonObject addon{
                {"addonID", 11},
                {"fileNameOnDisk", "example.jar"},
                {"modFolderPath", root + "/mods"},
                {"isLocked", false},
                {"isEnabled", enabled},
                {"installedFile",
                 QJsonObject{{"id", 101},
                             {"hashes", QJsonArray{QJsonObject{{"type", 1},
                                                               {"value", row.value("sha1").toString()}}}}}},
                {"unknownNested",
                 QJsonObject{{"isLocked", false}, {"text", "An escaped quote: \"isLocked\""}}}};
            original = QJsonDocument(QJsonObject{{"installedAddons", QJsonArray{addon}},
                                                 {"isUnlocked", false},
                                                 {"unknown", QJsonObject{{"preserve", "yes"}}}})
                           .toJson();
            original.replace("\n", "\r\n");
            original.prepend("\xEF\xBB\xBF");
            write(metadata, original);
        }
    }
    QVariantList source() const {
        QString error;
        auto rows = LauncherContentState::read(pack, &error);
        if (!error.isEmpty())
            qWarning() << error;
        return rows;
    }
    bool locked() const {
        const auto rows = source();
        return rows.size() == 1 && rows.first().toMap().value("launcherLocked").toBool();
    }
};
class GateLocks final : public ILauncherContentLocks {
  public:
    LauncherContentLocks native;
    mutable QSemaphore release;
    mutable std::atomic<bool> started{false};
    std::atomic<bool> hold{true}, fail{true};
    explicit GateLocks(const QString &backup) : native(backup) {}
    ContentWriteResult setLocked(const QVariantMap &pack, const QVariantList &rows,
                                 bool locked) const override {
        started = true;
        if (hold)
            release.acquire();
        if (fail)
            return {false, "Fixture source launcher write failed"};
        return native.setLocked(pack, rows, locked);
    }
};
} // namespace
class LauncherContentLockTests final : public QObject {
    Q_OBJECT
  private slots:
    void sourceDirectoryIdentityRequiresExistingDirectories() {
        Fixture fixture("modrinth");
        const auto root = fixture.pack.value("path").toString();
        QVERIFY(LauncherContentState::sameInstanceDirectory(root, root));
        QVERIFY(LauncherContentState::sameInstanceDirectory(root, root + "/."));
#ifdef Q_OS_WIN
        const auto extended = "\\\\?\\" + QDir::toNativeSeparators(root);
        QVERIFY(LauncherContentState::sameInstanceDirectory(root, extended));
        QVERIFY(LauncherContentState::sameInstanceDirectory(extended, root));
#endif
        QVERIFY(!LauncherContentState::sameInstanceDirectory(root, fixture.file));
        QVERIFY(!LauncherContentState::sameInstanceDirectory(fixture.file, fixture.file));
        QVERIFY(!LauncherContentState::sameInstanceDirectory({}, root));
        QVERIFY(!LauncherContentState::sameInstanceDirectory(root, {}));
        QVERIFY(!LauncherContentState::sameInstanceDirectory(root + "/missing", root));
        QVERIFY(!LauncherContentState::sameInstanceDirectory(root, root + "/missing"));
        const auto invalid = root + QChar(QChar::Null) + "/suffix";
        QVERIFY(!LauncherContentState::sameInstanceDirectory(invalid, root));
        QVERIFY(!LauncherContentState::sameInstanceDirectory(root, invalid));
    }
    void sourceScopeUsesTheActualProfileDirectory_data() {
        QTest::addColumn<QString>("targetName");
        QTest::addColumn<QString>("sourceName");
        QTest::addColumn<bool>("sameDirectory");
        QTest::addColumn<QString>("action");
        for (const auto &action : {"read", "lock", "unlock"}) {
#ifdef Q_OS_WIN
            QTest::newRow(qPrintable(QString("case-alias-%1").arg(action)))
                << QString("Target") << QString("TARGET") << true << QString(action);
#else
            QTest::newRow(qPrintable(QString("same-directory-%1").arg(action)))
                << QString("Target") << QString("Target") << true << QString(action);
#endif
            QTest::newRow(qPrintable(QString("distinct-sigma-%1").arg(action)))
                << QString::fromUtf16(u"\u03a3") << QString::fromUtf16(u"\u03c2") << false << QString(action);
            QTest::newRow(qPrintable(QString("distinct-kelvin-%1").arg(action)))
                << QString("K") << QString::fromUtf16(u"\u212a") << false << QString(action);
        }
    }
    void sourceScopeUsesTheActualProfileDirectory() {
        QFETCH(QString, targetName);
        QFETCH(QString, sourceName);
        QFETCH(bool, sameDirectory);
        QFETCH(QString, action);
        Fixture fixture("modrinth");
        const auto profiles = QFileInfo(fixture.database).dir().filePath("profiles");
        QVERIFY(QDir(profiles).rename("Fixture", targetName));
        const auto targetRoot = QDir(profiles).filePath(targetName);
        const auto sourceRoot = QDir(profiles).filePath(sourceName);
        const auto targetFile = targetRoot + "/mods/example.jar";
        const auto sourceFile = sourceRoot + "/mods/example.jar";
        if (!sameDirectory)
            QVERIFY(write(sourceFile, "original content"));
        fixture.pack["path"] = targetRoot;
        fixture.file = targetFile;
        QVERIFY(fixture.sql("UPDATE instances SET path='" + sourceName + "' WHERE id='instance'"));
        if (action != "lock")
            QVERIFY(fixture.sql("INSERT INTO instance_content_locks VALUES('file')"));
        const auto originalDatabase = read(fixture.database);
        if (action == "read") {
            QString error;
            const auto rows = LauncherContentState::read(fixture.pack, &error);
            QCOMPARE(rows.size(), sameDirectory ? 1 : 0);
            QCOMPARE(error.isEmpty(), sameDirectory);
            QCOMPARE(read(fixture.database), originalDatabase);
        } else {
            LauncherContentLocks locks(fixture.temp.filePath("backups"));
            const bool locked = action == "lock";
            const auto result = locks.setLocked(fixture.pack, {fixture.row}, locked);
            QCOMPARE(result.ok, sameDirectory);
            QCOMPARE(result.error.isEmpty(), sameDirectory);
            if (sameDirectory)
                QCOMPARE(fixture.locked(), locked);
            else
                QCOMPARE(read(fixture.database), originalDatabase);
        }
        QCOMPARE(read(targetFile), QByteArray("original content"));
        QCOMPARE(read(sourceFile), QByteArray("original content"));
    }
    void explicitDatabaseLocationSupportsExternalProfiles_data() {
        QTest::addColumn<bool>("longUnicodePath");
        QTest::newRow("ordinary") << false;
        QTest::newRow("long-unicode") << true;
    }
    void explicitDatabaseLocationSupportsExternalProfiles() {
        QFETCH(bool, longUnicodePath);
        Fixture fixture("modrinth");
        auto external = fixture.temp.path() + "/ExternalInstance";
        if (longUnicodePath) {
            external += QString("/long-instance-directory-component").repeated(12);
            external += QString::fromUtf16(u"/\u03a3");
            QVERIFY(external.size() > 260);
        }
        QVERIFY(write(external + "/mods/example.jar", "original content"));
        auto escaped = external;
        escaped.replace("'", "''");
        QVERIFY(fixture.sql("UPDATE instances SET path='" + escaped + "' WHERE id='instance'"));
        QVERIFY(fixture.sql("INSERT INTO instance_content_locks VALUES('file')"));
        auto pack = fixture.pack;
        pack["path"] = external;
        pack["launcherDatabasePath"] = fixture.database;
        QString error;
        const auto rows = LauncherContentState::read(pack, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows.first().toMap().value("launcherLocked").toBool());
        LauncherContentLocks locks(fixture.temp.path() + "/backups");
        const auto result = locks.setLocked(pack, rows, false);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto unlocked = LauncherContentState::read(pack, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(unlocked.size(), 1);
        QVERIFY(!unlocked.first().toMap().value("launcherLocked").toBool());
        QCOMPARE(read(external + "/mods/example.jar"), QByteArray("original content"));
        QCOMPARE(read(fixture.file), QByteArray("original content"));
    }
    void invalidExplicitDatabaseLocationNeverFallsBack_data() {
        QTest::addColumn<QString>("fault");
        for (const auto &fault : {"relative", "different-name", "missing", "empty"})
            QTest::newRow(fault) << QString(fault);
    }
    void invalidExplicitDatabaseLocationNeverFallsBack() {
        QFETCH(QString, fault);
        Fixture fixture("modrinth");
        auto pack = fixture.pack;
        pack["launcherDatabasePath"] = fault == "relative"         ? QString("app.db")
                                       : fault == "different-name" ? fixture.temp.path() + "/other.sqlite"
                                       : fault == "missing"        ? fixture.temp.path() + "/missing/app.db"
                                                                   : QString{};
        if (fault == "different-name")
            QVERIFY(write(pack.value("launcherDatabasePath").toString(), read(fixture.database)));
        const auto original = read(fixture.database);
        QString error;
        QVERIFY(LauncherContentState::read(pack, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(read(fixture.database), original);
    }
    void unavailableSourceRefreshKeepsConfirmedContentAndLocks() {
        Fixture fixture("modrinth");
        QVERIFY(fixture.sql("INSERT INTO instance_content_locks VALUES('file')"));
        const auto original = read(fixture.database);
        auto files = std::make_shared<NativeContentStore>(nullptr, fixture.temp.path() + "/backups");
        ContentActions content(
            files, std::make_shared<ContentCatalogueFixture>(), std::make_shared<ContentInstallerFixture>(),
            std::make_shared<SettingsContentPreferences>(fixture.temp.path() + "/prefs.ini"));
        content.setContext(fixture.pack, {fixture.row});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.rows().size(), 1);
        QVERIFY(content.rows().first().toMap().value("locked").toBool());
        const auto key = content.rows().first().toMap().value("contentKey").toString();
        QVERIFY(!key.isEmpty());
        QSignalSpy errors(&content, &ContentActions::operationFailed);
        const auto held = fixture.database + ".held";
        QVERIFY(QFile::rename(fixture.database, held));
        content.refresh(false);
        QTRY_VERIFY(!content.busy());
        QCOMPARE(errors.size(), 1);
        QVERIFY(!content.notice().isEmpty());
        QCOMPARE(content.rows().size(), 1);
        QCOMPARE(content.rows().first().toMap().value("contentKey").toString(), key);
        QVERIFY(content.rows().first().toMap().value("locked").toBool());
        QCOMPARE(read(fixture.file), QByteArray("original content"));
        QVERIFY(QFile::rename(held, fixture.database));
        content.refresh(false);
        QTRY_VERIFY(!content.busy());
        QCOMPARE(errors.size(), 1);
        QVERIFY(content.notice().isEmpty());
        QCOMPARE(content.rows().size(), 1);
        QCOMPARE(content.rows().first().toMap().value("contentKey").toString(), key);
        QVERIFY(content.rows().first().toMap().value("locked").toBool());
        QCOMPARE(read(fixture.database), original);
        QCOMPARE(read(fixture.file), QByteArray("original content"));
    }
    void sourceReadsValidateTheSelectedInstance_data() {
        QTest::addColumn<QString>("fault");
        for (const auto &fault : {"unknown-id", "foreign-id", "changed-path", "ambiguous-path"})
            QTest::newRow(fault) << QString(fault);
    }
    void sourceReadsValidateTheSelectedInstance() {
        QFETCH(QString, fault);
        Fixture fixture("modrinth");
        QVERIFY(fixture.sql("INSERT INTO instance_content_locks VALUES('file')"));
        auto pack = fixture.pack;
        if (fault == "unknown-id")
            pack["launcherInstanceId"] = "untracked";
        else if (fault == "foreign-id") {
            QVERIFY(QDir().mkpath(fixture.temp.path() + "/ModrinthApp/profiles/Other"));
            QVERIFY(fixture.sql("INSERT INTO instances VALUES('foreign','Other')"));
            pack["launcherInstanceId"] = "foreign";
        } else if (fault == "changed-path") {
            QVERIFY(QDir().mkpath(fixture.temp.path() + "/ModrinthApp/profiles/Other"));
            QVERIFY(fixture.sql("UPDATE instances SET path='Other' WHERE id='instance'"));
        } else {
            QVERIFY(fixture.sql("INSERT INTO instances VALUES('duplicate','Fixture')"));
            pack.remove("launcherInstanceId");
        }
        const auto original = read(fixture.database);
        QString error;
        QVERIFY(LauncherContentState::read(pack, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(read(fixture.database), original);
        QCOMPARE(read(fixture.file), QByteArray("original content"));
    }
    void validInstanceReadsSupportEmptyAndAbsolutePaths_data() {
        QTest::addColumn<bool>("absolutePath");
        QTest::addColumn<bool>("explicitId");
        QTest::addColumn<bool>("emptyInstance");
        for (bool absolute : {false, true})
            for (bool id : {false, true})
                for (bool empty : {false, true})
                    QTest::newRow(
                        qPrintable(QString("absolute-%1-id-%2-empty-%3").arg(absolute).arg(id).arg(empty)))
                        << absolute << id << empty;
    }
    void validInstanceReadsSupportEmptyAndAbsolutePaths() {
        QFETCH(bool, absolutePath);
        QFETCH(bool, explicitId);
        QFETCH(bool, emptyInstance);
        Fixture fixture("modrinth");
        QVERIFY(fixture.sql("INSERT INTO instance_content_locks VALUES('file')"));
        auto pack = fixture.pack;
        if (!explicitId)
            pack.remove("launcherInstanceId");
        if (absolutePath) {
            auto path = pack.value("path").toString();
            path.replace("'", "''");
            QVERIFY(fixture.sql("UPDATE instances SET path='" + path + "' WHERE id='instance'"));
        }
        if (emptyInstance) {
            QVERIFY(fixture.sql("DELETE FROM instance_content_locks"));
            QVERIFY(fixture.sql("DELETE FROM instance_content_entries"));
            QVERIFY(fixture.sql("DELETE FROM instance_files"));
        }
        const auto original = read(fixture.database);
        QString error = "old error";
        const auto rows = LauncherContentState::read(pack, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(rows.size(), emptyInstance ? 0 : 1);
        if (!emptyInstance)
            QVERIFY(rows.first().toMap().value("launcherLocked").toBool());
        QCOMPARE(read(fixture.database), original);
        QCOMPARE(read(fixture.file), QByteArray("original content"));
    }
    void unavailableModrinthSourceReportsAnError_data() {
        QTest::addColumn<QString>("fault");
        QTest::newRow("missing-database") << QString("database");
        QTest::newRow("unrecognized-profile-root") << QString("root");
        QTest::newRow("missing-profile-path") << QString("path");
    }
    void unavailableModrinthSourceReportsAnError() {
        QFETCH(QString, fault);
        Fixture fixture("modrinth");
        QVERIFY(fixture.sql("INSERT INTO instance_content_locks VALUES('file')"));
        QVERIFY(fixture.locked());
        auto pack = fixture.pack;
        if (fault == "database")
            QVERIFY(QFile::remove(fixture.database));
        else if (fault == "root")
            pack["path"] = fixture.temp.path();
        else
            pack.remove("path");
        QString error;
        QVERIFY(LauncherContentState::read(pack, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains("Modrinth"));
        QVERIFY(LauncherContentState::read(pack).isEmpty());
        QCOMPARE(read(fixture.file), QByteArray("original content"));
        if (fault == "database")
            QVERIFY(!QFileInfo::exists(fixture.database));
    }
    void successfulAndUnsupportedReadsClearOldErrors_data() {
        QTest::addColumn<QString>("provider");
        for (const auto &provider : {"modrinth", "curseforge", "conflux", ""})
            QTest::newRow(*provider ? provider : "no-provider") << QString(provider);
    }
    void successfulAndUnsupportedReadsClearOldErrors() {
        QFETCH(QString, provider);
        Fixture fixture(provider == "curseforge" ? "curseforge" : "modrinth");
        auto pack = fixture.pack;
        pack["provider"] = provider;
        QString error = "old failure";
        const auto rows = LauncherContentState::read(pack, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(rows.size(), provider == "modrinth" || provider == "curseforge" ? 1 : 0);
        QCOMPARE(read(fixture.file), QByteArray("original content"));
    }
    void sourceReadRecoversAfterItsDatabaseReturns() {
        Fixture fixture("modrinth");
        QVERIFY(fixture.sql("INSERT INTO instance_content_locks VALUES('file')"));
        const auto original = read(fixture.database);
        const auto held = fixture.database + ".held";
        QVERIFY(QFile::rename(fixture.database, held));
        QString error;
        QVERIFY(LauncherContentState::read(fixture.pack, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(QFile::rename(held, fixture.database));
        const auto rows = LauncherContentState::read(fixture.pack, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(rows.size(), 1);
        QVERIFY(rows.first().toMap().value("launcherLocked").toBool());
        QCOMPARE(read(fixture.database), original);
        QCOMPARE(read(fixture.file), QByteArray("original content"));
    }
    void nativeRoundTrip_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<bool>("enabled");
        for (const auto &provider : {"modrinth", "curseforge"})
            for (bool enabled : {true, false})
                QTest::newRow(qPrintable(QString("%1-%2").arg(provider).arg(enabled)))
                    << QString(provider) << enabled;
    }
    void nativeRoundTrip() {
        QFETCH(QString, provider);
        QFETCH(bool, enabled);
        Fixture fixture(provider, enabled);
        LauncherContentLocks writer(fixture.temp.path() + "/backups");
        QCOMPARE(fixture.source().size(), 1);
        QVERIFY(!fixture.locked());
        const auto result = writer.setLocked(fixture.pack, {fixture.row}, true);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(fixture.locked());
        QVERIFY(QFile::exists(result.backupPath));
        QCOMPARE(read(fixture.file), QByteArray("original content"));
        QVERIFY(writer.setLocked(fixture.pack, {fixture.row}, true).ok);
        QVERIFY(writer.setLocked(fixture.pack, {fixture.row}, false).ok);
        QVERIFY(!fixture.locked());
        if (provider == "curseforge") {
            QCOMPARE(read(fixture.metadata),
                     fixture.original); // BOM, CRLF, unknown fields and nested booleans unchanged.
            QCOMPARE(read(result.backupPath), fixture.original);
        }
    }
    void changedOrMissingFilesCanBeUnlockedButNotLocked_data() {
        nativeRoundTrip_data();
    }
    void changedOrMissingFilesCanBeUnlockedButNotLocked() {
        QFETCH(QString, provider);
        QFETCH(bool, enabled);
        Fixture fixture(provider, enabled);
        LauncherContentLocks writer(fixture.temp.path() + "/backups");
        QVERIFY(writer.setLocked(fixture.pack, {fixture.row}, true).ok);
        QVERIFY(write(fixture.file, "externally changed"));
        QVERIFY(!writer.setLocked(fixture.pack, {fixture.row}, true).ok);
        QVERIFY(fixture.locked());
        QVERIFY(writer.setLocked(fixture.pack, {fixture.row}, false).ok);
        QVERIFY(!fixture.locked());
        QVERIFY(QFile::remove(fixture.file));
        QVERIFY(writer.setLocked(fixture.pack, {fixture.row}, false).ok);
        QVERIFY(!writer.setLocked(fixture.pack, {fixture.row}, true).ok);
    }
    void modrinthTransactionRollsBackEveryLockOnFailure() {
        Fixture fixture("modrinth");
        LauncherContentLocks writer(fixture.temp.path() + "/backups");
        auto second = fixture.row;
        second["relativePath"] = "mods/second.jar";
        write(fixture.pack.value("path").toString() + "/mods/second.jar", "original content");
        QVERIFY(fixture.sql("INSERT INTO instance_files VALUES('second','instance','mods/second.jar','" +
                            second.value("sha1").toString() + "',1)"));
        QVERIFY(fixture.sql("CREATE TRIGGER reject_second BEFORE INSERT ON instance_content_locks WHEN "
                            "NEW.file_id='second' BEGIN SELECT RAISE(ABORT,'fixture rejection'); END"));
        const auto result = writer.setLocked(fixture.pack, {fixture.row, second}, true);
        QVERIFY(!result.ok);
        QVERIFY(!fixture.locked());
        for (const auto &value : fixture.source())
            QVERIFY(!value.toMap().value("launcherLocked").toBool());
    }
    void wrongInstanceAndUnsafePathsNeverModifySourceMetadata() {
        Fixture fixture("modrinth");
        LauncherContentLocks writer(fixture.temp.path() + "/backups");
        auto wrong = fixture.pack;
        wrong["launcherInstanceId"] = "other";
        QVERIFY(!writer.setLocked(wrong, {fixture.row}, true).ok);
        auto row = fixture.row;
        row["relativePath"] = "../example.jar";
        QVERIFY(!writer.setLocked(fixture.pack, {row}, false).ok);
        QVERIFY(!fixture.locked());
        QVERIFY(fixture.sql("DROP TABLE instance_content_locks"));
        QVERIFY(!writer.setLocked(fixture.pack, {fixture.row}, true).ok);
    }
    void busyModrinthDatabaseDoesNotChangeLocks() {
        Fixture fixture("modrinth");
        const auto name = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(fixture.database);
            QVERIFY(db.open());
            QSqlQuery transaction(db);
            QVERIFY(transaction.exec("BEGIN IMMEDIATE"));
            LauncherContentLocks writer(fixture.temp.path() + "/backups");
            const auto result = writer.setLocked(fixture.pack, {fixture.row}, true);
            QVERIFY(!result.ok);
            QVERIFY(result.error.contains("busy"));
            QVERIFY(transaction.exec("ROLLBACK"));
        }
        QSqlDatabase::removeDatabase(name);
        QVERIFY(!fixture.locked());
    }
    void busyCurseforgeMetadataDoesNotChangeBytes() {
#ifdef Q_OS_WIN
        Fixture fixture("curseforge");
        const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(fixture.metadata.utf16()), GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
        LauncherContentLocks writer(fixture.temp.path() + "/backups");
        const auto result = writer.setLocked(fixture.pack, {fixture.row}, true);
        CloseHandle(handle);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("busy"));
        QCOMPARE(read(fixture.metadata), fixture.original);
#endif
    }
    void duplicateCurseforgeTargetsAreIdempotent() {
        Fixture fixture("curseforge");
        QString error;
        const auto once =
            CurseforgeContentState::setLocked(fixture.original, fixture.pack, {fixture.row}, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto twice = CurseforgeContentState::setLocked(fixture.original, fixture.pack,
                                                             {fixture.row, fixture.row}, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(twice, once);
        QJsonParseError parse;
        QJsonDocument::fromJson(twice, &parse);
        QCOMPARE(parse.error, QJsonParseError::NoError);
    }
    void shadowedCurseforgeLockFieldsAreRejected() {
        Fixture fixture("curseforge");
        auto bytes = fixture.original;
        bytes.replace("\"isLocked\": false", "\"isLocked\": false, \"isLocked\": \"shadow\"");
        QString error;
        QVERIFY(
            CurseforgeContentState::setLocked(bytes, fixture.pack, {fixture.row}, true, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void reusedCurseforgeErrorOutputDoesNotPoisonValidWork() {
        Fixture fixture("curseforge");
        QString error("Previous failure");
        const auto bytes =
            CurseforgeContentState::setLocked(fixture.original, fixture.pack, {fixture.row}, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!bytes.isEmpty());
        error = "Previous failure";
        QCOMPARE(CurseforgeContentState::read(fixture.original, fixture.pack, &error).size(), 1);
        QVERIFY(error.isEmpty());
    }
    void nullErrorAndEscapedLockKeysAreSafe() {
        Fixture fixture("curseforge");
        QVERIFY(CurseforgeContentState::setLocked("{broken", fixture.pack, {fixture.row}, true, nullptr)
                    .isEmpty());
        auto escaped = fixture.original;
        escaped.replace("\"isLocked\": false", "\"is\\u004cocked\": false");
        QString error;
        const auto output =
            CurseforgeContentState::setLocked(escaped, fixture.pack, {fixture.row}, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto addons = QJsonDocument::fromJson(output).object().value("installedAddons").toArray();
        QVERIFY(addons.first().toObject().value("isLocked").toBool());
        QVERIFY(!addons.first().toObject().value("unknownNested").toObject().value("isLocked").toBool());
        auto duplicateRoot = fixture.original;
        duplicateRoot.insert(duplicateRoot.indexOf('{') + 1, "\"installedAddons\":null,");
        QVERIFY(CurseforgeContentState::setLocked(duplicateRoot, fixture.pack, {fixture.row}, true, &error)
                    .isEmpty());
        QVERIFY(error.contains("ambiguous"));
    }
    void largeCurseforgeBatchUsesOneLosslessOutputPass() {
        const QVariantMap pack{{"path", "C:/Instances/Large"}};
        QJsonArray addons, expected;
        QVariantList targets;
        for (int i = 0; i < 5000; ++i) {
            const auto name = QString("mod-%1.jar").arg(i);
            QJsonObject addon{{"addonID", i + 1},
                              {"modFolderPath", "C:/Instances/Large/mods"},
                              {"fileNameOnDisk", name},
                              {"isLocked", false},
                              {"unknown", QJsonObject{{"preserve", "unchanged"}, {"isLocked", false}}}};
            addons << addon;
            if (i % 5 == 0) {
                const QVariantMap target{{"relativePath", "mods/" + name},
                                         {"sourceIds", QVariantMap{{"curseforge", QString::number(i + 1)}}}};
                targets.prepend(target);
                if (i % 10 == 0)
                    targets.prepend(target); // Repeated targets remain idempotent.
                addon["isLocked"] = true;
            }
            expected << addon;
        }
        const auto original =
            QJsonDocument(QJsonObject{{"installedAddons", addons}}).toJson(QJsonDocument::Compact);
        QString error;
        QElapsedTimer elapsed;
        elapsed.start();
        const auto output = CurseforgeContentState::setLocked(original, pack, targets, true, &error);
        const auto duration = elapsed.elapsed();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(output,
                 QJsonDocument(QJsonObject{{"installedAddons", expected}}).toJson(QJsonDocument::Compact));
        qInfo() << "5000 addons / 1500 target entries / 1000 unique lock changes ms" << duration;
        QVERIFY(duration < 3000);
    }
    void curseforgeAmbiguousAndMalformedMetadataRemainByteExact() {
        Fixture fixture("curseforge");
        LauncherContentLocks writer(fixture.temp.path() + "/backups");
        auto object = QJsonDocument::fromJson(fixture.original).object();
        auto addons = object.value("installedAddons").toArray();
        addons << addons.first();
        object["installedAddons"] = addons;
        const auto duplicate = QJsonDocument(object).toJson();
        write(fixture.metadata, duplicate);
        QVERIFY(!writer.setLocked(fixture.pack, {fixture.row}, true).ok);
        QCOMPARE(read(fixture.metadata), duplicate);
        write(fixture.metadata, "{broken");
        QVERIFY(!writer.setLocked(fixture.pack, {fixture.row}, true).ok);
        QCOMPARE(read(fixture.metadata), QByteArray("{broken"));
    }
    void optimisticUiRollsBackFailuresAndTracksExternalSourceLocks_data() {
        QTest::addColumn<QString>("provider");
        QTest::newRow("modrinth") << QString("modrinth");
        QTest::newRow("curseforge") << QString("curseforge");
    }
    void optimisticUiRollsBackFailuresAndTracksExternalSourceLocks() {
        QFETCH(QString, provider);
        Fixture fixture(provider);
        auto files = std::make_shared<NativeContentStore>(nullptr, fixture.temp.path() + "/backups");
        auto preferences =
            std::make_shared<SettingsContentPreferences>(fixture.temp.path() + "/preferences.ini");
        auto locks = std::make_shared<GateLocks>(fixture.temp.path() + "/backups");
        ContentActions content(files, std::make_shared<ContentCatalogueFixture>(),
                               std::make_shared<ContentInstallerFixture>(), preferences);
        content.setLauncherLocks(locks);
        content.setContext(fixture.pack, {fixture.row});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.rows().size(), 1);
        QVERIFY(!content.rows().first().toMap().value("locked").toBool());
        QSignalSpy errors(&content, &ContentActions::operationFailed),
            resets(content.model(), &QAbstractItemModel::modelReset);
        content.setLocked(fixture.row.value("relativePath").toString(), true);
        QVERIFY(content.rows().first().toMap().value("locked").toBool()); // Before the worker is released.
        QTRY_VERIFY(locks->started.load());
        locks->release.release();
        QTRY_VERIFY(!content.busy());
        QCOMPARE(errors.size(), 1);
        QVERIFY(!fixture.locked());
        QVERIFY(!content.rows().first().toMap().value("locked").toBool());
        locks->hold = false;
        locks->fail = false;
        content.setLocked(fixture.row.value("relativePath").toString(), true);
        QTRY_VERIFY(!content.busy());
        QVERIFY(fixture.locked());
        QVERIFY(content.rows().first().toMap().value("locked").toBool());
        QVERIFY(!preferences->locked(fixture.pack.value("path").toString(),
                                     provider + ":11")); // Source is authoritative.
        QVERIFY(locks->native.setLocked(fixture.pack, {fixture.row}, false).ok);
        content.refresh(false);
        QTRY_VERIFY(!content.busy());
        QVERIFY(!content.rows().first().toMap().value("locked").toBool());
        QVERIFY(locks->native.setLocked(fixture.pack, {fixture.row}, true).ok);
        QVERIFY(QFile::remove(fixture.file));
        content.refresh(false);
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.rows().size(), 0);
        QVariantMap project{{"sourceIds", fixture.row.value("sourceIds")}};
        QVERIFY(content.projectLocked(project));
        content.setProjectLocked(project, false);
        QTRY_VERIFY(!content.busy());
        QVERIFY(!fixture.locked());
        QVERIFY(!content.projectLocked(project));
        QCOMPARE(resets.size(), 0);
    }
};
QTEST_GUILESS_MAIN(LauncherContentLockTests)
#include "LauncherContentLockTests.moc"
