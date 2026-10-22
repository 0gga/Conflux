#include "platform/ContentAccess.h"
#include "platform/LauncherContentState.h"
#include "platform/NativeContentStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSemaphore>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrent>
#include <QtTest>
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
