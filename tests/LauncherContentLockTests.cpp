#include "ContentFixtures.h"
#include "features/content/ContentActions.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/CurseforgeContentState.h"
#include "platform/LauncherContentLocks.h"
#include "platform/LauncherContentState.h"
#include "platform/NativeContentStore.h"
#include <QCryptographicHash>
#include <QDir>
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
