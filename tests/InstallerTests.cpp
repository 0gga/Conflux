#include "services/CatalogueClient.h"
#include "services/ModInstaller.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <QtTest>

using namespace studio;
class FixtureServer : public QTcpServer {
  public:
    QMap<QString, QByteArray> responses;
    QStringList requests;
    FixtureServer() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto input = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", input);
                    if (!input.contains("\r\n\r\n") || socket->property("answered").toBool())
                        return;
                    socket->setProperty("answered", true);
                    const auto target = QString::fromUtf8(input.split(' ').value(1));
                    requests << target;
                    const auto path = QUrl(target).path();
                    const auto found = responses.contains(path);
                    const auto data = responses.value(path, "not found");
                    socket->write(QByteArray("HTTP/1.1 ") + (found ? "200 OK" : "404 Not Found") +
                                  "\r\nContent-Length: " + QByteArray::number(data.size()) +
                                  "\r\nConnection: close\r\n\r\n" + data);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl base() const {
        return QUrl("http://127.0.0.1:" + QString::number(serverPort()) + "/");
    }
    QJsonObject version(const QString &project, const QString &id, const QByteArray &file,
                        const QJsonArray &deps = {}) {
        QJsonObject result{
            {"project_id", project},
            {"id", id},
            {"name", id},
            {"version_number", id},
            {"date_published", "2026-10-19T10:00:00Z"},
            {"version_type", "release"},
            {"game_versions", QJsonArray{"1.21.1"}},
            {"loaders", QJsonArray{"fabric"}},
            {"dependencies", deps},
            {"files",
             QJsonArray{QJsonObject{
                 {"filename", id + ".jar"},
                 {"primary", true},
                 {"url", base().resolved(QUrl("files/" + id)).toString()},
                 {"size", file.size()},
                 {"hashes",
                  QJsonObject{{"sha1", QString::fromLatin1(
                                           QCryptographicHash::hash(file, QCryptographicHash::Sha1).toHex())},
                              {"sha512",
                               QString::fromLatin1(
                                   QCryptographicHash::hash(file, QCryptographicHash::Sha512).toHex())}}}}}}};
        responses.insert("/version/" + id, QJsonDocument(result).toJson());
        responses.insert("/project/" + project,
                         QJsonDocument(QJsonObject{{"title", "Project " + project}}).toJson());
        responses.insert("/project/" + project + "/version", QJsonDocument(QJsonArray{result}).toJson());
        responses.insert("/files/" + id, file);
        return result;
    }
};
class InstallerTests : public QObject {
    Q_OBJECT
    static QJsonObject dep(const QString &project, const QString &version = {},
                           const QString &type = "required") {
        return {{"project_id", project}, {"version_id", version}, {"dependency_type", type}};
    }
    static QByteArray read(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return f.readAll();
    }
    static void write(const QString &path, const QByteArray &data) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(data);
    }
    static RepositoryResult install(ModInstaller &installer, const QString &path, const Release &release,
                                    int timeout = 10000) {
        bool done = false;
        RepositoryResult result;
        const auto connection =
            connect(&installer, &ModInstaller::finished, &installer, [&](const RepositoryResult &value) {
                result = value;
                done = true;
            });
        installer.start(path, release, "Root mod", {{"modrinth", release.projectId}});
        QElapsedTimer time;
        time.start();
        while (!done && time.elapsed() < timeout)
            QTest::qWait(10);
        disconnect(connection);
        return done ? result : RepositoryResult{false, "Test timed out"};
    }
  private slots:
    void excludedDependencyDoesNotDownloadOrModifyTheInstance_data() {
        QTest::addColumn<bool>("pinned");
        QTest::newRow("project-dependency") << false;
        QTest::newRow("exact-pinned-dependency") << true;
    }
    void excludedDependencyDoesNotDownloadOrModifyTheInstance() {
        QFETCH(bool, pinned);
        FixtureServer server;
        QTemporaryDir temp;
        auto pack = PackRepository::create(temp.path(), "Stage guard", "1.21.1", "fabric");
        QVERIFY(pack.ok);
        const auto before = read(pack.path + "/pack.json");
        auto root = modrinthRelease(
            server.version("root", "root-v1", "root bytes", {dep("library", pinned ? "library-v1" : "")}));
        auto library = server.version("library", "library-v1", "library bytes");
        library["version_type"] = "beta";
        server.responses["/version/library-v1"] = QJsonDocument(library).toJson();
        server.responses["/project/library/version"] = QJsonDocument(QJsonArray{library}).toJson();
        ContentInstallRequest request{root, "Root", {{"modrinth", "root"}}};
        request.allowedChannels = {"release"};
        ModInstaller installer(nullptr, server.base());
        QSignalSpy finished(&installer, &ModInstaller::finished);
        installer.startBatch(pack.path, {request});
        QTRY_COMPARE(finished.size(), 1);
        const auto result = qvariant_cast<RepositoryResult>(finished.first().first());
        QVERIFY(!result.ok);
        QCOMPARE(read(pack.path + "/pack.json"), before);
        QVERIFY(!QFile::exists(pack.path + "/mods/root-v1.jar"));
        for (const auto &url : server.requests)
            QVERIFY(!url.startsWith("/files/"));
    }

    void initialInstallsKeepCachedIncompatibilityGuardsWithoutFetchingUnrelatedGraphs() {
        for (bool reverse : {false, true}) {
            FixtureServer server;
            QTemporaryDir temp;
            auto pack = PackRepository::create(temp.path(), "Initial guard", "1.21.1", "fabric");
            QVERIFY(pack.ok);
            ModInstaller installer(nullptr, server.base());
            auto installed = modrinthRelease(
                server.version("existing", "old", "existing bytes",
                               reverse ? QJsonArray{dep("newroot", "", "incompatible")} : QJsonArray{}));
            QVERIFY(install(installer, pack.path, installed).ok);
            server.requests.clear();
            auto next = modrinthRelease(
                server.version("newroot", "new", "new bytes",
                               reverse ? QJsonArray{} : QJsonArray{dep("existing", "", "incompatible")}));
            const auto result = install(installer, pack.path, next);
            QVERIFY(!result.ok);
            QVERIFY(result.error.contains("incompatible"));
            for (const auto &url : server.requests)
                QVERIFY(!url.contains("existing"));
            QCOMPARE(PackRepository::inspect(pack.path).manifest.value("mods").toArray().size(), 1);
        }
    }
    void directUpdateKeepsUnrelatedPinsAndSkipsDependencyRequests() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Update", "1.21.1", "fabric");
        QVERIFY(pack.ok);
        ModInstaller installer(nullptr, server.base());
        const auto old = modrinthRelease(server.version("root", "old", "old bytes"));
        QVERIFY(install(installer, pack.path, old).ok);
        const auto unrelated = modrinthRelease(server.version("other", "other-old", "other bytes"));
        QVERIFY(install(installer, pack.path, unrelated).ok);
        server.requests.clear();
        auto next = modrinthRelease(
            server.version("root", "new", "new bytes", {dep("missing-library", "incompatible-version")}));
        next.dependencyRefs.append(dep("other", "", "incompatible"));
        next.gameVersions = {"1.21", "1.21.1", "1.21.4"};
        next.loaders = {"fabric", "neoforge"};
        bool done = false;
        RepositoryResult result;
        connect(&installer, &ModInstaller::finished, &installer, [&](RepositoryResult value) {
            result = value;
            done = true;
        });
        QSignalSpy progress(&installer, &ModInstaller::progress);
        installer.startBatch(pack.path, {{next, "Root", {{"modrinth", "root"}}, "mod", false}});
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.manifest.value("minecraft").toString(), QString("1.21.1"));
        QCOMPARE(result.manifest.value("loader").toString(), QString("fabric"));
        QCOMPARE(result.manifest.value("mods").toArray().size(), 2);
        for (const auto &url : server.requests) {
            QVERIFY2(!url.contains("version/"), qPrintable(url));
            QVERIFY(!url.contains("other"));
            QVERIFY(!url.contains("library"));
        }
        for (const auto &row : progress)
            QVERIFY(!row.first().toString().contains("Resolving"));
        auto wrong = next;
        wrong.id = "wrong";
        wrong.gameVersions = {"1.20.1"};
        done = false;
        installer.startBatch(pack.path, {{wrong, "Root", {{"modrinth", "root"}}, "mod", false}});
        QTRY_VERIFY(done);
        QVERIFY(!result.ok);
        wrong.gameVersions = {"1.21.1"};
        wrong.loaders = {"forge"};
        done = false;
        installer.startBatch(pack.path, {{wrong, "Root", {{"modrinth", "root"}}, "mod", false}});
        QTRY_VERIFY(done);
        QVERIFY(!result.ok);
    }

    void fabricBridgeAdmissionIsExplicitAndDependencyQueriesKeepBothLoaders() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Bridge", "1.21.1", "neoforge");
        QVERIFY(pack.ok);
        server.version("library", "library-v1", "library bytes");
        const auto release =
            modrinthRelease(server.version("root", "root-v1", "root bytes", {dep("library")}));
        ModInstaller installer(nullptr, server.base());
        const auto rejected = install(installer, pack.path, release);
        QVERIFY(!rejected.ok);
        RepositoryResult result;
        bool done = false;
        connect(&installer, &ModInstaller::finished, &installer, [&](const RepositoryResult &value) {
            result = value;
            done = true;
        });
        installer.startBatch(pack.path, {{release, "Root", {{"modrinth", "root"}}}}, true);
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.manifest.value("loader").toString(), QString("neoforge"));
        QCOMPARE(result.manifest.value("mods").toArray().size(), 2);
        bool unionQuery = false;
        for (const auto &target : server.requests)
            if (target.contains("/project/library/version")) {
                const QUrlQuery query(QUrl("http://fixture" + target));
                const auto loaders =
                    QJsonDocument::fromJson(query.queryItemValue("loaders").toUtf8()).array();
                unionQuery = loaders.contains("neoforge") && loaders.contains("fabric");
            }
        QVERIFY(unionQuery);
    }
    void disabledDependenciesAreNotSilentlyEnabledOrTreatedAsActive() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Disabled dependency", "1.21.1", "fabric");
        QVERIFY(pack.ok);
        const auto lib = modrinthRelease(server.version("lib", "lib-v1", "library"));
        ModInstaller installer(nullptr, server.base());
        QVERIFY(install(installer, pack.path, lib).ok);
        auto current = PackRepository::inspect(pack.path);
        auto refs = current.manifest.value("mods").toArray();
        auto ref = refs.first().toObject();
        const auto path = ref.value("installPath").toString();
        QVERIFY(QFile::rename(pack.path + '/' + path, pack.path + '/' + path + ".disabled"));
        ref["installPath"] = path + ".disabled";
        refs[0] = ref;
        QVERIFY(PackRepository::updateContentPins(pack.path, current.manifest, refs).ok);
        const auto before = read(pack.path + "/pack.json");
        const auto required =
            modrinthRelease(server.version("root", "required", "root", {dep("lib", "lib-v1")}));
        const auto failed = install(installer, pack.path, required);
        QVERIFY(!failed.ok);
        QVERIFY(failed.error.contains("disabled", Qt::CaseInsensitive));
        QCOMPARE(read(pack.path + "/pack.json"), before);
        const auto incompatible = modrinthRelease(
            server.version("root", "incompatible", "other-root", {dep("lib", "", "incompatible")}));
        const auto success = install(installer, pack.path, incompatible);
        QVERIFY2(success.ok, qPrintable(success.error));
        QVERIFY(QFileInfo::exists(pack.path + '/' + path + ".disabled"));
    }
    void batchUpdatesResolveTogetherAndPreserveDisabledState() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Batch", "1.21.1", "fabric");
        QVERIFY(pack.ok);
        const auto b1 = modrinthRelease(server.version("b", "b-v1", "b-one"));
        ModInstaller installer(nullptr, server.base());
        QVERIFY(install(installer, pack.path, b1).ok);
        const auto b2 = modrinthRelease(server.version("b", "b-v2", "b-two"));
        const auto a = modrinthRelease(server.version("a", "a-v1", "a-one", {dep("b", "b-v2")}));
        QSignalSpy finished(&installer, &ModInstaller::finished);
        installer.startBatch(pack.path, {{a, "A", {{"modrinth", "a"}}}, {b2, "B", {{"modrinth", "b"}}}});
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        auto current = PackRepository::inspect(pack.path);
        QVERIFY2(current.ok, qPrintable(current.error));
        QCOMPARE(current.manifest.value("mods").toArray().size(), 2);
        auto pins = current.manifest.value("mods").toArray();
        for (int i = 0; i < pins.size(); ++i) {
            auto ref = pins.at(i).toObject();
            if (ref.value("projectId") != "b")
                continue;
            const auto path = ref.value("installPath").toString();
            QVERIFY(QFile::rename(pack.path + '/' + path, pack.path + '/' + path + ".disabled"));
            ref["installPath"] = path + ".disabled";
            ref["enabled"] = false;
            pins[i] = ref;
        }
        QVERIFY(PackRepository::updateContentPins(pack.path, current.manifest, pins).ok);
        // A standalone disabled mod update keeps the disabled suffix.
        const auto only = PackRepository::create(temp.path(), "Disabled", "1.21.1", "fabric");
        QVERIFY(only.ok);
        QVERIFY(install(installer, only.path, b1).ok);
        current = PackRepository::inspect(only.path);
        pins = current.manifest.value("mods").toArray();
        auto ref = pins.first().toObject();
        const auto old = ref.value("installPath").toString();
        QVERIFY(QFile::rename(only.path + '/' + old, only.path + '/' + old + ".disabled"));
        ref["installPath"] = old + ".disabled";
        pins[0] = ref;
        QVERIFY(PackRepository::updateContentPins(only.path, current.manifest, pins).ok);
        const auto result = install(installer, only.path, b2);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto installed = result.manifest.value("mods").toArray().first().toObject();
        QVERIFY(installed.value("installPath").toString().endsWith(".jar.disabled"));
        QVERIFY(QFileInfo::exists(only.path + '/' + installed.value("installPath").toString()));
    }
    void requiredAndVersionOnlyDependenciesAreDownloadedOnce() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Dependencies", "1.21.1", "fabric");
        QVERIFY(pack.ok);
        const auto root =
            server.version("root", "root-v1", "root-data",
                           {dep("library", "lib-v1"), dep("", "lib-v1"), dep("optional", "", "optional")});
        server.version("library", "lib-v1", "library-data", {dep("root", "root-v1")});
        ModInstaller installer(nullptr, server.base());
        const auto result = install(installer, pack.path, modrinthRelease(root));
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.manifest.value("mods").toArray().size(), 2);
        QCOMPARE(server.requests.count("/files/lib-v1"), 1);
        for (const auto &value : result.manifest.value("mods").toArray()) {
            const auto ref = value.toObject();
            const auto data = read(QDir(pack.path).filePath(ref.value("installPath").toString()));
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex()),
                     ref.value("sha1").toString());
        }
        QVERIFY(!server.requests.join(',').contains("optional"));
    }
    void compatibleStableDependencyIsChosenAndCachedFilesAreReused() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Stable", "1.21.1", "fabric");
        auto stable = server.version("lib", "stable", "stable-data");
        auto beta = server.version("lib", "beta", "beta-data");
        beta.insert("version_type", "beta");
        beta.insert("date_published", "2026-10-20T00:00:00Z");
        server.responses.insert("/project/lib/version", QJsonDocument(QJsonArray{beta, stable}).toJson());
        const auto root = server.version("root", "root-v1", "root-data", {dep("lib")});
        ModInstaller installer(nullptr, server.base());
        auto result = install(installer, pack.path, modrinthRelease(root));
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(server.requests.contains("/files/stable"));
        QVERIFY(!server.requests.contains("/files/beta"));
        QVERIFY(server.requests.join(',').contains("game_versions="));
        QVERIFY(server.requests.join(',').contains("loaders="));
        server.requests.clear();
        result = install(installer, pack.path, modrinthRelease(root));
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!server.requests.join(',').contains("/files/"));
    }
    void corruptDownloadPreservesManifestAndManualFiles() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Corrupt", "1.21.1", "fabric");
        QDir().mkpath(pack.path + "/mods");
        write(pack.path + "/mods/manual.jar", "manual");
        const auto root = server.version("root", "bad", "expected");
        server.responses["/files/bad"] = "tampered";
        const auto before = read(pack.path + "/pack.json");
        ModInstaller installer(nullptr, server.base());
        const auto result = install(installer, pack.path, modrinthRelease(root));
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("checksum"));
        QCOMPARE(read(pack.path + "/pack.json"), before);
        QCOMPARE(read(pack.path + "/mods/manual.jar"), QByteArray("manual"));
        QCOMPARE(QDir(pack.path + "/mods").entryList(QDir::Files).size(), 1);
    }
    void pinnedVersionConflictIsReportedBeforeDownloading() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Conflict", "1.21.1", "fabric");
        const auto existing = modrinthRelease(server.version("lib", "lib-v2", "library-v2"));
        auto pinned = QJsonObject::fromVariantMap(existing.toVariant());
        pinned.insert("url", "https://example.invalid/library.jar");
        QVERIFY(PackRepository::pin(pack.path, pinned).ok);
        const auto before = read(pack.path + "/pack.json");
        const auto root = server.version("root", "root-v1", "root-data", {dep("lib", "lib-v1")});
        ModInstaller installer(nullptr, server.base());
        const auto result = install(installer, pack.path, modrinthRelease(root));
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("conflict", Qt::CaseInsensitive));
        QCOMPARE(read(pack.path + "/pack.json"), before);
        QVERIFY(!server.requests.join(',').contains("/files/"));
    }
    void incompatibleModsAreRejected() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Incompatible", "1.21.1", "fabric");
        server.version("lib", "lib-v1", "lib-data");
        const auto root =
            server.version("root", "root-v1", "root-data", {dep("lib"), dep("lib", "", "incompatible")});
        ModInstaller installer(nullptr, server.base());
        const auto result = install(installer, pack.path, modrinthRelease(root));
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("Incompatible"));
        QVERIFY(!server.requests.join(',').contains("/files/"));
    }
    void externalManifestEditsArePreserved() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Edited", "1.21.1", "fabric");
        const auto root = server.version("root", "root-v1", "root-data");
        auto changed = pack.manifest;
        changed.insert("name", "Edited outside the app");
        ModInstaller installer(nullptr, server.base());
        connect(&installer, &ModInstaller::progress, this, [&](const QString &status) {
            if (status.startsWith("Downloading"))
                write(pack.path + "/pack.json", QJsonDocument(changed).toJson());
        });
        const auto result = install(installer, pack.path, modrinthRelease(root));
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("changed"));
        QCOMPARE(QJsonDocument::fromJson(read(pack.path + "/pack.json")).object(), changed);
        QVERIFY(QDir(pack.path + "/mods").entryList(QDir::Files).isEmpty());
    }
    void cancellationLeavesNoInstalledMods() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Cancelled", "1.21.1", "fabric");
        const auto root = server.version("root", "root-v1", "root-data");
        const auto before = read(pack.path + "/pack.json");
        ModInstaller installer(nullptr, server.base());
        connect(&installer, &ModInstaller::progress, this, [&](const QString &status) {
            if (status.startsWith("Downloading"))
                QTimer::singleShot(0, &installer, &ModInstaller::cancel);
        });
        const auto result = install(installer, pack.path, modrinthRelease(root));
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("cancelled"));
        QCOMPARE(read(pack.path + "/pack.json"), before);
    }
    void upgradingAndRemovingOnlyDeletesOwnedFiles() {
        FixtureServer server;
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Upgrade", "1.21.1", "fabric");
        ModInstaller installer(nullptr, server.base());
        auto result =
            install(installer, pack.path, modrinthRelease(server.version("root", "v1", "version-one")));
        QVERIFY(result.ok);
        const auto oldPath =
            result.manifest.value("mods").toArray().first().toObject().value("installPath").toString();
        write(pack.path + "/mods/manual.jar", "manual");
        result = install(installer, pack.path, modrinthRelease(server.version("root", "v2", "version-two")));
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!QFile::exists(pack.path + '/' + oldPath));
        QCOMPARE(read(pack.path + "/mods/manual.jar"), QByteArray("manual"));
        const auto installed =
            result.manifest.value("mods").toArray().first().toObject().value("installPath").toString();
        result = PackRepository::remove(pack.path, "modrinth", "root");
        QVERIFY(result.ok);
        QVERIFY(!QFile::exists(pack.path + '/' + installed));
        QCOMPARE(read(pack.path + "/mods/manual.jar"), QByteArray("manual"));
    }
    void incompleteStagingRollsBackFilesAndManifest() {
        FixtureServer server;
        QTemporaryDir temp, staging;
        const auto pack = PackRepository::create(temp.path(), "Rollback", "1.21.1", "fabric");
        const auto before = read(pack.path + "/pack.json");
        const auto first = modrinthRelease(server.version("a", "a-v1", "first-file"));
        const auto second = modrinthRelease(server.version("b", "b-v1", "second-file"));
        write(staging.path() + '/' + first.sha1 + ".jar", "first-file");
        QJsonArray references;
        for (const auto &release : {first, second}) {
            auto ref = QJsonObject::fromVariantMap(release.toVariant());
            ref.insert("installPath", "mods/" + release.sha1 + ".jar");
            references.append(ref);
        }
        const auto result =
            PackRepository::applyInstallation(pack.path, pack.manifest, references, staging.path());
        QVERIFY(!result.ok);
        QCOMPARE(read(pack.path + "/pack.json"), before);
        QVERIFY(QDir(pack.path + "/mods").entryList(QDir::Files).isEmpty());
    }
    void liveModrinthInstallIncludesRequiredDependencies() {
        if (!qEnvironmentVariableIsSet("MODPACK_STUDIO_LIVE_TEST"))
            QSKIP("Live Modrinth installation is opt-in.");
        CatalogueClient client;
        bool loaded = false;
        ProjectDetail detail;
        connect(&client, &CatalogueClient::detailFinished, this, [&](int, const ProjectDetail &result) {
            detail = result;
            loaded = true;
        });
        Project project;
        project.provider = "modrinth";
        project.id = "iris";
        project.sourceIds.insert("modrinth", "iris");
        client.loadProject(project, "1.21.1", "fabric", 1);
        QTRY_VERIFY_WITH_TIMEOUT(loaded, 25000);
        QVERIFY2(detail.errors.isEmpty(), qPrintable(detail.errors.join(';')));
        QVERIFY(!detail.releases.isEmpty());
        const auto release = detail.releases.first();
        QVERIFY(!release.requiredDependencies.isEmpty());
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Live dependencies", "1.21.1", "fabric");
        QVERIFY(pack.ok);
        ModInstaller installer;
        const auto result = install(installer, pack.path, release, 60000);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto mods = result.manifest.value("mods").toArray();
        QVERIFY(mods.size() >= 2);
        for (const auto &value : mods) {
            const auto ref = value.toObject();
            const auto bytes = read(pack.path + '/' + ref.value("installPath").toString());
            QVERIFY(!bytes.isEmpty());
            QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex()),
                     ref.value("sha1").toString());
        }
    }
};
QTEST_GUILESS_MAIN(InstallerTests)
#include "InstallerTests.moc"
