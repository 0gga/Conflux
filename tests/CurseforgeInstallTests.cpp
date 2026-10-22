#include "miniz.h"
#include "platform/ContentInstaller.h"
#include "platform/CurseforgeInstallSource.h"
#include "services/CatalogueClient.h"
#include "services/ContentCatalogue.h"
#include "services/PackRepository.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
using namespace studio;
class CurseforgeFixture final : public QTcpServer {
  public:
    QMap<QString, QByteArray> responses;
    QMap<QString, int> statuses;
    QMap<QString, QByteArray> headers;
    QStringList requests;
    CurseforgeFixture() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const auto input = socket->property("input").toByteArray() + socket->readAll();
                    socket->setProperty("input", input);
                    if (!input.contains("\r\n\r\n") || socket->property("answered").toBool())
                        return;
                    const int headerEnd = input.indexOf("\r\n\r\n") + 4;
                    int contentLength = 0;
                    for (const auto &line : input.left(headerEnd).split('\n'))
                        if (line.toLower().startsWith("content-length:"))
                            contentLength = line.mid(15).trimmed().toInt();
                    if (input.size() < headerEnd + contentLength)
                        return;
                    socket->setProperty("answered", true);
                    const auto path = QUrl(QString::fromUtf8(input.split(' ').value(1))).path();
                    requests << path;
                    headers[path] = input;
                    const auto body = responses.value(path, "not found");
                    const int status = statuses.value(path, responses.contains(path) ? 200 : 404);
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: " +
                                  QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl base() const {
        return QUrl("http://127.0.0.1:" + QString::number(serverPort()) + '/');
    }
    static QByteArray hash(const QByteArray &bytes) {
        return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex();
    }
    void json(const QString &path, const QJsonValue &data) {
        responses[path] = QJsonDocument(QJsonObject{{"data", data}}).toJson();
    }
    QJsonObject file(int project, int id, const QByteArray &bytes, QJsonArray deps = {}) {
        const QString projectPath = "/mods/" + QString::number(project);
        const auto download = "/download/" + QString::number(id);
        QJsonObject f{
            {"id", id},
            {"modId", project},
            {"displayName", QString::number(id)},
            {"fileName", QString::number(id) + ".jar"},
            {"gameVersions", QJsonArray{"1.21.1", "Fabric"}},
            {"fileDate", "2026-10-22T10:00:00Z"},
            {"releaseType", 1},
            {"isAvailable", true},
            {"downloadUrl", base().resolved(QUrl(download)).toString()},
            {"fileLength", bytes.size()},
            {"hashes", QJsonArray{QJsonObject{{"algo", 1}, {"value", QString::fromLatin1(hash(bytes))}}}},
            {"dependencies", deps}};
        json(projectPath, QJsonObject{{"id", project},
                                      {"gameId", 432},
                                      {"name", "Project " + QString::number(project)},
                                      {"allowModDistribution", true}});
        json(projectPath + "/files/" + QString::number(id), f);
        json(projectPath + "/files", QJsonArray{f});
        responses[download] = bytes;
        return f;
    }
};
class CurseforgeInstallTests final : public QObject {
    Q_OBJECT
    static ContentInstallRequest request(const QJsonObject &f, bool dependencies = true) {
        auto release = curseforgeRelease(f);
        return {release, "Fixture", {{"curseforge", release.projectId}}, "mod", dependencies};
    }
    static QByteArray archive(const QString &path, const QString &id) {
        mz_zip_archive zip{};
        if (!mz_zip_writer_init_file(&zip, QFile::encodeName(path).constData(), 0))
            return {};
        const auto data =
            QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"id", id}, {"name", id}, {"version", "1"}})
                .toJson();
        mz_zip_writer_add_mem(&zip, "fabric.mod.json", data.data(), size_t(data.size()), 0);
        mz_zip_writer_finalize_archive(&zip);
        mz_zip_writer_end(&zip);
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
  private slots:
    void exactProjectScopesUseTheBulkEndpointAndKeepCompatibilityFiltersAndPagination() {
        CurseforgeFixture server;
        const auto project = [](int id, int count, QString game = "1.21.1", int loader = 4, int type = 6) {
            return QJsonObject{{"id", id},
                               {"gameId", 432},
                               {"classId", type},
                               {"name", "Fixture " + QString::number(id)},
                               {"downloadCount", count},
                               {"authors", QJsonArray{QJsonObject{{"id", 7}}}},
                               {"categories", QJsonArray{QJsonObject{{"id", 42}}}},
                               {"latestFilesIndexes",
                                QJsonArray{QJsonObject{{"gameVersion", game}, {"modLoader", loader}}}}};
        };
        server.json("/mods", QJsonArray{project(999, 1000), project(11, 100), project(18, 500),
                                        project(12, 900, "1.20.1"), project(13, 900, "1.21.1", 6),
                                        project(14, 900, "1.21.1", 4, 12)});
        CatalogueClient client(nullptr, server.base(), server.base());
        client.setCurseforgeKey("fixture-key");
        CatalogueQuery query;
        query.provider = "curseforge";
        query.projectIds = {"11", "12", "13", "14", "18"};
        query.versions = {"1.21.1"};
        query.loaders = {"fabric"};
        query.tags = {"cf:42"};
        query.authorIds = {{"curseforge", "7"}};
        query.text = "Fixture";
        query.sort = "downloads";
        for (int offset = 0; offset < 2; ++offset) {
            bool done = false;
            CataloguePage page;
            client.fetch(query, offset, 1, [&](auto result) {
                page = result;
                done = true;
            });
            QTRY_VERIFY(done);
            QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
            QCOMPARE(page.total, 2);
            QCOMPARE(page.projects.size(), 1);
            QCOMPARE(page.projects.first().id, offset == 0 ? QString("18") : QString("11"));
        }
        QVERIFY(!server.requests.contains("/mods/search"));
        QCOMPARE(server.requests.count("/mods"), 2);
        QVERIFY(server.headers.value("/mods").contains("modIds"));
        query.type = "resourcepack"; // Pack filters ignore the mod-loader choice.
        bool done = false;
        CataloguePage page;
        client.fetch(query, 0, 10, [&](auto result) {
            page = result;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(page.projects.size(), 1);
        QCOMPARE(page.projects.first().id, QString("14"));
    }
    void missingCredentialsYieldBeforeCompletingUpdateLookups() {
        CatalogueClient client;
        client.setCurseforgeKey({});
        int callbacks = 0;
        for (int i = 0; i < 5; ++i)
            client.curseforgeVersions("11", "1.21.1", {"fabric"}, [&](auto releases, QString error) {
                QVERIFY(releases.isEmpty());
                QVERIFY(error.contains("key"));
                ++callbacks;
            });
        QCOMPARE(callbacks, 0);
        QTRY_COMPARE(callbacks, 5);
    }
    void updateIndexRetainsLockedItemsAndNeverOffersTheInstalledOrIncompatibleFile() {
        CurseforgeFixture server;
        auto old = server.file(11, 101, "old bytes");
        old["fileDate"] = "2026-10-21T10:00:00Z";
        auto candidate = server.file(11, 102, "new bytes");
        server.responses["/version_files"] = "{}";
        ContentCatalogue source(nullptr, server.base(), server.base());
        source.setCurseforgeKey("fixture-key");
        auto installed = curseforgeRelease(old);
        QVariantMap row{{"category", "mods"},
                        {"locked", true},
                        {"sha1", installed.sha1},
                        {"sourceIds", QVariantMap{{"curseforge", "11"}}},
                        {"installedRelease", installed.toVariant()}};
        for (int scenario = 0; scenario < 8; ++scenario) {
            if (scenario == 1)
                candidate = old;
            if (scenario == 2) {
                candidate = server.file(11, 103, "wrong version");
                candidate["gameVersions"] = QJsonArray{"1.20.1", "Fabric"};
            }
            if (scenario >= 3) {
                candidate = server.file(11, 102, "new bytes");
                installed.channel = scenario == 5                    ? "release"
                                    : scenario == 3 || scenario == 7 ? "alpha"
                                                                     : "beta";
                candidate["releaseType"] = scenario == 6 ? 1 : scenario == 4 || scenario == 7 ? 3 : 2;
                row["installedRelease"] = installed.toVariant();
            }
            server.json("/mods/11/files", QJsonArray{candidate});
            bool done = false;
            QVariantList rows;
            QString error;
            source.refresh({row}, "1.21.1", "fabric", [&](auto result, QString failure) {
                rows = result;
                error = failure;
                done = true;
            });
            QTRY_VERIFY(done);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(rows.size(), 1);
            QCOMPARE(rows.first().toMap().value("updateAvailable").toBool(),
                     (scenario == 0 || scenario == 3 || scenario == 6 || scenario == 7));
            QVERIFY(rows.first().toMap().value("locked").toBool());
        }
    }
    void aCorruptDependencyDownloadLeavesTheWholeInitialInstallUnchanged() {
        CurseforgeFixture server;
        QTemporaryDir temp;
        const auto rootBytes = archive(temp.path() + "/root.jar", "rootmod");
        const auto libraryBytes = archive(temp.path() + "/library.jar", "librarymod");
        const auto root = server.file(11, 101, rootBytes, {QJsonObject{{"modId", 12}, {"relationType", 3}}});
        server.file(12, 102, libraryBytes);
        server.responses["/download/102"] = "corrupt bytes";
        const auto created = PackRepository::create(temp.path(), "Atomic CF fixture", "1.21.1", "fabric");
        QVERIFY(created.ok);
        const auto before = QJsonDocument(PackRepository::inspectContent(created.path).manifest).toJson();
        const QVariantMap pack{{"path", created.path},
                               {"managed", true},
                               {"provider", "conflux"},
                               {"minecraft", "1.21.1"},
                               {"loader", "fabric"}};
        auto files = std::make_shared<NativeContentStore>(nullptr, temp.path() + "/backups");
        ContentInstaller installer(files, nullptr, true, server.base(), server.base());
        installer.setCurseforgeKey("fixture-key");
        bool done = false;
        ContentWriteResult result;
        installer.install(pack, {}, {request(root)}, [&](auto value) {
            result = value;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(!result.ok);
        QVERIFY(!result.changed);
        QCOMPARE(QJsonDocument(PackRepository::inspectContent(created.path).manifest).toJson(), before);
        QVERIFY(files->reload(pack, {}).rows.isEmpty());
        QVERIFY(server.requests.contains("/download/101"));
    }
    void liveCurseforgeInstallUsesOnlyATemporaryManagedInstance() {
        if (!qEnvironmentVariableIsSet("CONFLUX_LIVE_CURSEFORGE_INSTALL_TEST") ||
            qEnvironmentVariableIsEmpty("CURSEFORGE_API_KEY"))
            QSKIP("Explicit live download opt-in and a session API key are required.");
        CatalogueClient client;
        client.setCurseforgeKey(qgetenv("CURSEFORGE_API_KEY"));
        CatalogueQuery query;
        query.provider = "curseforge";
        query.type = "mod";
        query.versions = {"1.21.1"};
        query.loaders = {"neoforge"};
        query.sort = "downloads";
        bool searched = false;
        CataloguePage page;
        client.fetch(query, 0, 10, [&](auto result) {
            page = result;
            searched = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(searched, 60000);
        QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
        QVERIFY(!page.projects.isEmpty());
        ProjectDetail detail;
        bool loaded = false;
        connect(&client, &CatalogueClient::detailFinished, this, [&](int, auto result) {
            detail = result;
            loaded = true;
        });
        auto project = page.projects.first();
        project.sourceIds = {{"curseforge", project.id}};
        auto exact = query;
        exact.projectIds = {project.id};
        bool scoped = false;
        CataloguePage exactPage;
        client.fetch(exact, 0, 10, [&](auto result) {
            exactPage = result;
            scoped = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(scoped, 60000);
        QVERIFY2(exactPage.error.isEmpty(), qPrintable(exactPage.error));
        QCOMPARE(exactPage.projects.size(), 1);
        QCOMPARE(exactPage.projects.first().id, project.id);
        client.loadProject(project, "1.21.1", "neoforge", 1);
        QTRY_VERIFY_WITH_TIMEOUT(loaded, 30000);
        QVERIFY2(detail.errors.isEmpty(), qPrintable(detail.errors.join(';')));
        Release selected;
        for (const auto &release : detail.releases)
            if (!release.url.isEmpty() && release.fileSize > 0 && release.fileSize < 8 * 1024 * 1024 &&
                release.filename.endsWith(".jar", Qt::CaseInsensitive)) {
                selected = release;
                break;
            }
        QVERIFY2(!selected.id.isEmpty(), "The sampled project has no available small compatible JAR.");
        QCOMPARE(selected.provider, QString("curseforge"));
        QTemporaryDir temp;
        const auto created =
            PackRepository::create(temp.path(), "Authenticated CF integration fixture", "1.21.1", "neoforge");
        QVERIFY(created.ok);
        const QVariantMap pack{{"path", created.path},
                               {"managed", true},
                               {"provider", "conflux"},
                               {"minecraft", "1.21.1"},
                               {"loader", "neoforge"}};
        auto files = std::make_shared<NativeContentStore>(nullptr, temp.path() + "/backups");
        ContentInstaller installer(files);
        installer.setCurseforgeKey(qgetenv("CURSEFORGE_API_KEY"));
        bool done = false;
        ContentWriteResult result;
        installer.install(
            pack, {},
            {{selected, page.projects.first().title, {{"curseforge", selected.projectId}}, "mod", false}},
            [&](auto value) {
                result = value;
                done = true;
            });
        QTRY_VERIFY_WITH_TIMEOUT(done, 60000);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto rows = files->reload(pack, {}).rows;
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value("sha1").toString().toLower(), selected.sha1.toLower());
        QCOMPARE(rows.first().toMap().value("sourceIds").toMap().value("curseforge").toString(),
                 selected.projectId);
    }
    void catalogueMatchesSharedFilesInOnePublicBatchWithoutLeakingTheKey() {
        CurseforgeFixture server;
        const auto file = server.file(11, 101, "shared binary");
        const auto hash = QString::fromLatin1(CurseforgeFixture::hash("shared binary"));
        server.responses["/mods/search"] =
            QJsonDocument(QJsonObject{{"data", QJsonArray{QJsonObject{{"id", 11},
                                                                      {"name", "Different platform title"},
                                                                      {"latestFiles", QJsonArray{file}}}}},
                                      {"pagination", QJsonObject{{"totalCount", 1}}}})
                .toJson();
        server.responses["/version_files"] =
            QJsonDocument(
                QJsonObject{{hash, QJsonObject{{"project_id", "mr-project"},
                                               {"files", QJsonArray{QJsonObject{
                                                             {"hashes", QJsonObject{{"sha1", hash}}}}}}}}})
                .toJson();
        CatalogueClient client(nullptr, server.base(), server.base());
        client.setCurseforgeKey("fixture-key");
        CatalogueQuery query;
        query.provider = "curseforge";
        for (int i = 0; i < 2; ++i) {
            bool done = false;
            CataloguePage page;
            client.fetch(query, 0, 20, [&](auto result) {
                page = result;
                done = true;
            });
            QTRY_VERIFY(done);
            QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
            QCOMPARE(page.projects.size(), 1);
            QCOMPARE(page.projects.first().sourceIds.value("modrinth"), QString("mr-project"));
        }
        QCOMPARE(server.requests.count("/version_files"), 1);
        QVERIFY(!server.headers.value("/version_files").toLower().contains("x-api-key"));
        QVERIFY(server.headers.value("/mods/search").toLower().contains("x-api-key: fixture-key"));
    }
    void rejectedMetadataNeverReachesTheTransaction_data() {
        QTest::addColumn<QString>("failure");
        for (const auto *failure : {"no-key", "distribution", "url", "sha1", "game", "loader", "identity",
                                    "unavailable", "unauthorized", "rate-limit", "json", "redirect"})
            QTest::newRow(failure) << QString(failure);
    }
    void rejectedMetadataNeverReachesTheTransaction() {
        QFETCH(QString, failure);
        CurseforgeFixture server;
        auto f = server.file(11, 101, "fixture bytes");
        const auto selected = request(f);
        CurseforgeInstallSource source(nullptr, server.base());
        source.setKey(failure == "no-key" ? QByteArray{} : QByteArray("fixture-key"));
        if (failure == "distribution")
            server.json("/mods/11",
                        QJsonObject{{"id", 11}, {"gameId", 432}, {"allowModDistribution", false}});
        else if (failure == "url")
            f["downloadUrl"] = QJsonValue::Null;
        else if (failure == "sha1")
            f["hashes"] = QJsonArray{};
        else if (failure == "game")
            f["gameVersions"] = QJsonArray{"1.20.1", "Fabric"};
        else if (failure == "loader")
            f["gameVersions"] = QJsonArray{"1.21.1", "NeoForge"};
        else if (failure == "identity")
            f["modId"] = 12;
        else if (failure == "unavailable")
            f["isAvailable"] = false;
        if (failure == "unauthorized")
            server.statuses["/mods/11"] = 401;
        if (failure == "rate-limit")
            server.statuses["/mods/11"] = 429;
        if (failure == "redirect")
            server.statuses["/mods/11"] = 302;
        if (failure == "json")
            server.responses["/mods/11"] = "invalid json";
        server.json("/mods/11/files/101", f);
        bool done = false;
        QString error;
        source.prepare({{"minecraft", "1.21.1"}, {"loader", "fabric"}}, {}, {selected},
                       [&](auto requests, QString result) {
                           QVERIFY(requests.isEmpty());
                           error = result;
                           done = true;
                       });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(!error.isEmpty());
        QVERIFY(!source.busy());
        QVERIFY(!server.requests.contains("/download/101"));
        if (failure == "no-key")
            QVERIFY(server.requests.isEmpty());
    }
    void initialInstallIncludesRequiredDependenciesAndUpdatesRemainShallow() {
        CurseforgeFixture server;
        QTemporaryDir temp;
        const auto rootBytes = archive(temp.path() + "/root.jar", "rootmod");
        const auto libraryBytes = archive(temp.path() + "/library.jar", "librarymod");
        auto root = server.file(11, 101, rootBytes,
                                {QJsonObject{{"modId", 12}, {"relationType", 3}},
                                 QJsonObject{{"modId", 13}, {"relationType", 2}}});
        server.file(12, 102, libraryBytes, {QJsonObject{{"modId", 11}, {"relationType", 3}}});
        const auto created = PackRepository::create(temp.path(), "CF fixture", "1.21.1", "fabric");
        QVERIFY(created.ok);
        QVariantMap pack{{"path", created.path},
                         {"managed", true},
                         {"provider", "conflux"},
                         {"minecraft", "1.21.1"},
                         {"loader", "fabric"}};
        auto files = std::make_shared<NativeContentStore>(nullptr);
        ContentInstaller installer(files, nullptr, true, server.base(), server.base());
        installer.setCurseforgeKey("fixture-key");
        bool done = false;
        ContentWriteResult result;
        installer.install(pack, {}, {request(root)}, [&](auto value) {
            result = value;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(result.ok, qPrintable(result.error));
        auto inspected = PackRepository::inspectContent(created.path);
        QCOMPARE(inspected.manifest.value("mods").toArray().size(), 2);
        QVERIFY(!server.requests.contains("/mods/13"));
        QVERIFY(server.headers.value("/mods/11").toLower().contains("x-api-key: fixture-key"));
        QVERIFY(!server.headers.value("/download/101").toLower().contains("x-api-key"));
        auto rows = files->reload(pack, {}).rows;
        QCOMPARE(rows.size(), 2);
        const auto newBytes = archive(temp.path() + "/new.jar", "rootmod");
        root = server.file(11, 201, newBytes, {QJsonObject{{"modId", 99}, {"relationType", 3}}});
        server.requests.clear();
        done = false;
        installer.install(pack, rows, {request(root, false)}, [&](auto value) {
            result = value;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!server.requests.contains("/mods/99"));
        QCOMPARE(PackRepository::inspectContent(created.path).manifest.value("mods").toArray().size(), 2);
    }
    void disabledDependenciesAndCancellationDoNotPublishPreparedRequests() {
        CurseforgeFixture server;
        const auto f = server.file(11, 101, "root", {QJsonObject{{"modId", 12}, {"relationType", 3}}});
        CurseforgeInstallSource source(nullptr, server.base());
        source.setKey("fixture-key");
        const QVariantMap pack{{"minecraft", "1.21.1"}, {"loader", "fabric"}};
        bool done = false;
        QString error;
        source.prepare(pack,
                       {QVariantMap{{"category", "mods"},
                                    {"enabled", false},
                                    {"sourceIds", QVariantMap{{"curseforge", "12"}}}}},
                       {request(f)}, [&](auto prepared, QString result) {
                           QVERIFY(prepared.isEmpty());
                           error = result;
                           done = true;
                       });
        QTRY_VERIFY(done);
        QVERIFY(error.contains("disabled"));
        int callbacks = 0;
        source.prepare(pack, {}, {request(f)}, [&](auto prepared, QString result) {
            QVERIFY(prepared.isEmpty());
            QVERIFY(result.contains("cancelled"));
            ++callbacks;
        });
        source.cancel();
        QCOMPARE(callbacks, 1);
        QTest::qWait(50);
        QCOMPARE(callbacks, 1);
        QVERIFY(!source.busy());
    }
};
QTEST_GUILESS_MAIN(CurseforgeInstallTests)
#include "CurseforgeInstallTests.moc"
