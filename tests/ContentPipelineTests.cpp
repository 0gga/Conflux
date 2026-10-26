#include "TestPerformance.h"
#include "miniz.h"
#include "platform/ContentFileCache.h"
#include "platform/ContentInstaller.h"
#include "platform/InstalledModIdentity.h"
#include "services/PackContent.h"
#include "services/PackRepository.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrent>
#include <QtTest>
#include <filesystem>
using namespace studio;
class ContentPipelineTests : public QObject {
    Q_OBJECT
    static QString hash(const QByteArray &bytes) {
        return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
    }
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
    static QByteArray archive(const QString &path, const QString &type, const QString &id, int version,
                              int padding = 0, bool neoForge = false) {
        mz_zip_archive zip{};
        if (!mz_zip_writer_init_file(&zip, QFile::encodeName(path).constData(), 0))
            return {};
        const auto data = type == "mods" && neoForge
                              ? QString("modLoader=\"javafml\"\nloaderVersion=\"[1,)\"\nlicense=\"MIT\"\n[["
                                        "mods]]\nmodId=\"%1\"\nversion=\"%2\"\ndisplayName=\"%1\"\n")
                                    .arg(id)
                                    .arg(version)
                                    .toUtf8()
                          : type == "mods" ? QJsonDocument(QJsonObject{{"schemaVersion", 1},
                                                                       {"id", id},
                                                                       {"name", id},
                                                                       {"version", QString::number(version)}})
                                                 .toJson()
                                           : QByteArray("{\"pack\":{\"pack_format\":34,\"description\":\"") +
                                                 id.toUtf8() + QByteArray::number(version) + "\"}}";
        const auto name = type == "mods" && neoForge ? "META-INF/neoforge.mods.toml"
                          : type == "mods"           ? "fabric.mod.json"
                          : type == "shaderpacks"    ? "shaders/test.fsh"
                                                     : "pack.mcmeta";
        mz_zip_writer_add_mem(&zip, name, data.data(), size_t(data.size()), 0);
        if (padding) {
            QByteArray blob(padding, 'x');
            mz_zip_writer_add_mem(&zip, "payload.bin", blob.data(), size_t(blob.size()), 0);
        }
        mz_zip_writer_finalize_archive(&zip);
        mz_zip_writer_end(&zip);
        return read(path);
    }
    static Release release(const QString &id, const QByteArray &bytes, const QString &filename) {
        Release r;
        r.provider = "modrinth";
        r.projectId = id;
        r.id = id + "-2";
        r.version = "2";
        r.filename = filename;
        r.sha1 = hash(bytes);
        r.fileSize = bytes.size();
        r.gameVersions = {"1.21.1"};
        r.loaders = {"fabric"};
        return r;
    }
    struct Server : QTcpServer {
        QMap<QString, QByteArray> responses;
        QStringList requests;
        bool stall = false;
        Server() {
            listen(QHostAddress::LocalHost);
            connect(this, &QTcpServer::newConnection, this, [this] {
                while (hasPendingConnections()) {
                    auto *socket = nextPendingConnection();
                    connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                        auto bytes = socket->property("request").toByteArray() + socket->readAll();
                        socket->setProperty("request", bytes);
                        if (!bytes.contains("\r\n\r\n") || socket->property("done").toBool())
                            return;
                        socket->setProperty("done", true);
                        auto path = QString::fromUtf8(bytes.split(' ').value(1));
                        requests << path;
                        if (stall)
                            return;
                        const auto body = responses.value(path);
                        socket->write("HTTP/1.1 " +
                                      QByteArray(responses.contains(path) ? "200 OK" : "404 Missing") +
                                      "\r\nContent-Length: " + QByteArray::number(body.size()) +
                                      "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                }
            });
        }
        QUrl base() const {
            return QUrl("http://127.0.0.1:" + QString::number(serverPort()) + '/');
        }
    };
  private slots:
    void snapshotPinsRequireTheMatchingCategoryPathAndHash_data() {
        QTest::addColumn<QString>("category");
        QTest::addColumn<QString>("fault");
        for (const auto &category : {"mods", "resourcepacks", "shaderpacks", "datapacks"})
            for (const auto &fault : {"none", "absent", "category", "path", "hash", "hash-case", "path-case"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(category, fault)))
                    << QString(category) << QString(fault);
    }
    void snapshotPinsRequireTheMatchingCategoryPathAndHash() {
        QFETCH(QString, category);
        QFETCH(QString, fault);
        QTemporaryDir temp;
        const QByteArray bytes = "verified " + category.toUtf8();
        const auto relative = category + (category == "mods" ? "/example.jar" : "/example.zip");
        write(temp.filePath(relative), bytes);
        QVariantMap pin{{"installPath", relative}, {"sha1", hash(bytes)},
                        {"provider", "modrinth"},  {"projectId", "pinned-project"},
                        {"id", "pinned-release"},  {"projectTitle", "Pinned title"},
                        {"version", "2"}};
        if (fault == "path")
            pin["installPath"] = relative + ".disabled";
        if (fault == "hash")
            pin["sha1"] = hash("different binary");
        if (fault == "hash-case")
            pin["sha1"] = pin.value("sha1").toString().toUpper();
        if (fault == "path-case")
            pin["installPath"] = relative.toUpper();
        auto kind = category == "mods" ? QString("mods") : QString("assets");
        if (fault == "category")
            kind = kind == "mods" ? "assets" : "mods";
        QVariantMap pack{{"path", temp.path()}, {"provider", "conflux"}};
        if (fault != "absent")
            pack[kind] = QVariantList{pin};
        NativeContentStore store(nullptr, temp.filePath("backups"));
        const auto result = store.snapshot(
            pack,
            {QVariantMap{{"relativePath", relative}, {"category", category}, {"title", "Local title"}}});
        QCOMPARE(result.size(), 1);
        bool matches = fault == "none";
#ifdef Q_OS_WIN
        matches |= fault == "path-case";
#endif
        const auto actual = result.first().toMap();
        QCOMPARE(actual.contains("installedRelease"), matches);
        QCOMPARE(actual.value("external").toBool(), !matches);
        QCOMPARE(actual.value("title").toString(),
                 matches ? QString("Pinned title") : QString("Local title"));
        QCOMPARE(actual.value("sha1").toString(), hash(bytes));
        if (matches) {
            QCOMPARE(actual.value("installedRelease").toMap(), pin);
            QCOMPARE(actual.value("sourceIds").toMap(), (QVariantMap{{"modrinth", "pinned-project"}}));
            QCOMPARE(actual.value("version").toString(), QString("2"));
        }
    }
    void snapshotPinsPreserveDuplicateOrderAndMappedIdentities_data() {
        QTest::addColumn<bool>("finalTitle");
        QTest::newRow("last-title-overrides") << true;
        QTest::newRow("missing-title-retains-prior") << false;
    }
    void snapshotPinsPreserveDuplicateOrderAndMappedIdentities() {
        QFETCH(bool, finalTitle);
        QTemporaryDir temp;
        const QByteArray bytes = "shared exact file";
        const QString relative = "mods/example.jar";
        write(temp.filePath(relative), bytes);
        const QVariantMap first{{"installPath", relative}, {"sha1", hash(bytes)},
                                {"provider", "modrinth"},  {"projectId", "primary"},
                                {"id", "first"},           {"projectTitle", "First title"}};
        QVariantMap last{{"installPath", relative},
                         {"sha1", hash(bytes)},
                         {"provider", "curseforge"},
                         {"projectId", "42"},
                         {"id", "second"},
                         {"version", "2"},
                         {"projectSourceIds", QVariantMap{{"modrinth", "primary"}}}};
        if (finalTitle)
            last["projectTitle"] = "Final title";
        auto stale = first;
        stale["sha1"] = hash("different file");
        stale["projectTitle"] = "Must not apply";
        const QVariantMap pack{{"path", temp.path()}, {"mods", QVariantList{stale, first, last, stale}}};
        NativeContentStore store(nullptr, temp.filePath("backups"));
        const auto rows = store.snapshot(
            pack, {QVariantMap{{"relativePath", relative}, {"category", "mods"}, {"title", "Local title"}}});
        QCOMPARE(rows.size(), 1);
        const auto actual = rows.first().toMap();
        QCOMPARE(actual.value("installedRelease").toMap(), last);
        QCOMPARE(actual.value("sourceIds").toMap(),
                 (QVariantMap{{"modrinth", "primary"}, {"curseforge", "42"}}));
        QCOMPARE(actual.value("provider").toString(), QString("curseforge"));
        QCOMPARE(actual.value("projectId").toString(), QString("42"));
        QCOMPARE(actual.value("title").toString(),
                 finalTitle ? QString("Final title") : QString("First title"));
    }
    void warmSnapshotPinAssociationHasBoundedCpuAndNoExtraHashIo_data() {
        QTest::addColumn<int>("count");
        QTest::newRow("owner-sized-310") << 310;
        QTest::newRow("large-3000") << 3000;
    }
    void warmSnapshotPinAssociationHasBoundedCpuAndNoExtraHashIo() {
        QFETCH(int, count);
        QTemporaryDir temp;
        const QByteArray bytes(256, 'x');
        QVariantList files, pins;
        files.reserve(count);
        pins.reserve(count);
        for (int i = 0; i < count; ++i) {
            const auto relative = QString("mods/file-%1.jar").arg(i);
            write(temp.filePath(relative), bytes);
            files << QVariantMap{{"relativePath", relative}, {"category", "mods"}, {"title", "Local"}};
            pins << QVariantMap{{"installPath", relative}, {"sha1", hash(bytes)},
                                {"provider", "modrinth"},  {"projectId", QString("project-%1").arg(i)},
                                {"id", "version"},         {"projectTitle", QString("Pinned %1").arg(i)},
                                {"version", "1"}};
        }
        const QVariantMap plain{{"path", temp.path()}, {"provider", "conflux"}};
        auto managed = plain;
        managed["mods"] = pins;
        NativeContentStore store(nullptr, temp.filePath("backups"));
        ContentFileCache::instance().clear();
        QCOMPARE(store.snapshot(plain, files).size(), count);
        const auto before = ContentFileCache::instance().statistics();
        QElapsedTimer clock;
        clock.start();
        const auto rows = store.snapshot(managed, files);
        const auto elapsed = clock.elapsed();
        QCOMPARE(rows.size(), count);
        for (int i = 0; i < count; ++i) {
            const auto actual = rows[i].toMap();
            QCOMPARE(actual.value("projectId").toString(), QString("project-%1").arg(i));
            QCOMPARE(actual.value("title").toString(), QString("Pinned %1").arg(i));
            QCOMPARE(actual.value("sourceIds").toMap().value("modrinth").toString(),
                     QString("project-%1").arg(i));
        }
        const auto after = ContentFileCache::instance().statistics();
        QCOMPARE(after.hashedFiles, before.hashedFiles);
        QCOMPARE(after.hashedBytes, before.hashedBytes);
        qInfo() << "Native snapshot pins/elapsed ms/extra hashed bytes" << count << elapsed
                << (after.hashedBytes - before.hashedBytes);
        QVERIFY2(
            test_performance::releaseBudgetMet(elapsed, count == 310 ? 350 : 1500),
            qPrintable(
                QString("Native snapshot pin association took %1ms for %2 rows").arg(elapsed).arg(count)));
    }
    void init() {
        ContentFileCache::instance().clear();
    }
    void sameNameReplacementIsAtomicAndHonorsLocks_data() {
        QTest::addColumn<QString>("category");
        QTest::addColumn<bool>("managed");
        QTest::addColumn<bool>("identical");
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<bool>("locked");
        for (auto type : {"mods", "resourcepacks", "shaderpacks", "datapacks"})
            for (bool managed : {false, true})
                for (bool identical : {false, true})
                    for (bool enabled : {false, true})
                        for (bool locked : {false, true})
                            QTest::newRow(qPrintable(QString("%1-managed%2-identical%3-enabled%4-locked%5")
                                                         .arg(type)
                                                         .arg(managed)
                                                         .arg(identical)
                                                         .arg(enabled)
                                                         .arg(locked)))
                                << QString(type) << managed << identical << enabled << locked;
    }
    void sameNameReplacementIsAtomicAndHonorsLocks() {
        QFETCH(QString, category);
        QFETCH(bool, managed);
        QFETCH(bool, identical);
        QFETCH(bool, enabled);
        QFETCH(bool, locked);
        QTemporaryDir temp, stage;
        QVariantMap pack{{"path", temp.path() + "/instance"}, {"loader", "fabric"}, {"minecraft", "1.21.1"}};
        if (managed) {
            const auto created = PackRepository::create(temp.path(), "Fixture", "1.21.1", "fabric");
            QVERIFY(created.ok);
            pack = created.manifest.toVariantMap();
            pack["path"] = created.path;
            pack["managed"] = true;
        }
        const auto oldBytes = archive(temp.path() + "/old.zip", category, "fixture", 1),
                   newBytes = archive(temp.path() + "/new.zip", category, "fixture", 2);
        const auto destinationBytes =
            identical ? newBytes : archive(temp.path() + "/other.zip", category, "fixture", 3);
        const auto suffix = category == "mods" ? ".jar" : ".zip";
        const auto oldPath =
            category + '/' +
            (managed && category == "mods" ? hash(oldBytes) + ".jar" : "old" + QString(suffix)) +
            (enabled ? "" : ".disabled");
        auto target = category + '/' +
                      (managed && category == "mods" ? hash(newBytes) + ".jar" : "same" + QString(suffix)) +
                      (enabled ? "" : ".disabled");
#ifdef Q_OS_WIN
        if (managed && category != "mods")
            target = target.toUpper().replace(category.toUpper() + "/", category + "/");
#endif
        const auto root = pack.value("path").toString();
        write(root + '/' + oldPath, oldBytes);
        write(root + '/' + target, destinationBytes);
        const auto staged = category + "/download.jar";
        write(stage.path() + '/' + staged, newBytes);
        const QVariantMap old{{"relativePath", oldPath}, {"category", category},
                              {"sha1", hash(oldBytes)},  {"enabled", enabled},
                              {"modId", "fixture"},      {"sourceIds", QVariantMap{{"modrinth", "fixture"}}}};
        auto dest = old;
        dest["relativePath"] = target;
        dest["sha1"] = hash(destinationBytes);
        dest["locked"] = locked;
        auto locks = std::make_shared<ContentLockRegistry>();
        locks->setRows(root, {old, dest});
        QJsonObject ref{{"provider", "modrinth"}, {"projectId", "fixture"},
                        {"sha1", hash(newBytes)}, {"filename", "same" + QString(suffix)},
                        {"installPath", staged},  {"confluxReplacePath", oldPath}};
        if (category != "mods")
            ref["category"] = category;
        NativeContentStore files(nullptr, temp.path() + "/backups", locks);
        if (managed) {
            auto original = PackRepository::inspectContent(root);
            QVERIFY(original.ok);
            QJsonObject first{{"provider", "modrinth"},
                              {"projectId", "fixture"},
                              {"sha1", hash(oldBytes)},
                              {"installPath", oldPath},
                              {"version", "1"}};
            QJsonObject second{{"provider", "modrinth"},
                               {"projectId", "collision"},
                               {"sha1", hash(destinationBytes)},
                               {"installPath", target},
                               {"version", "3"}};
            if (category != "mods") {
                first["category"] = category;
                second["category"] = category;
            }
            const QJsonArray originals{first, second};
            QVERIFY(PackRepository::updateContentReferences(root, original.manifest,
                                                            category == "mods" ? originals : QJsonArray{},
                                                            category == "mods" ? QJsonArray{} : originals)
                        .ok);
        }
        ref["version"] = "2";
        const auto result = files.commitStagedContent(
            pack, {old, dest}, stage.path(), {{category == "mods" ? "mods" : "assets", QJsonArray{ref}}},
            {"fixture"}, true);
        if (locked) {
            QVERIFY(!result.ok);
            QVERIFY(result.error.contains("lock", Qt::CaseInsensitive));
            QCOMPARE(read(root + '/' + oldPath), oldBytes);
            QCOMPARE(read(root + '/' + target), destinationBytes);
            return;
        }
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(root + '/' + target), newBytes);
        QVERIFY(!QFileInfo::exists(root + '/' + oldPath));
        QCOMPARE(read(result.backupPath + '/' + target), destinationBytes);
        QCOMPARE(read(result.backupPath + '/' + oldPath), oldBytes);
        if (managed) {
            auto manifest = PackRepository::inspectContent(root).manifest;
            QVERIFY(!QJsonDocument(manifest).toJson().contains("confluxReplacePath"));
            const auto finalRefs = manifest.value(category == "mods" ? "mods" : "assets").toArray();
            QCOMPARE(finalRefs.size(), 1);
            QCOMPARE(finalRefs.first().toObject().value("sha1").toString(), hash(newBytes));
            QCOMPARE(finalRefs.first().toObject().value("version").toString(), QString("2"));
        }
    }
    void identicalSelectedReinstallDoesNotCreateDuplicateIds_data() {
        QTest::addColumn<bool>("managed");
        QTest::addColumn<bool>("enabled");
        for (bool managed : {false, true})
            for (bool enabled : {false, true})
                QTest::newRow(qPrintable(QString("managed%1-enabled%2").arg(managed).arg(enabled)))
                    << managed << enabled;
    }
    void identicalSelectedReinstallDoesNotCreateDuplicateIds() {
        QFETCH(bool, managed);
        QFETCH(bool, enabled);
        QTemporaryDir temp, stage;
        QVariantMap pack{{"path", temp.path() + "/instance"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
        if (managed) {
            const auto created = PackRepository::create(temp.path(), "Fixture", "1.21.1", "fabric");
            QVERIFY(created.ok);
            pack = created.manifest.toVariantMap();
            pack["path"] = created.path;
            pack["managed"] = true;
        }
        const auto bytes = archive(temp.path() + "/source.jar", "mods", "fixture", 1);
        const auto relative = QString("mods/") + (managed ? hash(bytes) : QString("fixture")) + ".jar" +
                              (enabled ? "" : ".disabled");
        write(pack.value("path").toString() + '/' + relative, bytes);
        write(stage.path() + "/mods/staged.jar", bytes);
        QJsonObject ref{{"provider", "modrinth"},
                        {"projectId", "fixture"},
                        {"sha1", hash(bytes)},
                        {"filename", "fixture.jar"},
                        {"installPath", "mods/staged.jar"},
                        {"confluxReplacePath", relative}};
        QVariantMap row{{"relativePath", relative}, {"category", "mods"},
                        {"sha1", hash(bytes)},      {"enabled", enabled},
                        {"modId", "fixture"},       {"sourceIds", QVariantMap{{"modrinth", "fixture"}}}};
        NativeContentStore files(nullptr, temp.path() + "/backups");
        const auto result = files.commitStagedContent(pack, {row}, stage.path(), {{"mods", QJsonArray{ref}}},
                                                      {"fixture"}, true);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(pack.value("path").toString() + '/' + relative), bytes);
        QCOMPARE(files.reload(pack, {}).rows.size(), 1);
    }
    void renamedOrUnresolvedModsReplaceExistingRuntimeIdentity_data() {
        QTest::addColumn<bool>("managed");
        QTest::addColumn<bool>("identical");
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<int>("copies");
        QTest::addColumn<bool>("neoForge");
        for (bool managed : {false, true})
            for (bool identical : {false, true})
                for (bool enabled : {false, true})
                    for (int copies : {1, 2})
                        for (bool neoForge : {false, true})
                            QTest::newRow(
                                qPrintable(QString("managed%1-identical%2-enabled%3-copies%4-neoforge%5")
                                               .arg(managed)
                                               .arg(identical)
                                               .arg(enabled)
                                               .arg(copies)
                                               .arg(neoForge)))
                                << managed << identical << enabled << copies << neoForge;
    }
    void renamedOrUnresolvedModsReplaceExistingRuntimeIdentity() {
        QFETCH(bool, managed);
        QFETCH(bool, identical);
        QFETCH(bool, enabled);
        QFETCH(int, copies);
        QFETCH(bool, neoForge);
        QTemporaryDir temp, stage;
        QVariantMap pack{{"path", temp.path() + "/instance"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
        if (managed) {
            const auto created = PackRepository::create(temp.path(), "Replacement", "1.21.1",
                                                        neoForge ? "neoforge" : "fabric");
            QVERIFY(created.ok);
            pack = created.manifest.toVariantMap();
            pack["path"] = created.path;
            pack["managed"] = true;
        }
        if (neoForge)
            pack["loader"] = "neoforge";
        const auto incoming = archive(temp.path() + "/new.jar", "mods", "accessorify", 2, 0, neoForge);
        const auto previous =
            identical ? incoming : archive(temp.path() + "/old.jar", "mods", "accessorify", 1, 0, neoForge);
        const auto root = pack.value("path").toString();
        const auto original = "mods/" + (managed ? hash(previous) : QString("renamed-accessorify-old")) +
                              ".jar" + (enabled ? "" : ".disabled");
        write(root + '/' + original, previous);
        if (managed) {
            auto oldPin = QJsonObject::fromVariantMap(release("11", previous, "old.jar").toVariant());
            oldPin["provider"] = "curseforge";
            oldPin["id"] = "101";
            oldPin["installPath"] = original;
            oldPin["enabled"] = enabled;
            if (neoForge)
                oldPin["loaders"] = QJsonArray{"neoforge"};
            const auto current = PackRepository::inspectContent(root);
            QVERIFY(current.ok);
            const auto updated =
                PackRepository::updateContentReferences(root, current.manifest, {oldPin}, {});
            QVERIFY2(updated.ok, qPrintable(updated.error));
            pack["mods"] = QVariantList{oldPin.toVariantMap()};
        }
        if (copies == 2)
            write(root + "/mods/extra-accessorify.jar" + (enabled ? "" : QString(".disabled")), previous);
        NativeContentStore files(nullptr, temp.path() + "/backups");
        const auto input = files.reload(pack, {}).rows;
        QCOMPARE(input.size(), copies);
        write(stage.path() + "/mods/download.jar", incoming);
        auto ref = QJsonObject::fromVariantMap(
            release("new-provider-project", incoming, "accessorify-new.jar").toVariant());
        ref["installPath"] = "mods/download.jar";
        const auto result =
            files.commitStagedContent(pack, input, stage.path(), {{"mods", QJsonArray{ref}}}, {}, true);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto confirmed = files.reload(pack, {}).rows;
        QCOMPARE(confirmed.size(), 1);
        const auto installed = confirmed.first().toMap();
        QCOMPARE(installed.value("sha1").toString(), hash(incoming));
        QCOMPARE(installed.value("enabled").toBool(), enabled);
        QCOMPARE(read(root + '/' + installed.value("relativePath").toString()), incoming);
        if (!identical)
            QCOMPARE(read(result.backupPath + '/' + original), previous);
        if (managed) {
            const auto state = PackRepository::inspectContent(root);
            QVERIFY2(state.ok, qPrintable(state.error));
            const auto pins = state.manifest.value("mods").toArray();
            QCOMPARE(pins.size(), 1);
            QCOMPARE(pins.first().toObject().value("sha1").toString(), hash(incoming));
        }
    }
    void downloadingSameFilenameOverwritesAcrossAllContent_data() {
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("category");
        QTest::addColumn<bool>("resolve");
        QTest::addColumn<bool>("differentCase");
        const QStringList types{"mod", "resourcepack", "shader", "datapack"},
            categories{"mods", "resourcepacks", "shaderpacks", "datapacks"};
        for (int i = 0; i < types.size(); ++i)
            for (bool resolve : {false, true})
#ifdef Q_OS_WIN
                for (bool differentCase : {false, true})
#else
                for (bool differentCase : {false})
#endif
                    QTest::newRow(
                        qPrintable(types[i] + QString::number(resolve) + QString::number(differentCase)))
                        << types[i] << categories[i] << resolve << differentCase;
    }
    void downloadingSameFilenameOverwritesAcrossAllContent() {
        QFETCH(QString, type);
        QFETCH(QString, category);
        QFETCH(bool, resolve);
        QFETCH(bool, differentCase);
        QTemporaryDir temp;
        Server server;
        const auto filename = type == "mod" ? QString("same.jar") : QString("same.zip");
        const auto old = archive(temp.path() + "/old.jar", category, type == "mod" ? "fixture" : "other", 1),
                   bytes = archive(temp.path() + "/new.jar", category, "fixture", 2);
        QVariantMap pack{{"path", temp.path() + "/instance"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
        const auto target = pack.value("path").toString() + '/' + category + '/' + filename;
        write(target, old);
        auto files = std::make_shared<NativeContentStore>(nullptr, temp.path() + "/backups");
        const auto input = files->reload(pack, {});
        auto r = release("fixture", bytes, differentCase ? filename.toUpper() : filename);
        r.url = server.base().resolved(QUrl("file")).toString();
        server.responses["/file"] = bytes;
        ContentInstaller installer(files, nullptr, true, server.base());
        bool done = false;
        ContentWriteResult result;
        installer.install(pack, input.rows, {{r, "Fixture", {{"modrinth", "fixture"}}, type, resolve}},
                          [&](auto value) {
                              result = value;
                              done = true;
                          });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(target), bytes);
        QCOMPARE(read(result.backupPath + '/' + category + '/' + filename), old);
        QCOMPARE(files->reload(pack, {}).rows.size(), 1);
    }
    void emptyInstallDoesNotCreateAWorkspace() {
        QTemporaryDir temp;
        ContentInstaller installer(std::make_shared<NativeContentStore>(nullptr), nullptr, true);
        bool done = false;
        ContentWriteResult result;
        installer.install({{"path", temp.path()}}, {}, {}, [&](auto value) {
            result = value;
            done = true;
        });
        QVERIFY(done);
        QVERIFY(!result.ok);
        QVERIFY(QDir(temp.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    }
    void lockedInputFailsBeforeNetworkWork() {
        QTemporaryDir temp;
        Server server;
        auto bytes = archive(temp.path() + "/source.jar", "mods", "fixture", 2);
        auto r = release("fixture", bytes, "fixture.jar");
        r.url = server.base().resolved(QUrl("file")).toString();
        auto files = std::make_shared<NativeContentStore>(nullptr);
        ContentInstaller installer(files, nullptr, true);
        bool done = false;
        ContentWriteResult result;
        installer.install({{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}},
                          {QVariantMap{{"relativePath", "mods/fixture.jar"},
                                       {"category", "mods"},
                                       {"locked", true},
                                       {"sourceIds", QVariantMap{{"modrinth", "fixture"}}}}},
                          {{r, "Fixture", {{"modrinth", "fixture"}}, "mod", false}}, [&](auto value) {
                              result = value;
                              done = true;
                          });
        QTRY_VERIFY(done);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("lock", Qt::CaseInsensitive));
        QVERIFY(server.requests.isEmpty());
    }
    void fingerprintCacheDetectsEditsWithRestoredModificationTime() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/file.jar";
        write(path, "abcd");
        auto &cache = ContentFileCache::instance();
        QCOMPARE(cache.sha1(path), hash("abcd"));
        const auto before = QFileInfo(path).lastModified();
        write(path, "wxyz");
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(before, QFileDevice::FileModificationTime));
        }
        QCOMPARE(cache.sha1(path), hash("wxyz"));
        QSaveFile replacement(path);
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        replacement.write("1234");
        QVERIFY(replacement.commit());
        QCOMPARE(cache.sha1(path), hash("1234"));
        QVERIFY(QFile::remove(path));
        QVERIFY(cache.sha1(path).isEmpty());
        write(path, "abcd");
        QCOMPARE(cache.sha1(path), hash("abcd"));
    }
    void warmInventoryAndSingleUpdateHaveBoundedIo_data() {
        QTest::addColumn<int>("count");
        QTest::newRow("200") << 200;
        QTest::newRow("500") << 500;
    }
    void warmInventoryAndSingleUpdateHaveBoundedIo() {
        QFETCH(int, count);
        QTemporaryDir temp, stage;
        const QVariantMap pack{{"path", temp.path()}, {"loader", "fabric"}, {"minecraft", "1.21.1"}};
        for (int i = 0; i < count; ++i) {
            const auto file = temp.path() + QString("/mods/mod%1.jar").arg(i);
            QDir().mkpath(QFileInfo(file).absolutePath());
            QVERIFY(!archive(file, "mods", QString("mod%1").arg(i), 1, 128 * 1024).isEmpty());
        }
        for (int i = 0; i < 1500; ++i)
            write(temp.path() + QString("/config/unrelated%1.txt").arg(i), "config");
        auto native = std::make_shared<NativePackFileStore>();
        NativeContentStore files(std::make_shared<NativeInstalledModIdentity>(native),
                                 temp.path() + "/backups");
        QElapsedTimer clock;
        clock.start();
        auto first = files.reload(pack, {});
        auto cold = clock.restart();
        const auto initial = ContentFileCache::instance().statistics();
        auto warm = files.reload(pack, first.rows);
        const auto warmMs = clock.restart();
        const auto cached = ContentFileCache::instance().statistics();
        QCOMPARE(first.rows.size(), count);
        QCOMPARE(warm.rows.size(), count);
        QCOMPARE(cached.hashedFiles, initial.hashedFiles);
        QCOMPARE(cached.decodedFiles, initial.decodedFiles);
        QVERIFY2(warmMs < 1500, qPrintable(QString("Warm load exceeded 1500ms: %1").arg(warmMs)));
        auto rows = warm.rows;
        for (auto &v : rows) {
            auto r = v.toMap();
            r["sourceIds"] = QVariantMap{{"modrinth", r.value("modId")}};
            v = r;
        }
        auto selected = rows.first().toMap();
        const auto next =
            archive(stage.path() + "/next.zip", "mods", selected.value("modId").toString(), 2, 128 * 1024);
        write(stage.path() + "/mods/new.jar", next);
        QFile::remove(stage.path() + "/next.zip");
        QJsonObject ref{{"provider", "modrinth"},
                        {"projectId", selected.value("modId").toString()},
                        {"sha1", hash(next)},
                        {"filename", "next.jar"},
                        {"installPath", "mods/new.jar"},
                        {"confluxReplacePath", selected.value("relativePath").toString()}};
        const auto result = files.commitStagedContent(pack, rows, stage.path(), {{"mods", QJsonArray{ref}}},
                                                      {selected.value("modId").toString()}, true);
        QVERIFY2(result.ok, qPrintable(result.error));
        auto after = files.reload(pack, rows);
        const auto updated = ContentFileCache::instance().statistics();
        QCOMPARE(after.rows.size(), count);
        QVERIFY(updated.hashedBytes - cached.hashedBytes <= quint64(next.size() * 2));
        QVERIFY(updated.decodedFiles - cached.decodedFiles <= 2);
        const auto updateMs = clock.elapsed();
        QVERIFY2(updateMs < 2000, qPrintable(QString("Single update exceeded 2000ms: %1").arg(updateMs)));
        clock.restart();
        const auto workspace = PackContent::scan(pack);
        const auto workspaceMs = clock.elapsed();
        QCOMPARE(workspace.files.size(), count + 1500);
        QVERIFY2(workspaceMs < 1500,
                 qPrintable(QString("Workspace scan exceeded 1500ms: %1").arg(workspaceMs)));
        qInfo() << "Workspace scan ms" << workspaceMs;
        qInfo() << "Pipeline files/cold/warm/update ms/extra hash bytes" << count << cold << warmMs
                << updateMs << updated.hashedBytes - cached.hashedBytes;
    }
    void inventoryRejectsPrivateAndLinkedChildren() {
        QTemporaryDir temp, outside;
        write(temp.path() + "/config/.env", "private");
        write(temp.path() + "/config/.git/hidden.json", "private");
        write(temp.path() + "/config/allowed.json", "{}");
        const auto bytes = archive(outside.path() + "/outside.jar", "mods", "outside", 1);
        QDir().mkpath(temp.path() + "/mods");
        write(outside.path() + "/foreign.json", "{}");
        const auto link = temp.path() + "/config/linked";
#ifdef Q_OS_WIN
        QProcess junction;
        junction.setStandardOutputFile(QProcess::nullDevice());
        junction.setStandardErrorFile(QProcess::nullDevice());
        junction.start("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(link),
                                   QDir::toNativeSeparators(outside.path())});
        QVERIFY(junction.waitForFinished());
        QCOMPARE(junction.exitCode(), 0);
        QVERIFY(QFileInfo(link).isJunction());
#else
        std::error_code error;
        std::filesystem::create_directory_symlink(outside.path().toStdString(), link.toStdString(), error);
        QVERIFY2(!error, qPrintable(QString::fromStdString(error.message())));
#endif
        const auto rows = PackContent::scan({{"path", temp.path()}}).files;
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value("relativePath").toString(), QString("config/allowed.json"));
        QCOMPARE(read(outside.path() + "/outside.jar"), bytes);
    }
    void existingVerifiedDestinationIsReusedWithoutNetwork() {
        QTemporaryDir temp;
        auto bytes = archive(temp.path() + "/existing.jar", "mods", "fixture", 2);
        auto r = release("fixture", bytes, "existing.jar");
        r.url = "http://127.0.0.1:1/not-requested";
        QDir().mkpath(temp.path() + "/stage");
        VerifiedDownload download(nullptr, true);
        bool done = false;
        VerifiedDownloadResult result;
        download.start(
            r, temp.path() + "/stage", "mod",
            [&](auto value) {
                result = value;
                done = true;
            },
            temp.path() + "/existing.jar");
        QTRY_VERIFY(done);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(read(result.path), bytes);
        QCOMPARE(read(temp.path() + "/existing.jar"), bytes);
    }
    void workerDownloadRejectsInvalidPayloads_data() {
        QTest::addColumn<QString>("fault");
        for (auto fault : {"hash", "size", "sha512", "archive", "url", "oversize"})
            QTest::newRow(fault) << QString(fault);
    }
    void workerDownloadRejectsInvalidPayloads() {
        QFETCH(QString, fault);
        QTemporaryDir temp;
        Server server;
        auto bytes = fault == "archive" ? QByteArray("not a zip")
                                        : archive(temp.path() + "/source.jar", "mods", "fixture", 2);
        auto r = release("fixture", bytes, "file.jar");
        r.url = server.base().resolved(QUrl("file")).toString();
        server.responses["/file"] = bytes;
        if (fault == "hash")
            r.sha1 = QString(40, '0');
        if (fault == "size")
            ++r.fileSize;
        if (fault == "sha512")
            r.sha512 = QString(128, '0');
        if (fault == "url")
            r.url = "file:///local.jar";
        if (fault == "oversize")
            r.fileSize = 513LL * 1024 * 1024;
        VerifiedDownload download(nullptr, true);
        bool done = false;
        VerifiedDownloadResult result;
        download.start(r, temp.path(), "mod", [&](auto value) {
            result = value;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.path.isEmpty());
        QVERIFY(!download.busy());
    }
    void cancelledDownloadCanBeFollowedImmediatelyByAnother() {
        QTemporaryDir temp;
        Server server;
        server.stall = true;
        const auto bytes = archive(temp.path() + "/source.jar", "mods", "fixture", 2);
        auto r = release("fixture", bytes, "file.jar");
        r.url = server.base().resolved(QUrl("file")).toString();
        server.responses["/file"] = bytes;
        VerifiedDownload download(nullptr, true);
        bool done = false;
        QString error;
        download.start(r, temp.path(), "mod", [&](auto value) {
            error = value.error;
            done = true;
        });
        QTRY_VERIFY(!server.requests.isEmpty());
        download.cancel();
        QTRY_VERIFY(done);
        QVERIFY(error.contains("cancel", Qt::CaseInsensitive));
        server.stall = false;
        done = false;
        download.start(r, temp.path(), "mod", [&](auto value) {
            error = value.error;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    void largeDownloadKeepsTheEventLoopResponsive() {
        QTemporaryDir temp;
        Server server;
        const auto bytes = archive(temp.path() + "/source.jar", "mods", "fixture", 2, 24 * 1024 * 1024);
        auto r = release("fixture", bytes, "file.jar");
        r.url = server.base().resolved(QUrl("file")).toString();
        server.responses["/file"] = bytes;
        QTimer pulse;
        pulse.setInterval(2);
        QElapsedTimer timer;
        timer.start();
        qint64 previous = 0, worst = 0;
        int ticks = 0;
        connect(&pulse, &QTimer::timeout, &pulse, [&] {
            const auto now = timer.elapsed();
            worst = qMax(worst, now - previous);
            previous = now;
            ++ticks;
        });
        pulse.start();
        VerifiedDownload download(nullptr, true);
        bool done = false;
        QString error;
        download.start(r, temp.path(), "mod", [&](auto value) {
            error = value.error;
            done = true;
            QCOMPARE(QThread::currentThread(), QCoreApplication::instance()->thread());
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(ticks > 1);
        QVERIFY2(test_performance::releaseBudgetMet(worst, 250),
                 qPrintable(QString("Event loop gap %1ms").arg(worst)));
        qInfo() << "Download event-loop max gap ms" << worst;
    }
    void incompatibleUpdatesDoNotStartDownloads_data() {
        QTest::addColumn<bool>("wrongLoader");
        QTest::newRow("minecraft") << false;
        QTest::newRow("loader") << true;
    }
    void incompatibleUpdatesDoNotStartDownloads() {
        QFETCH(bool, wrongLoader);
        QTemporaryDir temp;
        Server server;
        const auto bytes = archive(temp.path() + "/source.jar", "mods", "fixture", 2);
        auto r = release("fixture", bytes, "file.jar");
        r.url = server.base().resolved(QUrl("file")).toString();
        if (wrongLoader)
            r.loaders = {"neoforge"};
        else
            r.gameVersions = {"1.20.1"};
        ContentInstaller installer(std::make_shared<NativeContentStore>(nullptr), nullptr, true);
        bool done = false;
        ContentWriteResult result;
        installer.install({{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "fabric"}}, {},
                          {{r, "Fixture", {{"modrinth", "fixture"}}, "mod", false}}, [&](auto value) {
                              result = value;
                              done = true;
                          });
        QTRY_VERIFY(done);
        QVERIFY(!result.ok);
        QVERIFY(server.requests.isEmpty());
    }
    void initialDependenciesAndMixedContentUseOneAtomicCommit_data() {
        QTest::addColumn<bool>("mixed");
        QTest::newRow("mods") << false;
        QTest::newRow("mixed") << true;
    }
    void initialDependenciesAndMixedContentUseOneAtomicCommit() {
        QFETCH(bool, mixed);
        QTemporaryDir temp;
        Server server;
        QVariantMap pack{{"path", temp.path() + "/instance"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
        QDir().mkpath(pack.value("path").toString());
        const auto rootBytes = archive(temp.path() + "/root.jar", "mods", "root", 1),
                   libBytes = archive(temp.path() + "/lib.jar", "mods", "lib", 1),
                   assetBytes = archive(temp.path() + "/pack.zip", "resourcepacks", "pack", 1);
        auto root = release("root", rootBytes, "root.jar"), lib = release("lib", libBytes, "lib.jar"),
             asset = release("pack", assetBytes, "pack.zip");
        for (auto *r : {&root, &lib, &asset})
            r->url = server.base().resolved(QUrl(r->projectId)).toString();
        root.dependencyRefs = QJsonArray{
            QJsonObject{{"project_id", "lib"}, {"version_id", lib.id}, {"dependency_type", "required"}}};
        root.requiredDependencies = {"lib"};
        server.responses["/root"] = rootBytes;
        server.responses["/lib"] = libBytes;
        server.responses["/pack"] = assetBytes;
        server.responses["/version/" + lib.id] =
            QJsonDocument(
                QJsonObject{{"project_id", "lib"},
                            {"id", lib.id},
                            {"version_number", "1"},
                            {"version_type", "release"},
                            {"game_versions", QJsonArray{"1.21.1"}},
                            {"loaders", QJsonArray{"fabric"}},
                            {"files", QJsonArray{QJsonObject{{"filename", "lib.jar"},
                                                             {"primary", true},
                                                             {"url", lib.url},
                                                             {"size", lib.fileSize},
                                                             {"hashes", QJsonObject{{"sha1", lib.sha1}}}}}}})
                .toJson();
        server.responses["/project/lib"] = "{\"title\":\"Library\"}";
        QVector<ContentInstallRequest> requests{{root, "Root", {{"modrinth", "root"}}, "mod", true}};
        if (mixed)
            requests << ContentInstallRequest{asset, "Pack", {{"modrinth", "pack"}}, "resourcepack", true};
        auto files = std::make_shared<NativeContentStore>(nullptr, temp.path() + "/backups");
        ContentInstaller installer(files, nullptr, true, server.base());
        bool done = false;
        ContentWriteResult result;
        installer.install(pack, {}, requests, [&](auto value) {
            result = value;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(read(pack.value("path").toString() + "/mods/root.jar"), rootBytes);
        QCOMPARE(read(pack.value("path").toString() + "/mods/lib.jar"), libBytes);
        if (mixed)
            QCOMPARE(read(pack.value("path").toString() + "/resourcepacks/pack.zip"), assetBytes);
    }
};
QTEST_GUILESS_MAIN(ContentPipelineTests)
#include "ContentPipelineTests.moc"
