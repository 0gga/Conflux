#include "features/content/ContentActions.h"
#include "features/content/ContentDependencyInstaller.h"
#include "features/explore/BulkContentAdd.h"
#include "features/preferences/ContentPreferences.h"
#include "miniz.h"
#include "platform/BrowseSettingsStore.h"
#include "platform/ContentInstaller.h"
#include "platform/NativeContentStore.h"
#include <QCryptographicHash>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
using namespace studio;
namespace {
Project project(QString title, QString id, QString provider = "modrinth") {
    Project p;
    p.title = title;
    p.id = id;
    p.provider = provider;
    p.type = "mod";
    p.url = provider == "modrinth" ? "https://modrinth.com/mod/" + id
                                   : "https://www.curseforge.com/minecraft/mc-mods/" + id;
    p.sourceIds[provider] = id;
    return p;
}
Release release(QString id) {
    Release r;
    r.provider = "modrinth";
    r.projectId = id;
    r.id = id + "-v1";
    r.version = "1";
    r.url = "https://example.test/" + id + ".jar";
    r.filename = id + ".jar";
    r.sha1 = QString(40, 'a');
    r.gameVersions = {"1.21.1"};
    r.loaders = {"fabric"};
    return r;
}
class Search final : public ICatalogueSearch {
  public:
    struct Call {
        CatalogueQuery query;
        Callback callback;
    };
    QList<Call> calls;
    QHash<QString, QVector<Project>> results;
    bool automatic = true;
    int active = 0, peak = 0;
    void fetch(const CatalogueQuery &q, int offset, int limit, Callback cb) override {
        Q_UNUSED(offset);
        Q_UNUSED(limit);
        ++active;
        peak = qMax(peak, active);
        calls << Call{q, cb};
        if (automatic) {
            --active;
            cb({results.value(q.provider + ':' + q.text), 0, {}});
        }
    }
    void finish(int index, QVector<Project> projects = {}) {
        auto cb = std::move(calls[index].callback);
        if (!cb)
            return;
        --active;
        cb({projects, 0, {}});
    }
    void facets(std::function<void(QVariantMap, QString)> cb) override {
        cb({}, {});
    }
    void setKey(const QByteArray &) override {}
};
class Versions final : public IContentCatalogue {
  public:
    QHash<QString, QVector<Release>> results;
    QList<QVariantMap> calls;
    bool automatic = true;
    VersionsCallback pending;
    void versions(const QVariantMap &p, const QString &game, const QString &loaders,
                  VersionsCallback cb) override {
        auto call = p;
        call["game"] = game;
        call["requestedLoaders"] = loaders;
        calls << call;
        if (automatic)
            cb(results.value(p.value("id").toString()), {});
        else
            pending = std::move(cb);
    }
    void refresh(const QVariantList &r, const QString &, const QString &, IndexCallback cb) override {
        cb(r, {});
    }
    void setCurseforgeKey(const QByteArray &) override {}
};
struct Fixture {
    QTemporaryDir temp;
    std::shared_ptr<Search> search = std::make_shared<Search>();
    std::shared_ptr<Versions> versions = std::make_shared<Versions>();
    std::shared_ptr<PackReleasePolicy> policy = std::make_shared<PackReleasePolicy>(
        std::make_shared<BrowseSettingsStore>(temp.path() + "/prefs.ini"));
    BulkContentAdd model{search, versions, policy};
    QVariantMap pack{
        {"path", temp.path() + "/A"}, {"name", "A"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
    Fixture() {
        model.open(pack, {});
    }
    void add(const QString &query, const Project &p) {
        search->results[p.provider + ':' + query] << p;
        versions->results[p.id] = {release(p.id)};
    }
};
} // namespace
class BulkContentTests : public QObject {
    Q_OBJECT
  private slots:
    void selectedMatchUsesRealIndependentInstallPipeline() {
        Fixture f;
        f.add("alpha", project("alpha", "alpha"));
        f.add("beta", project("beta", "beta"));
        const auto root = f.pack.value("path").toString();
        QVERIFY(QDir().mkpath(root + "/mods"));
        f.pack["provider"] = "conflux";
        const auto archivePath = f.temp.path() + "/beta.jar";
        mz_zip_archive zip{};
        QVERIFY(mz_zip_writer_init_file(&zip, archivePath.toUtf8().constData(), 0));
        const QByteArray metadata = R"({"schemaVersion":1,"id":"beta","name":"Beta","version":"1"})";
        QVERIFY(mz_zip_writer_add_mem(&zip, "fabric.mod.json", metadata.constData(), metadata.size(),
                                      MZ_BEST_COMPRESSION));
        QVERIFY(mz_zip_writer_finalize_archive(&zip));
        QVERIFY(mz_zip_writer_end(&zip));
        QFile archive(archivePath);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const auto bytes = archive.readAll();
        archive.close();
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int downloads = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("answered").toBool())
                    return;
                socket->setProperty("answered", true);
                ++downloads;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                              "\r\nConnection: close\r\n\r\n" + bytes);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        auto beta = release("beta");
        beta.sha1 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
        beta.url = "http://127.0.0.1:" + QString::number(server.serverPort()) + "/beta.jar";
        beta.fileSize = bytes.size();
        beta.dependencyRefs =
            QJsonArray{QJsonObject{{"project_id", "missing"}, {"dependency_type", "required"}}};
        f.versions->results["beta"] = {beta};
        struct MissingDependency final : IContentDependencySource {
            int calls = 0;
            void resolve(const QVariantMap &, const QVariantList &, const ContentDependency &,
                         const QStringList &, Callback done) override {
                ++calls;
                done({}, "Fixture dependency is unavailable");
            }
            void cancel() override {}
        };
        auto locks = std::make_shared<ContentLockRegistry>();
        auto files = std::make_shared<NativeContentStore>(nullptr, f.temp.path() + "/backups", locks);
        auto leaf = std::make_shared<ContentInstaller>(files, nullptr, true);
        auto missing = std::make_shared<MissingDependency>();
        auto coordinator = std::make_shared<ContentDependencyInstaller>(leaf, missing, files);
        ContentActions content(files, f.versions, coordinator,
                               std::make_shared<SettingsContentPreferences>(f.temp.path() + "/content.ini"));
        content.setReleasePolicy(f.policy);
        content.setLockRegistry(locks);
        content.setContext(f.pack, {});
        QTRY_VERIFY(!content.busy());
        f.model.open(f.pack, {});
        f.model.setInstaller([&](QVariantMap r, QVariantMap p) { return content.installRelease(r, p); });
        connect(&content, &ContentActions::installationFinished, &f.model,
                &BulkContentAdd::installationFinished);
        f.model.setInput("alpha\nbeta");
        f.model.resolveInput();
        QTRY_VERIFY(!f.model.busy());
        f.model.setSelected(f.model.rows()[0].toMap().value("key").toString(), false);
        f.model.installSelected();
        QTRY_COMPARE_WITH_TIMEOUT(downloads, 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!content.busy(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(f.model.rows()[1].toMap().value("state").toString(), QString("installed"),
                                  10000);
        QFile installed(root + "/mods/beta.jar");
        QVERIFY(installed.open(QIODevice::ReadOnly));
        QCOMPARE(installed.readAll(), bytes);
        QVERIFY(!QFileInfo::exists(root + "/mods/alpha.jar"));
        QCOMPARE(missing->calls, 1);
        QCOMPARE(downloads, 1);
        QCOMPARE(content.operations()->failedCount(), 1);
        int installedTasks = 0, dependencyWarnings = 0;
        for (const auto &value : content.operations()->entries()) {
            const auto task = value.toMap();
            if (task.value("state") == "done")
                ++installedTasks;
            if (task.value("action") == "Installing dependencies" && task.value("state") == "failed")
                ++dependencyWarnings;
        }
        QCOMPARE(installedTasks, 1);
        QCOMPARE(dependencyWarnings, 1);
        QCOMPARE(content.inventory().size(), 1);
    }
    void parseNamesLinksAndFilenames() {
        const auto p =
            parseBulkContentInput("- Sodium\nhttps://modrinth.com/mod/waystones\nsupplementaries, "
                                  "jade\njei-1.21.1-neoforge-19.21.jar\nfabric-api-0.116.0+1.21.1.jar");
        QVERIFY(p.error.isEmpty());
        QCOMPARE(p.references.size(), 6);
        QCOMPARE(p.references[0].query, "Sodium");
        QCOMPARE(p.references[1].provider, "modrinth");
        QCOMPARE(p.references[1].slug, "waystones");
        QCOMPARE(p.references[4].query, "jei");
        QCOMPARE(p.references[5].query, "fabric api");
        QVERIFY(p.references[4].filename);
        QCOMPARE(p.references[4].line, 4);
    }
    void rejectImpersonatedAndUnsupportedLinks_data() {
        QTest::addColumn<QString>("url");
        for (const auto &u :
             {"https://modrinth.com.evil.test/mod/example", "https://evil.test/modrinth.com/mod/example",
              "file://modrinth.com/mod/example", "https://user@modrinth.com/mod/example",
              "https://modrinth.com:9000/mod/example", "https://modrinth.com/modpack/example",
              "https://curseforge.com/other/mc-mods/example"})
            QTest::newRow(u) << QString(u);
    }
    void rejectImpersonatedAndUnsupportedLinks() {
        QFETCH(QString, url);
        auto p = parseBulkContentInput(url);
        QCOMPARE(p.references.size(), 1);
        QVERIFY(!p.references.first().error.isEmpty());
    }
    void providerLinksKeepTheirExactNamespaceAndQuery() {
        auto parsed = parseBulkContentInput("https://modrinth.com/mod/example?list=a,b");
        QCOMPARE(parsed.references.size(), 1);
        QCOMPARE(parsed.references.first().slug, "example");
        auto ref =
            parseBulkContentInput("https://www.curseforge.com/minecraft/mc-mods/example").references.first();
        auto linked = project("Example", "mr-id");
        linked.sourceIds["curseforge"] = "33";
        linked.sourceUrls["curseforge"] = "https://www.curseforge.com/minecraft/mc-mods/example";
        QCOMPARE(bulkContentMatchRank(ref, linked), 0);
        auto numeric =
            parseBulkContentInput("https://www.curseforge.com/minecraft/mc-mods/33").references.first();
        QCOMPARE(bulkContentMatchRank(numeric, linked), -1);
    }
    void inputBudgets() {
        QVERIFY(!parseBulkContentInput(QString(65537, 'a')).error.isEmpty());
        QVERIFY(parseBulkContentInput(QString("alpha\n").repeated(500)).error.isEmpty());
        auto tooMany = parseBulkContentInput(QString("alpha\n").repeated(501));
        QVERIFY(!tooMany.error.isEmpty());
        QVERIFY(tooMany.references.isEmpty());
    }
    void literalRankingAndExplicitLinkIdentity() {
        const auto ref = parseBulkContentInput("modabc").references.first();
        QCOMPARE(bulkContentMatchRank(ref, project("modabc", "exact")), 0);
        QCOMPARE(bulkContentMatchRank(ref, project("mod-abc", "punctuation")), 1);
        QVERIFY(bulkContentMatchRank(ref, project("modabc extra", "prefix")) > 1);
        auto compat = parseBulkContentInput("compat").references.first();
        QCOMPARE(bulkContentMatchRank(compat, project("Compact", "compact")), -1);
        auto link = parseBulkContentInput("https://modrinth.com/mod/exact").references.first();
        QCOMPARE(bulkContentMatchRank(link, project("Unrelated title", "exact")), 0);
        QCOMPARE(bulkContentMatchRank(link, project("exact", "different")), -1);
    }
    void checkboxesAndNextMatchPreserveUserChoice() {
        Fixture f;
        f.model.setCurseforgeKey("fixture");
        f.add("modabc", project("mod-abc", "punctuation"));
        f.add("modabc", project("modabc", "exact"));
        f.add("modabc", project("modabc extra", "prefix"));
        f.add("modabc", project("modabc", "cf-exact", "curseforge"));
        f.model.setInput("modabc");
        f.model.resolveInput();
        QTRY_VERIFY(!f.model.busy());
        QCOMPARE(f.model.rows().size(), 1);
        auto row = f.model.rows().first().toMap();
        QCOMPARE(row.value("id").toString(), "exact");
        QCOMPARE(row.value("candidateCount").toInt(), 4);
        QCOMPARE(f.model.selectedCount(), 1);
        const auto key = row.value("key").toString();
        f.model.nextCandidate(key);
        QCOMPARE(f.model.rows().first().toMap().value("id").toString(), "cf-exact");
        f.model.setSelected(key, false);
        f.model.nextCandidate(key);
        QCOMPARE(f.model.rows().first().toMap().value("id").toString(), "punctuation");
        QCOMPARE(f.model.selectedCount(), 0);
        f.model.selectAll(true);
        QCOMPARE(f.model.selectedCount(), 1);
        QCOMPARE(f.search->calls.first().query.versions, QStringList{"1.21.1"});
        QCOMPARE(f.search->calls.first().query.loaders, QStringList{"fabric"});
    }
    void installedMatchCanCycleToUninstalledAlternative() {
        Fixture f;
        f.add("modabc", project("modabc", "exact"));
        f.add("modabc", project("mod-abc", "other"));
        f.model.open(f.pack,
                     {QVariantMap{{"category", "mods"}, {"sourceIds", QVariantMap{{"modrinth", "exact"}}}}});
        f.model.setInput("modabc");
        f.model.resolveInput();
        QTRY_VERIFY(!f.model.busy());
        QCOMPARE(f.model.rows().first().toMap().value("state").toString(), "installed");
        QCOMPARE(f.model.selectedCount(), 0);
        f.model.nextCandidate(f.model.rows().first().toMap().value("key").toString());
        QCOMPARE(f.model.selectedCount(), 1);
    }
    void boundedRequestsIgnoreOldAndDuplicateReplies() {
        Fixture f;
        f.search->automatic = false;
        f.model.setInput("alpha\nbeta\ngamma\ndelta\nepsilon");
        f.model.resolveInput();
        QTRY_COMPARE(f.search->calls.size(), 4);
        QCOMPARE(f.search->peak, 4);
        auto duplicate = f.search->calls[0].callback;
        auto packB = f.pack;
        packB["path"] = f.temp.path() + "/B";
        f.model.open(packB, {});
        f.model.setInput("newmod");
        f.model.resolveInput();
        for (int i = 0; i < 4; ++i)
            f.search->finish(i, {project("alpha", "old")});
        duplicate({{project("alpha", "obsolete")}, 0, {}});
        QTRY_COMPARE(f.search->calls.size(), 5);
        f.search->finish(4, {project("newmod", "newmod")});
        QTRY_VERIFY(!f.model.busy());
        QCOMPARE(f.search->peak, 4);
        QCOMPARE(f.model.rows().size(), 1);
        QCOMPARE(f.model.rows().first().toMap().value("id").toString(), "newmod");
    }
    void installsStayIndependentAndDeduplicateVerifiedProjects() {
        Fixture f;
        f.add("alpha", project("alpha", "alpha"));
        f.add("beta", project("beta", "beta"));
        f.versions->results["alpha"] = {};
        QList<QVariantMap> accepted;
        f.model.setInstaller([&](QVariantMap r, QVariantMap p) {
            accepted << p;
            f.model.installationFinished(f.pack.value("path").toString(), {r}, {}, {});
            return true;
        });
        f.model.setInput("alpha\nbeta\nhttps://modrinth.com/mod/beta");
        f.model.resolveInput();
        QTRY_VERIFY(!f.model.busy());
        // The explicit link has its own provider lookup query.
        f.model.installSelected();
        QTRY_VERIFY(!f.model.operationsBusy());
        QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first().value("id").toString(), "beta");
        QCOMPARE(f.model.rows()[0].toMap().value("state").toString(), "error");
        QCOMPARE(f.model.rows()[1].toMap().value("state").toString(), "installed");
        QCOMPARE(f.model.rows()[2].toMap().value("state").toString(), "installed");
    }
    void acceptedJobsKeepOriginalPackAfterDismissalAndNavigation() {
        Fixture f;
        f.add("alpha", project("alpha", "alpha"));
        f.add("beta", project("beta", "beta"));
        f.versions->automatic = false;
        QList<QVariantMap> accepted;
        f.model.setInstaller([&](QVariantMap, QVariantMap p) {
            accepted << p;
            return true;
        });
        f.model.setInput("alpha\nbeta");
        f.model.resolveInput();
        QTRY_VERIFY(!f.model.busy());
        f.model.installSelected();
        QTRY_VERIFY(bool(f.versions->pending));
        f.model.dismiss();
        auto packB = f.pack;
        packB["path"] = f.temp.path() + "/B";
        f.model.open(packB, {});
        auto finish = std::move(f.versions->pending);
        finish({release("alpha")}, {});
        QTRY_VERIFY(bool(f.versions->pending));
        finish = std::move(f.versions->pending);
        finish({release("beta")}, {});
        QTRY_VERIFY(!f.model.operationsBusy());
        QCOMPARE(accepted.size(), 2);
        for (const auto &p : accepted)
            QCOMPARE(p.value("targetPack").toMap().value("path"), f.pack.value("path"));
        QCOMPARE(f.model.pack().value("path"), packB.value("path"));
        QVERIFY(f.model.rows().isEmpty());
    }
    void releasePolicyLoaderAndMultipleMinecraftVersions() {
        Fixture f;
        f.add("alpha", project("alpha", "alpha"));
        auto bad = release("alpha");
        bad.loaders = {"neoforge"};
        auto beta = release("alpha");
        beta.channel = "beta";
        auto good = release("alpha");
        good.gameVersions = {"1.21", "1.21.1"};
        f.versions->results["alpha"] = {bad, beta, good};
        QVariantMap installed;
        f.model.setInstaller([&](QVariantMap r, QVariantMap) {
            installed = r;
            return true;
        });
        f.model.setInput("alpha");
        f.model.resolveInput();
        QTRY_VERIFY(!f.model.busy());
        f.model.installSelected();
        QTRY_VERIFY(!f.model.operationsBusy());
        QCOMPARE(installed.value("channel").toString(), "release");
        QCOMPARE(installed.value("loaders").toStringList(), QStringList{"fabric"});
    }
};
QTEST_GUILESS_MAIN(BulkContentTests)
#include "BulkContentTests.moc"
