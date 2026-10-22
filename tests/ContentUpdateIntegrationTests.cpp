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
    void realTransactionsKeepPartialSuccess_data() {
        QTest::addColumn<QString>("category");
        QTest::addColumn<int>("failureAt");
        QTest::addColumn<bool>("managed");
        for (const auto &type : {"mods", "resourcepacks", "shaderpacks", "datapacks", "mixed"})
            for (int index = 0; index < 3; ++index)
                for (bool managed : {false, true})
                    QTest::newRow(
                        qPrintable(QString("%1-fail%2-managed%3").arg(type).arg(index).arg(managed)))
                        << QString(type) << index << managed;
    }
    void realTransactionsKeepPartialSuccess() {
        QFETCH(QString, category);
        QFETCH(int, failureAt);
        QFETCH(bool, managed);
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
        ContentActions content(files, std::make_shared<ContentCatalogueFixture>(), installer,
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        content.setLockRegistry(locks);
        content.setContext(pack, rows);
        QTRY_VERIFY(!content.busy());
        content.updateAll();
        QTRY_VERIFY_WITH_TIMEOUT(!content.busy(), 15000);
        QVERIFY2(downloads == 3,
                 qPrintable(QJsonDocument(QJsonArray::fromVariantList(content.operations()->entries()))
                                .toJson(QJsonDocument::Compact)));
        QCOMPARE(content.operations()->failedCount(), 1);
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
        QCOMPARE(downloads, 4);
        QCOMPARE(content.operations()->failedCount(), 0);
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
