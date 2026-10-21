#include "ContentFixtures.h"
#include "features/content/ContentActions.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/InstalledModIdentity.h"
#include "platform/NativeContentStore.h"
#include "platform/PackChangeMonitor.h"
#include "services/ContentCatalogue.h"
#include "services/PackRepository.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
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
        QCOMPARE(installer->requested.size(), 3);
        QCOMPARE(installer->requested[0].contentType, QString("mod"));
        QCOMPARE(installer->requested[1].contentType, QString("resourcepack"));
        QCOMPARE(installer->requested[2].contentType, QString("datapack"));
        content.cancel();
        QVERIFY(!content.busy());
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
        QCOMPARE(content.updateCount(), 1);
        content.updateAll();
        QVERIFY(content.busy());
        QCOMPARE(installer->requested.size(), 1);
        QCOMPARE(installer->requested.first().release.projectId, QString("Bravo"));
        installer->completion({true, {}, {}});
        QVERIFY(!content.busy());
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
