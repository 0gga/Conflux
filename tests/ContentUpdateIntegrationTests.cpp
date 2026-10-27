#include "ContentUpdateFixtures.h"
#include "miniz.h"
#include "platform/ContentInstaller.h"
#include "platform/NativeContentStore.h"
#include "services/PackRepository.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
using namespace update_fixture;
class ContentUpdateIntegrationTests : public QObject {
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
    static QByteArray archive(const QString &path, const QString &category, const QString &id, int version) {
        mz_zip_archive zip{};
        if (!mz_zip_writer_init_file(&zip, path.toUtf8().constData(), 0))
            return {};
        const auto data =
            category == "mods"
                ? QJsonDocument(
                      QJsonObject{{"schemaVersion", 1}, {"id", id}, {"version", QString::number(version)}})
                      .toJson()
                : QByteArray("{\"pack\":{\"pack_format\":34,\"description\":\"Fixture ") + id.toUtf8() + ' ' +
                      QByteArray::number(version) + "\"}}";
        const auto name = category == "mods"          ? "fabric.mod.json"
                          : category == "shaderpacks" ? "shaders/fixture.fsh"
                                                      : "pack.mcmeta";
        mz_zip_writer_add_mem(&zip, name, data.constData(), size_t(data.size()), MZ_BEST_COMPRESSION);
        mz_zip_writer_finalize_archive(&zip);
        mz_zip_writer_end(&zip);
        return read(path);
    }
  private slots:
    void sourceReadFailureAfterCommit_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QString>("fault");
        for (const auto &provider : {QString("curseforge"), QString("modrinth")})
            for (const auto &operation : {"update", "disable", "remove", "import"})
                for (const auto &fault : {"missing", "invalid", "scope"})
                    QTest::newRow(qPrintable(provider + '-' + operation + '-' + fault))
                        << provider << QString(operation) << QString(fault);
    }
    void sourceReadFailureAfterCommit() {
        QFETCH(QString, provider);
        QFETCH(QString, operation);
        QFETCH(QString, fault);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto app = temp.path() + "/App";
        const auto root = app + "/profiles/Fixture";
        QVERIFY(QDir().mkpath(root));
        const auto foreign = temp.path() + "/Foreign";
        QVERIFY(QDir().mkpath(foreign));
        QVariantMap pack{{"path", root},
                         {"minecraft", "1.21.1"},
                         {"loader", "fabric"},
                         {"provider", provider},
                         {"launcherInstanceId", "instance"}};
        auto item = row("fixture");
        auto old = releaseFromVariant(item.value("installedRelease").toMap());
        auto next = releaseFromVariant(item.value("updateRelease").toMap());
        const auto original = archive(temp.path() + "/old.jar", "mods", "fixture", 1);
        const auto replacement = archive(temp.path() + "/new.jar", "mods", "fixture", 2);
        const auto imported = archive(temp.path() + "/imported.jar", "mods", "imported", 1);
        QVERIFY(!original.isEmpty() && !replacement.isEmpty() && !imported.isEmpty());
        old.sha1 = hash(original);
        next.sha1 = hash(replacement);
        next.fileSize = replacement.size();
        write(root + "/mods/" + old.filename, original);
        item["sha1"] = old.sha1;
        item["installedRelease"] = old.toVariant();
        item["sourceIds"] = QVariantMap{{"modrinth", "fixture"}, {"curseforge", "33"}};
        const auto source = provider == "curseforge" ? root + "/minecraftinstance.json" : app + "/app.db";
        if (provider == "curseforge") {
            write(source,
                  QJsonDocument(
                      QJsonObject{{"installedAddons",
                                   QJsonArray{QJsonObject{
                                       {"addonID", 33},
                                       {"isLocked", false},
                                       {"isEnabled", true},
                                       {"fileNameOnDisk", old.filename},
                                       {"modFolderPath", root + "/mods"},
                                       {"installedFile",
                                        QJsonObject{{"id", 333},
                                                    {"hashes", QJsonArray{QJsonObject{
                                                                   {"algo", 1}, {"value", old.sha1}}}}}}}}}})
                      .toJson());
        } else {
            const auto connection = QUuid::createUuid().toString();
            {
                auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
                db.setDatabaseName(source);
                QVERIFY(db.open());
                QSqlQuery query(db);
                for (const auto &sql :
                     {"CREATE TABLE instances(id TEXT,path TEXT)",
                      "CREATE TABLE instance_files(id TEXT,instance_id TEXT,relative_path TEXT,sha1 "
                      "TEXT,enabled INTEGER)",
                      "CREATE TABLE instance_content_entries(file_id TEXT,project_id TEXT,version_id TEXT)",
                      "CREATE TABLE instance_content_locks(file_id TEXT)",
                      "INSERT INTO instances VALUES('instance','Fixture')"})
                    QVERIFY(query.exec(sql));
                query.prepare("INSERT INTO instance_files VALUES('file','instance',?,?,1)");
                query.addBindValue("mods/" + old.filename);
                query.addBindValue(old.sha1);
                QVERIFY(query.exec());
                query.prepare("INSERT INTO instance_content_entries VALUES('file','fixture',?)");
                query.addBindValue(old.id);
                QVERIFY(query.exec());
            }
            QSqlDatabase::removeDatabase(connection);
        }
        const auto sourceBytes = read(source);
        QVERIFY(!sourceBytes.isEmpty());
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        next.url = "http://127.0.0.1:" + QString::number(server.serverPort()) + '/' + next.filename;
        item["updateRelease"] = next.toVariant();
        int downloads = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto input = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", input);
                if (!input.contains("\r\n\r\n") || socket->property("answered").toBool())
                    return;
                socket->setProperty("answered", true);
                QCOMPARE(input.split(' ').value(1), ('/' + next.filename).toUtf8());
                ++downloads;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(replacement.size()) +
                              "\r\nConnection: close\r\n\r\n" + replacement);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        struct Files final : IContentFileStore {
            std::shared_ptr<NativeContentStore> native;
            std::function<bool()> fault;
            mutable std::atomic<int> writes{0}, verificationReads{0};
            mutable std::atomic<bool> faultApplied{false};
            ContentWriteResult acknowledge(ContentWriteResult result) const {
                if (result.ok) {
                    ++writes;
                    faultApplied = fault();
                }
                return result;
            }
            QVariantList snapshot(const QVariantMap &pack, const QVariantList &rows) const override {
                return native->snapshot(pack, rows);
            }
            ContentFileState reload(const QVariantMap &pack, const QVariantList &rows,
                                    bool inspect) const override {
                if (writes > 0)
                    ++verificationReads;
                return native->reload(pack, rows, inspect);
            }
            ContentWriteResult setEnabled(const QVariantMap &pack, const QVariantMap &row,
                                          bool enabled) const override {
                return acknowledge(native->setEnabled(pack, row, enabled));
            }
            ContentWriteResult remove(const QVariantMap &pack, const QVariantList &rows) const override {
                return acknowledge(native->remove(pack, rows));
            }
            ContentWriteResult importFiles(const QVariantMap &pack, const QList<QUrl> &urls,
                                           const QString &category) const override {
                return acknowledge(native->importFiles(pack, urls, category));
            }
        };
        struct Installer final : IContentInstaller {
            std::shared_ptr<ContentInstaller> native;
            std::shared_ptr<Files> files;
            void install(const QVariantMap &pack, const QVariantList &rows,
                         const QVector<ContentInstallRequest> &requests, Callback callback) override {
                native->install(pack, rows, requests,
                                [this, callback = std::move(callback)](ContentWriteResult result) {
                                    callback(files->acknowledge(std::move(result)));
                                });
            }
            void cancel() override {
                native->cancel();
            }
            bool cancellable() const override {
                return native->cancellable();
            }
        };
        auto locks = std::make_shared<ContentLockRegistry>();
        auto files = std::make_shared<Files>();
        files->native = std::make_shared<NativeContentStore>(nullptr, temp.path() + "/backups", locks);
        files->fault = [source, provider, fault, foreign] {
            if (fault == "missing")
                return QFile::remove(source);
            if (provider == "modrinth" && fault == "scope") {
                bool ok = false;
                const auto connection = QUuid::createUuid().toString();
                {
                    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
                    db.setDatabaseName(source);
                    if (db.open()) {
                        QSqlQuery query(db);
                        query.prepare("UPDATE instances SET path=? WHERE id='instance'");
                        query.addBindValue(foreign);
                        ok = query.exec();
                    }
                }
                QSqlDatabase::removeDatabase(connection);
                return ok;
            }
            QFile file(source);
            const QByteArray bytes = fault == "scope" ? "{\"installedAddons\":{}}" : "invalid metadata";
            return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
        };
        auto installer = std::make_shared<Installer>();
        installer->files = files;
        installer->native = std::make_shared<ContentInstaller>(files->native, nullptr, true);
        ContentActions content(
            files, std::make_shared<ContentCatalogueFixture>(), installer,
            std::make_shared<SettingsContentPreferences>(temp.path() + "/preferences.ini"));
        content.setLockRegistry(locks);
        content.setContext(pack, {item});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.inventory().size(), 1);
        QCOMPARE(files->writes.load(), 0);
        const auto key = content.inventory().first().toMap().value("relativePath").toString();
        if (operation == "update")
            content.update(key);
        else if (operation == "disable")
            content.setEnabled(key, false);
        else if (operation == "remove")
            content.remove(key);
        else
            content.importFiles({QUrl::fromLocalFile(temp.path() + "/imported.jar")}, "mods");
        QTRY_VERIFY_WITH_TIMEOUT(!content.busy(), 15000);
        QCOMPARE(files->writes.load(), 1);
        QVERIFY(files->faultApplied);
        QCOMPARE(files->verificationReads.load(), 2);
        QCOMPARE(downloads, int(operation == "update"));
        const auto checkFiles = [&] {
            if (operation == "update") {
                QVERIFY(!QFileInfo::exists(root + "/mods/" + old.filename));
                QCOMPARE(read(root + "/mods/" + next.filename), replacement);
            } else if (operation == "disable") {
                QVERIFY(!QFileInfo::exists(root + "/mods/" + old.filename));
                QCOMPARE(read(root + "/mods/" + old.filename + ".disabled"), original);
            } else if (operation == "remove")
                QVERIFY(!QFileInfo::exists(root + "/mods/" + old.filename));
            else {
                QCOMPARE(read(root + "/mods/" + old.filename), original);
                QCOMPARE(read(root + "/mods/imported.jar"), imported);
            }
        };
        checkFiles();
        int done = 0, diagnostics = 0;
        QString message;
        for (const auto &value : content.operations()->entries()) {
            const auto task = value.toMap();
            if (task.value("action") == "Reading changes") {
                ++diagnostics;
                QCOMPARE(task.value("state").toString(), "failed");
                QVERIFY(!task.value("retryable").toBool());
                message = task.value("message").toString();
            } else if (task.value("state") == "done")
                ++done;
            content.operations()->retry(task.value("id").toULongLong());
        }
        QCOMPARE(done, 1);
        QCOMPARE(diagnostics, 1);
        QCOMPARE(content.operations()->failedCount(), 1);
        QVERIFY(message.contains("Changes were saved"));
        QVERIFY(message.contains(provider == "modrinth" ? "Modrinth" : "CurseForge"));
        QCOMPARE(content.notice(), message);
        write(source, sourceBytes);
        content.setContext(pack, content.inventory(), true);
        QTRY_VERIFY(!content.busy());
        QCOMPARE(files->writes.load(), 1);
        QCOMPARE(downloads, int(operation == "update"));
        QVERIFY(files->native->reload(pack, content.inventory()).error.isEmpty());
        QCOMPARE(content.inventory().size(), operation == "remove" ? 0 : operation == "import" ? 2 : 1);
        checkFiles();
        QVERIFY2(!message.contains("No changes were made"), qPrintable(message));
        QVERIFY2(!message.contains("No content was changed"), qPrintable(message));
    }
    void realTransactionsKeepPartialSuccess_data() {
        QTest::addColumn<QString>("category");
        QTest::addColumn<int>("failureAt");
        QTest::addColumn<bool>("managed");
        QTest::addColumn<bool>("verificationFailure");
        QTest::addColumn<bool>("missingRelease");
        for (const auto &type : {"mods", "resourcepacks", "shaderpacks", "datapacks", "mixed"})
            for (int index = 0; index < 3; ++index)
                for (bool managed : {false, true})
                    for (bool verificationFailure : {false, true})
                        QTest::newRow(qPrintable(QString("%1-fail%2-managed%3-readFailure%4")
                                                     .arg(type)
                                                     .arg(index)
                                                     .arg(managed)
                                                     .arg(verificationFailure)))
                            << QString(type) << index << managed << verificationFailure << false;
        for (const auto &type : {"mods", "mixed"})
            for (int index = 0; index < 3; ++index)
                QTest::newRow(qPrintable(QString("%1-missing-release-fail%2").arg(type).arg(index)))
                    << QString(type) << index << false << true << true;
    }
    void realTransactionsKeepPartialSuccess() {
        QFETCH(QString, category);
        QFETCH(int, failureAt);
        QFETCH(bool, managed);
        QFETCH(bool, verificationFailure);
        QFETCH(bool, missingRelease);
        QTemporaryDir temp;
        QVariantMap pack{{"path", temp.path() + "/ModrinthApp/profiles/Fixture"},
                         {"minecraft", "1.21.1"},
                         {"loader", "fabric"},
                         {"managed", managed}};
        if (managed) {
            const auto created = PackRepository::create(temp.path(), "Fixture", "1.21.1", "fabric");
            QVERIFY(created.ok);
            pack = created.manifest.toVariantMap();
            pack["path"] = created.path;
            pack["managed"] = true;
        }
        const auto root = pack.value("path").toString();
        QVERIFY(QDir().mkpath(root));
        const QByteArray ownerMetadata = "owner launcher data";
        write(root + "/profile.json", ownerMetadata);
        QVariantList rows;
        QJsonArray mods, assets;
        QMap<QString, QByteArray> payloads, originals;
        QStringList paths, newPaths;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        for (int i = 0; i < 3; ++i) {
            const auto type =
                category == "mixed" ? QStringList{"mods", "resourcepacks", "datapacks"}[i] : category;
            const auto id = QString("fixture%1").arg(i);
            auto item = row(id, type, i != 1);
            const auto original = archive(temp.path() + '/' + id + ".old.zip", type, id, 1);
            const auto bytes = archive(temp.path() + '/' + id + ".new.zip", type, id, 2);
            QVERIFY(!original.isEmpty());
            QVERIFY(!bytes.isEmpty());
            auto old = releaseFromVariant(item.value("installedRelease").toMap());
            old.sha1 = hash(original);
            old.fileSize = original.size();
            auto next = releaseFromVariant(item.value("updateRelease").toMap());
            next.sha1 = hash(bytes);
            next.fileSize = bytes.size();
            next.url = "http://127.0.0.1:" + QString::number(server.serverPort()) + '/' + next.filename;
            const auto path = type + '/' + (managed && type == "mods" ? old.sha1 + ".jar" : old.filename) +
                              (i == 1 ? ".disabled" : "");
            paths << path;
            newPaths << type + '/' + (managed && type == "mods" ? next.sha1 + ".jar" : next.filename) +
                            (i == 1 ? ".disabled" : "");
            write(root + '/' + path, original);
            originals[path] = original;
            payloads['/' + next.filename] = bytes;
            item["relativePath"] = path;
            item["sha1"] = old.sha1;
            if (missingRelease)
                item.remove("installedRelease");
            else
                item["installedRelease"] = old.toVariant();
            item["updateRelease"] = next.toVariant();
            rows << item;
            auto pin = QJsonObject::fromVariantMap(old.toVariant());
            pin["installPath"] = path;
            pin["projectTitle"] = id;
            pin["projectSourceIds"] = QJsonObject::fromVariantMap(item.value("sourceIds").toMap());
            pin["enabled"] = i != 1;
            if (type == "mods")
                mods << pin;
            else {
                pin["category"] = type;
                assets << pin;
            }
        }
        QString protectedPath;
        QByteArray protectedBytes, launcherMetadata;
        if (!managed) {
            auto protectedRow = row("protected", "resourcepacks");
            const auto bytes = archive(temp.path() + "/protected.zip", "resourcepacks", "protected", 1);
            auto pin = protectedRow.value("installedRelease").toMap();
            pin["sha1"] = hash(bytes);
            protectedRow["sha1"] = hash(bytes);
            protectedRow["installedRelease"] = pin;
            protectedRow["updateAvailable"] = false;
            write(root + '/' + protectedRow.value("relativePath").toString(), bytes);
            protectedPath = root + '/' + protectedRow.value("relativePath").toString();
            protectedBytes = bytes;
            rows << protectedRow;
            const auto connection = QUuid::createUuid().toString();
            {
                auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
                db.setDatabaseName(temp.path() + "/ModrinthApp/app.db");
                QVERIFY(db.open());
                QSqlQuery query(db);
                for (const auto &sql :
                     {"CREATE TABLE instances(id TEXT,path TEXT)",
                      "CREATE TABLE instance_files(id TEXT,instance_id TEXT,relative_path TEXT,sha1 "
                      "TEXT,enabled INTEGER)",
                      "CREATE TABLE instance_content_entries(file_id TEXT,project_id TEXT,version_id TEXT)",
                      "CREATE TABLE instance_content_locks(file_id TEXT)",
                      "INSERT INTO instances VALUES('instance','Fixture')"})
                    QVERIFY(query.exec(sql));
                for (int i = 0; i < rows.size(); ++i) {
                    const auto r = rows[i].toMap();
                    query.prepare("INSERT INTO instance_files VALUES(?,'instance',?,?,?)");
                    query.addBindValue(QString::number(i));
                    query.addBindValue(r.value("relativePath"));
                    query.addBindValue(r.value("sha1"));
                    query.addBindValue(r.value("enabled"));
                    QVERIFY(query.exec());
                    query.prepare("INSERT INTO instance_content_entries VALUES(?,?,?)");
                    query.addBindValue(QString::number(i));
                    query.addBindValue(r.value("projectId"));
                    query.addBindValue(r.value("installedRelease").toMap().value("id"));
                    QVERIFY(query.exec());
                }
                QVERIFY(query.exec("INSERT INTO instance_content_locks VALUES('3')"));
            }
            QSqlDatabase::removeDatabase(connection);
            launcherMetadata = read(temp.path() + "/ModrinthApp/app.db");
            pack["provider"] = "modrinth";
            pack["launcherInstanceId"] = "instance";
        }
        if (managed) {
            const auto repository = PackRepository::inspect(root);
            QVERIFY(PackRepository::updateContentReferences(root, repository.manifest, mods, assets).ok);
            pack["mods"] = mods.toVariantList();
            pack["assets"] = assets.toVariantList();
        }
        int downloads = 0;
        bool corrupt = true;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto input = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", input);
                if (!input.contains("\r\n\r\n") || socket->property("answered").toBool())
                    return;
                socket->setProperty("answered", true);
                const auto target = QString::fromUtf8(input.split(' ').value(1));
                QVERIFY2(payloads.contains(target), qPrintable("Unexpected network lookup: " + target));
                auto bytes = payloads.value(target);
                if (corrupt && target.contains(QString("fixture%1-").arg(failureAt)))
                    bytes[0] = char(bytes[0] ^ 1);
                ++downloads;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                              "\r\nConnection: close\r\n\r\n" + bytes);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        auto locks = std::make_shared<ContentLockRegistry>();
        auto files = std::make_shared<NativeContentStore>(nullptr, temp.path() + "/backups", locks);
        auto installer = std::make_shared<ContentInstaller>(files, nullptr, true);
        struct ReadBoundary final : IContentFileStore {
            std::shared_ptr<NativeContentStore> files;
            mutable std::atomic<int> failures{0};
            QVariantList snapshot(const QVariantMap &pack, const QVariantList &rows) const override {
                return files->snapshot(pack, rows);
            }
            ContentFileState reload(const QVariantMap &pack, const QVariantList &rows,
                                    bool inspect) const override {
                auto remaining = failures.load();
                while (remaining > 0)
                    if (failures.compare_exchange_weak(remaining, remaining - 1))
                        return {pack, {}, "Fixture post-commit inventory unavailable"};
                return files->reload(pack, rows, inspect);
            }
            ContentWriteResult setEnabled(const QVariantMap &pack, const QVariantMap &row,
                                          bool enabled) const override {
                return files->setEnabled(pack, row, enabled);
            }
            ContentWriteResult remove(const QVariantMap &pack, const QVariantList &rows) const override {
                return files->remove(pack, rows);
            }
            ContentWriteResult importFiles(const QVariantMap &pack, const QList<QUrl> &urls,
                                           const QString &category) const override {
                return files->importFiles(pack, urls, category);
            }
        };
        struct WriteBoundary final : IContentInstaller {
            std::shared_ptr<ContentInstaller> installer;
            std::shared_ptr<ReadBoundary> reads;
            bool armFailure = false;
            void install(const QVariantMap &pack, const QVariantList &rows,
                         const QVector<ContentInstallRequest> &requests, Callback callback) override {
                installer->install(pack, rows, requests,
                                   [this, callback = std::move(callback)](ContentWriteResult result) {
                                       if (result.ok && std::exchange(armFailure, false))
                                           reads->failures = 2;
                                       callback(std::move(result));
                                   });
            }
            void cancel() override {
                installer->cancel();
            }
            bool cancellable() const override {
                return installer->cancellable();
            }
        };
        auto reads = std::make_shared<ReadBoundary>();
        reads->files = files;
        auto writes = std::make_shared<WriteBoundary>();
        writes->installer = installer;
        writes->reads = reads;
        writes->armFailure = verificationFailure;
        ContentActions content(reads, std::make_shared<ContentCatalogueFixture>(), writes,
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        content.setLockRegistry(locks);
        content.setContext(pack, rows);
        QTRY_VERIFY(!content.busy());
        content.updateAll();
        QTRY_VERIFY_WITH_TIMEOUT(!content.busy(), 15000);
        QVERIFY2(downloads == 3,
                 qPrintable(QJsonDocument(QJsonArray::fromVariantList(content.operations()->entries()))
                                .toJson(QJsonDocument::Compact)));
        QCOMPARE(content.operations()->failedCount(), 1 + int(verificationFailure));
        if (verificationFailure) {
            int diagnostics = 0, committed = 0;
            for (const auto &value : content.operations()->entries()) {
                const auto task = value.toMap();
                if (task.value("action") == "Reading changes") {
                    ++diagnostics;
                    QVERIFY(task.value("message").toString().contains("Changes were saved"));
                    QVERIFY(!task.value("retryable").toBool());
                } else if (task.value("state") == "done")
                    ++committed;
            }
            QCOMPARE(diagnostics, 1);
            QCOMPARE(committed, 2);
        }
        for (int i = 0; i < 3; ++i) {
            if (i == failureAt) {
                QCOMPARE(read(root + '/' + paths[i]), originals[paths[i]]);
                QVERIFY(!QFileInfo::exists(root + '/' + newPaths[i]));
            } else {
                QVERIFY(!QFileInfo::exists(root + '/' + paths[i]));
                QCOMPARE(
                    read(root + '/' + newPaths[i]),
                    payloads.value(
                        '/' + rows[i].toMap().value("updateRelease").toMap().value("filename").toString()));
            }
        }
        QCOMPARE(read(root + "/profile.json"), ownerMetadata);
        if (!managed)
            QVERIFY(!QFileInfo::exists(root + "/pack.json"));
        else {
            const auto manifest = PackRepository::inspect(root).manifest;
            QVERIFY(!QJsonDocument(manifest).toJson().contains("confluxRetainedPath"));
            QCOMPARE(manifest.value("mods").toArray().size() + manifest.value("assets").toArray().size(), 3);
        }
        corrupt = false;
        content.operations()->retry(taskId(content, QString("fixture%1").arg(failureAt)));
        QTRY_VERIFY_WITH_TIMEOUT(!content.busy(), 15000);
        QVERIFY2(downloads == 4,
                 qPrintable(QJsonDocument(QJsonArray::fromVariantList(content.operations()->entries()))
                                .toJson(QJsonDocument::Compact)));

        QCOMPARE(content.operations()->failedCount(), int(verificationFailure));
        QVERIFY(!QFileInfo::exists(root + '/' + paths[failureAt]));
        QCOMPARE(
            read(root + '/' + newPaths[failureAt]),
            payloads.value(
                '/' + rows[failureAt].toMap().value("updateRelease").toMap().value("filename").toString()));
        if (!managed) {
            QCOMPARE(read(protectedPath), protectedBytes);
            QCOMPARE(read(temp.path() + "/ModrinthApp/app.db"), launcherMetadata);
        }
    }
};
QTEST_GUILESS_MAIN(ContentUpdateIntegrationTests)
#include "ContentUpdateIntegrationTests.moc"
