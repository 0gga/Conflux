#include "miniz.h"
#include "platform/AssetInstaller.h"
#include "platform/ContentInstaller.h"
#include "services/PackRepository.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
using namespace studio;
class AssetInstallerTests : public QObject {
    Q_OBJECT
    static QByteArray zip(const QString &path, const QByteArray &description = "Fixture") {
        mz_zip_archive archive{};
        if (!mz_zip_writer_init_file(&archive, path.toUtf8().constData(), 0))
            return {};
        const QByteArray data = "{\"pack\":{\"pack_format\":34,\"description\":\"" + description + "\"}}";
        mz_zip_writer_add_mem(&archive, "pack.mcmeta", data.constData(), size_t(data.size()),
                              MZ_BEST_COMPRESSION);
        mz_zip_writer_finalize_archive(&archive);
        mz_zip_writer_end(&archive);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
    static void write(const QString &path, const QByteArray &bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(bytes), bytes.size());
    }
    static QByteArray read(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return f.readAll();
    }
    ContentWriteResult download(const QVariantMap &pack, const QVariantList &rows,
                                ContentInstallRequest request, const QByteArray &bytes,
                                bool corrupt = false) {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0))
            return {false, "Fixture server failed"};
        connect(&server, &QTcpServer::newConnection, &server, [&server, bytes] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, bytes] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                              "\r\nConnection: close\r\n\r\n" + bytes);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        request.release.url = "http://127.0.0.1:" + QString::number(server.serverPort()) + "/pack.zip";
        request.release.fileSize = bytes.size();
        request.release.sha1 =
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
        if (corrupt)
            request.release.sha1 = QString(40, '0');
        auto files =
            std::make_shared<NativeContentStore>(nullptr, pack.value("path").toString() + "/test-backups");
        AssetInstaller installer(files, nullptr, true);
        ContentWriteResult result;
        bool done = false;
        installer.install(pack, rows, request, [&](ContentWriteResult value) {
            result = value;
            done = true;
        });
        QElapsedTimer timer;
        timer.start();
        while (!done && timer.elapsed() < 10000)
            QTest::qWait(10);
        return done ? result : ContentWriteResult{false, "Fixture download timed out"};
    }
    static ContentInstallRequest request(const QString &type = "resourcepack") {
        ContentInstallRequest r;
        r.contentType = type;
        r.title = "Fixture";
        r.sourceIds = {{"modrinth", "project"}};
        r.release.provider = "modrinth";
        r.release.projectId = "project";
        r.release.id = "version";
        r.release.version = "1.0";
        r.release.filename = "fixture.zip";
        r.release.gameVersions = {"1.21.1"};
        return r;
    }
  private slots:
    void mixedZipQueuePreservesManagedAssetPins_data() {
        QTest::addColumn<bool>("failLast");
        QTest::newRow("complete") << false;
        QTest::newRow("partial-failure") << true;
    }
    void mixedZipQueuePreservesManagedAssetPins() {
        QFETCH(bool, failLast);
        QTemporaryDir temp;
        const auto bytes = zip(temp.path() + "/fixture.zip");
        QVERIFY(!bytes.isEmpty());
        const auto created = PackRepository::create(temp.path(), "Assets", "1.21.1", "fabric");
        QVERIFY(created.ok);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, bytes] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                              "\r\nConnection: close\r\n\r\n" + bytes);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        QVector<ContentInstallRequest> requests;
        for (const auto &type : QStringList{"resourcepack", "datapack"}) {
            Release r;
            r.provider = "modrinth";
            r.projectId = type;
            r.id = type + "-2";
            r.filename = type + ".zip";
            r.gameVersions = {"1.21.1"};
            r.url = "http://127.0.0.1:" + QString::number(server.serverPort()) + "/file.zip";
            r.fileSize = bytes.size();
            r.sha1 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
            requests << ContentInstallRequest{r, type, QJsonObject{{"modrinth", type}}, type};
        }
        if (failLast) {
            auto bad = requests.first();
            bad.release.projectId = "bad";
            bad.release.sha1 = QString(40, '0');
            requests << bad;
        }
        auto files = std::make_shared<NativeContentStore>(nullptr, temp.path() + "/backups");
        ContentInstaller installer(files, nullptr, true);
        auto pack = created.manifest.toVariantMap();
        pack["path"] = created.path;
        pack["managed"] = true;
        ContentWriteResult result;
        bool done = false;
        installer.install(pack, {}, requests, [&](ContentWriteResult value) {
            result = value;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(result.ok, !failLast);
        QVERIFY(result.changed);
        QCOMPARE(read(created.path + "/resourcepacks/resourcepack.zip"), bytes);
        QCOMPARE(read(created.path + "/datapacks/datapack.zip"), bytes);
        QCOMPARE(PackRepository::inspect(created.path).manifest.value("assets").toArray().size(), 2);
    }
    void verifiedZipInstallsWithExactManagedPinsAndGitExclusions() {
        QTemporaryDir temp;
        const auto created = PackRepository::create(temp.path(), "Pack", "1.21.1", "fabric");
        QVERIFY(created.ok);
        auto pack = created.manifest.toVariantMap();
        pack["path"] = created.path;
        pack["managed"] = true;
        const auto bytes = zip(temp.path() + "/input.zip");
        QVERIFY(!bytes.isEmpty());
        const auto result = download(pack, {}, request(), bytes);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(created.path + "/resourcepacks/fixture.zip"), bytes);
        const auto manifest = PackRepository::inspect(created.path).manifest;
        QCOMPARE(manifest.value("assets").toArray().size(), 1);
        QCOMPARE(manifest.value("assets").toArray().first().toObject().value("projectId").toString(),
                 QString("project"));
        QVERIFY(read(created.path + "/.gitignore").contains("resourcepacks/"));
    }
    void assetVersionReplacementPreservesDisabledStateAndExactPins() {
        QTemporaryDir temp;
        const auto created = PackRepository::create(temp.path(), "Assets", "1.21.1", "fabric");
        QVERIFY(created.ok);
        auto pack = created.manifest.toVariantMap();
        pack["path"] = created.path;
        pack["managed"] = true;
        const auto original = zip(temp.path() + "/original.zip");
        QVERIFY(download(pack, {}, request(), original).ok);
        pack = PackRepository::inspect(created.path).manifest.toVariantMap();
        pack["path"] = created.path;
        pack["managed"] = true;
        QVariantMap old{{"category", "resourcepacks"},
                        {"relativePath", "resourcepacks/fixture.zip"},
                        {"sha1", QString::fromLatin1(
                                     QCryptographicHash::hash(original, QCryptographicHash::Sha1).toHex())},
                        {"enabled", true},
                        {"sourceIds", QVariantMap{{"modrinth", "project"}}}};
        NativeContentStore files(nullptr, temp.path() + "/backups");
        QVERIFY(files.setEnabled(pack, old, false).ok);
        pack = PackRepository::inspect(created.path).manifest.toVariantMap();
        pack["path"] = created.path;
        pack["managed"] = true;
        old["relativePath"] = "resourcepacks/fixture.zip.disabled";
        old["enabled"] = false;
        const auto replacement = zip(temp.path() + "/replacement.zip", "Replacement");
        QVERIFY(original != replacement);
        auto next = request();
        next.release.filename = "replacement.zip";
        next.release.id = "version-two";
        const auto result = download(pack, {old}, next, replacement);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(created.path + "/resourcepacks/replacement.zip.disabled"), replacement);
        QVERIFY(!QFileInfo::exists(created.path + "/resourcepacks/fixture.zip.disabled"));
        const auto assets = PackRepository::inspect(created.path).manifest.value("assets").toArray();
        QCOMPARE(assets.size(), 1);
        QCOMPARE(assets.first().toObject().value("id").toString(), QString("version-two"));
        QCOMPARE(assets.first().toObject().value("enabled").toBool(), false);
    }
    void corruptDownloadDoesNotCreateInstalledFiles() {
        QTemporaryDir temp;
        const auto bytes = zip(temp.path() + "/input.zip");
        const auto result =
            download({{"path", temp.path()}, {"minecraft", "1.21.1"}}, {}, request(), bytes, true);
        QVERIFY(!result.ok);
        QVERIFY(!QFileInfo::exists(temp.path() + "/resourcepacks/fixture.zip"));
    }
    void unsafeArchiveFilenamesCannotBecomeWindowsAlternateStreams() {
        QTemporaryDir temp;
        const auto bytes = zip(temp.path() + "/input.zip");
        auto r = request();
        r.release.filename = "existing.zip:payload.zip";
        const auto result = download({{"path", temp.path()}, {"minecraft", "1.21.1"}}, {}, r, bytes);
        QVERIFY(!result.ok);
        QVERIFY(!QFileInfo::exists(temp.path() + "/resourcepacks"));
    }
    void dataPackInstallsAtTheInstanceRootAndNeverModifiesSaves() {
        QTemporaryDir temp;
        const auto bytes = zip(temp.path() + "/input.zip");
        write(temp.path() + "/saves/one/level.dat", "world bytes");
        write(temp.path() + "/saves/two/level.dat", "second world");
        auto r = request("datapack");

        const auto result = download({{"path", temp.path()}, {"minecraft", "1.21.1"}}, {}, r, bytes);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(temp.path() + "/datapacks/fixture.zip"), bytes);
        QCOMPARE(read(temp.path() + "/saves/one/level.dat"), QByteArray("world bytes"));
        QVERIFY(!QFileInfo::exists(temp.path() + "/saves/one/datapacks/fixture.zip"));
        QVERIFY(!QFileInfo::exists(temp.path() + "/saves/two/datapacks/fixture.zip"));
        write(temp.path() + "/saves/one/datapacks/existing.zip", bytes);
        NativeContentStore store(nullptr, temp.path() + "/backups");
        const auto changed = store.setEnabled(
            {{"path", temp.path()}},
            {{"relativePath", "saves/one/datapacks/existing.zip"},
             {"sha1",
              QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex())}},
            false);
        QVERIFY(!changed.ok);
        QVERIFY(changed.error.contains("Unsafe"));
        QCOMPARE(read(temp.path() + "/saves/one/datapacks/existing.zip"), bytes);
    }
};
QTEST_GUILESS_MAIN(AssetInstallerTests)
#include "AssetInstallerTests.moc"
