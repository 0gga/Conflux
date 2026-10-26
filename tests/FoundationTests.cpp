#include "domain/Catalogue.h"
#include "platform/ProjectAuthors.h"
#include "platform/ReviewedAuthorIdentities.h"
#include "services/CatalogueClient.h"
#include "services/InstallationDiscovery.h"
#include "services/PackRepository.h"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace studio;
class FoundationTests : public QObject {
    Q_OBJECT
  private:
    static Project project(const QString &provider, const QString &id) {
        Project p;
        p.provider = provider;
        p.id = id;
        p.title = "Same display name";
        p.summary = provider + " description";
        return p;
    }
    static Release release(const QString &provider, const QString &id,
                           const QString &hash = "0123456789012345678901234567890123456789") {
        Release r;
        r.provider = provider;
        r.projectId = "project-1";
        r.id = id;
        r.version = "1.0";
        r.gameVersions = {"1.21.1"};
        r.loaders = {"fabric"};
        r.sha1 = hash;
        r.url = "https://cdn.modrinth.com/example.jar";
        r.filename = "example.jar";
        return r;
    }
    static QByteArray runGit(const QString &path, const QStringList &args) {
        QProcess process;
        process.setWorkingDirectory(path);
        process.start("git", args);
        if (!process.waitForFinished(10000))
            return "FAILED";
        return process.readAllStandardOutput();
    }
    static bool writeFile(const QString &path, const QByteArray &data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
  private slots:
    void releaseVariantsPreservePublicationTimeAndTimezone() {
        auto value = release("modrinth", "exact-time");
        for (const auto &timestamp :
             {"2026-10-22T11:16:33.063140+00:00", "2026-10-22T13:16:33+02:00", "2026-10-22", ""}) {
            value.published = timestamp;
            const auto row = value.toVariant();
            QCOMPARE(row.value("published").toString(), QString(timestamp));
            QCOMPARE(releaseFromVariant(row).published, QString(timestamp));
        }
    }

    void authorRostersKeepExactSeparateIdentitiesAndStableOwnershipOrder() {
        const auto member = [](const QString &id, const QString &name, const QString &role,
                               bool accepted = true) {
            return QJsonObject{{"user", QJsonObject{{"id", id},
                                                    {"username", name},
                                                    {"avatar_url", "https://example.test/icon.png"}}},
                               {"role", role},
                               {"accepted", accepted}};
        };
        const auto authors = ProjectAuthors::modrinthTeam(
            {member("two", "Second, Author", "Developer"), member("one", "First_Author", "owner"),
             member("two", "Second, Author", "Developer"), member("pending", "Pending", "Developer", false),
             member("three", "First_Author", "Developer")});
        QCOMPARE(authors.size(), 3);
        QCOMPARE(authors[0].toMap().value("name").toString(), QString("First_Author"));
        QCOMPARE(authors[0].toMap().value("ids").toMap().value("modrinth").toString(), QString("one"));
        QCOMPARE(authors[1].toMap().value("name").toString(), QString("Second, Author"));
        QCOMPARE(authors[2].toMap().value("ids").toMap().value("modrinth").toString(), QString("three"));
        const auto cf = ProjectAuthors::curseforge(
            {QJsonObject{{"id", 12}, {"name", "One"}}, QJsonObject{{"id", 34}, {"name", "Two"}},
             QJsonObject{{"id", 12}, {"name", "One"}}, QJsonObject{{"id", 0}, {"name", "Unlinked"}}});
        QCOMPARE(cf.size(), 3);
        QCOMPARE(cf[1].toMap().value("ids").toMap().value("curseforge").toString(), QString("34"));
        QVERIFY(cf[2].toMap().value("ids").toMap().isEmpty());
        Project p;
        p.author = "Legacy, Name";
        p.authorIds = {{"modrinth", "exact-id"}};
        QCOMPARE(p.toVariant().value("authors").toList().size(), 1);
        p.authors = authors;
        QCOMPARE(p.toVariant().value("authors").toList(), authors);
    }
    void authorIdentityLinksRejectAmbiguousNamesAndPreserveExactProviderIds() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/authors.json";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            R"({"modrinth:mr123":"owner","curseforge:456":"owner","modrinth:other":"ambiguous","modrinth:another":"ambiguous","curseforge:789":"ambiguous"})");
        file.close();
        const auto linked = ReviewedAuthorIdentities::expand({{"modrinth", "mr123"}}, path);
        QCOMPARE(linked.value("curseforge"), QString("456"));
        const auto separate = ReviewedAuthorIdentities::expand({{"modrinth", "same-display-name"}}, path);
        QVERIFY(!separate.contains("curseforge"));
        QCOMPARE(ReviewedAuthorIdentities::expand({{"modrinth", "other"}}, path).size(), 1);
    }
    void updatesDoNotDowngradeOrChangeProjectIdentity() {
        Release installed;
        installed.projectId = "project";
        installed.id = "beta";
        installed.published = "2026-10-20T12:00:00Z";
        auto candidate = installed;
        candidate.id = "stable";
        candidate.published = "2026-10-19T12:00:00Z";
        QVERIFY(!isNewerRelease(installed, candidate));
        candidate.published = "2026-10-21T12:00:00Z";
        QVERIFY(isNewerRelease(installed, candidate));
        candidate.projectId = "another-project";
        QVERIFY(!isNewerRelease(installed, candidate));
        candidate.projectId = installed.projectId;
        installed.version = "2.0.0-beta";
        candidate.version = "1.9.9";
        QVERIFY(!isNewerRelease(installed, candidate));
        candidate.projectId = installed.projectId;
        candidate.published = "unknown";
        QVERIFY(!isNewerRelease(installed, candidate));
    }
    void identicalFilePublicationIsNotAnUpdateButChangedRebuildCanBe() {
        Release installed;
        installed.projectId = "project";
        installed.id = "old";
        installed.version = "1.6.6";
        installed.published = "2026-07-29T07:43:04Z";
        installed.sha1 = QString(40, 'a');
        installed.sha512 = QString(128, 'b');
        auto candidate = installed;
        candidate.id = "republished";
        candidate.published = "2026-07-29T07:43:41Z";
        QVERIFY(!isNewerRelease(installed, candidate));
        candidate.sha1 = installed.sha1.toUpper();
        candidate.sha512.clear();
        QVERIFY(!isNewerRelease(installed, candidate));
        candidate.sha1 = QString(40, 'c');
        candidate.sha512 = installed.sha512.toUpper();
        QVERIFY(!isNewerRelease(installed, candidate));
        candidate.sha512 = QString(128, 'd');
        QVERIFY(isNewerRelease(installed, candidate));
    }
    void projectIdentityRequiresEvidence() {
        const QVector<Project> input{project("curseforge", "123"), project("modrinth", "abc")};
        QCOMPARE(mergeProjects(input).size(), 2);
        const QHash<QString, QString> identities{{"curseforge:123", "verified-project"},
                                                 {"modrinth:abc", "verified-project"}};
        const auto merged = mergeProjects(input, identities);
        QCOMPARE(merged.size(), 1);
        QCOMPARE(merged.first().provider, "modrinth");
        QCOMPARE(merged.first().summary, "modrinth description");
        QCOMPARE(merged.first().sourceIds.size(), 2);
        QCOMPARE(mergeProjects({input.at(1), input.at(0)}, identities).first().provider, "modrinth");
    }
    void downloadTotalsKeepExactProviderBreakdownsAndDoNotDoubleCountMerges() {
        Project mr;
        mr.provider = "modrinth";
        mr.id = "abc";
        mr.downloads = 83389414;
        Project cf;
        cf.provider = "curseforge";
        cf.id = "123";
        cf.downloads = 7205;
        const QHash<QString, QString> ids{{"modrinth:abc", "pair"}, {"curseforge:123", "pair"}};
        auto merged = mergeProjects({mr, cf}, ids).first();
        QCOMPARE(merged.totalDownloads(), qint64(83396619));
        const auto variant = merged.toVariant();
        QCOMPARE(variant.value("downloadsRaw").toLongLong(), qint64(83396619));
        const auto stats = variant.value("downloadStats").toList();
        QCOMPARE(stats.size(), 2);
        QCOMPARE(stats[0].toMap().value("raw").toLongLong(), mr.downloads);
        QCOMPARE(stats[1].toMap().value("raw").toLongLong(), cf.downloads);
        merged = mergeProjects({merged, mr, cf, merged}, ids).first();
        QCOMPARE(merged.totalDownloads(), qint64(83396619));
        QCOMPARE(mr.totalDownloads(), mr.downloads);
        merged.sourceDownloads["unverified"] = 99999999;
        QCOMPARE(merged.totalDownloads(), qint64(83396619));
    }
    void releaseEquivalencePreservesVariants() {
        auto cf = release("curseforge", "1"), mr = release("modrinth", "2");
        QCOMPARE(mergeReleases({cf, mr}).size(), 1);
        QCOMPARE(mergeReleases({mr, cf}).first().provider, "modrinth");
        QCOMPARE(mergeReleases({cf, mr}).first().sourceIds.size(), 2);
        mr.sha1 = "different-binary";
        QCOMPARE(mergeReleases({cf, mr}).size(), 2);
        mr.sha1 = cf.sha1;
        mr.loaders = {"neoforge"};
        QCOMPARE(mergeReleases({cf, mr}).size(), 2);
        mr.loaders = cf.loaders;
        mr.gameVersions = {"1.20.1"};
        QCOMPARE(mergeReleases({cf, mr}).size(), 2);
        mr.sha1.clear();
        cf.sha1.clear();
        QCOMPARE(mergeReleases({cf, mr}).size(), 2);
    }
    void decodedImagesIgnoreContainerEncoding() {
        QImage image(9, 7, QImage::Format_RGB32);
        image.fill(QColor("#45764d"));
        QByteArray png, bmp;
        QBuffer p(&png), b(&bmp);
        p.open(QIODevice::WriteOnly);
        b.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&p, "PNG"));
        QVERIFY(image.save(&b, "BMP"));
        QVERIFY(png != bmp);
        QCOMPARE(decodedImageFingerprint(png), decodedImageFingerprint(bmp));
        QVERIFY(decodedImageFingerprint("not an image").isEmpty());
    }
    void packCreationNeverOverwritesOrEscapesRoot() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto pack = PackRepository::create(temp.path(), "../../My pack; no shell", "1.21.1", "fabric");
        QVERIFY2(pack.ok, qPrintable(pack.error));
        QVERIFY(!pack.gitEnabled);
        QVERIFY(!QFileInfo::exists(pack.path + "/.git"));
        QCOMPARE(QFileInfo(pack.path).absolutePath(), QDir(temp.path()).absolutePath());
        const auto original = QJsonDocument(pack.manifest).toJson();
        const auto duplicate =
            PackRepository::create(temp.path(), "../../My pack; no shell", "1.21.1", "fabric");
        QVERIFY(!duplicate.ok);
        QCOMPARE(QJsonDocument(PackRepository::inspect(pack.path).manifest).toJson(), original);
        QVERIFY(!PackRepository::create(temp.path(), "Bad", "garbage", "fabric").ok);
        QCOMPARE(PackRepository::slug("CON"), "pack-con");
    }
    void pinCompatibilityAndCrossProviderDuplicates() {
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Test", "1.21.1", "fabric");
        QVERIFY(pack.ok);
        auto reference = QJsonObject::fromVariantMap(release("modrinth", "version-a").toVariant());
        reference.insert("projectSourceIds", QJsonObject{{"modrinth", "project-1"}, {"curseforge", "42"}});
        auto pinned = PackRepository::pin(pack.path, reference);
        QVERIFY2(pinned.ok, qPrintable(pinned.error));
        QCOMPARE(pinned.manifest.value("mods").toArray().size(), 1);
        reference.insert("provider", "curseforge");
        reference.insert("projectId", "42");
        pinned = PackRepository::pin(pack.path, reference);
        QVERIFY(pinned.ok);
        QCOMPARE(pinned.manifest.value("mods").toArray().size(), 1);
        reference.insert("loaders", QJsonArray{"neoforge"});
        QVERIFY(!PackRepository::pin(pack.path, reference).ok);
        reference.insert("loaders", QJsonArray{"fabric"});
        reference.insert("gameVersions", QJsonArray{"1.20.1"});
        QVERIFY(!PackRepository::pin(pack.path, reference).ok);
        const auto removed = PackRepository::remove(pack.path, "curseforge", "42");
        QVERIFY(removed.ok);
        QVERIFY(removed.manifest.value("mods").toArray().isEmpty());
    }
    void gitCommitStagesOnlyOwnedFiles() {
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "History", "1.21.1", "fabric", true);
        QVERIFY(pack.ok);
        runGit(pack.path, {"config", "user.name", "Foundation Test"});
        runGit(pack.path, {"config", "user.email", "foundation@example.invalid"});
        // Do not inherit any locally configured hook runner in this temporary test repository.
        runGit(pack.path, {"config", "core.hooksPath", QDir(temp.path()).filePath("no-hooks")});
        QVERIFY(writeFile(QDir(pack.path).filePath("private-token.txt"), "not a real secret"));
        const auto saved = PackRepository::commit(pack.path, "Initial pack");
        QVERIFY2(saved.ok, qPrintable(saved.error));
        QVERIFY(saved.history.contains("Initial pack"));
        const auto files = runGit(pack.path, {"ls-files"});
        QVERIFY(files.contains("pack.json"));
        QVERIFY(files.contains(".gitignore"));
        QVERIFY(!files.contains("private-token"));
        runGit(pack.path, {"add", "--", "private-token.txt"});
        QVERIFY(!PackRepository::commit(pack.path, "Must refuse unrelated staged file").ok);
        QCOMPARE(runGit(pack.path, {"rev-list", "--count", "HEAD"}).trimmed(), QByteArray("1"));
        QVERIFY(!PackRepository::commit(pack.path, "").ok);
    }
    void packCreationAndInspectionDoNotRequireAGitExecutable() {
        QTemporaryDir temp;
        const auto previous = qgetenv("PATH");
        const auto restore = qScopeGuard([previous] { qputenv("PATH", previous); });
        qputenv("PATH", temp.path().toUtf8());
        const auto pack = PackRepository::create(temp.path(), "Offline", "1.21.1", "fabric");
        QVERIFY2(pack.ok, qPrintable(pack.error));
        QVERIFY(!pack.gitEnabled);
        const auto inspected = PackRepository::inspect(pack.path);
        QVERIFY2(inspected.ok, qPrintable(inspected.error));
        QVERIFY(inspected.history.isEmpty());
        QVERIFY(!QFileInfo::exists(pack.path + "/.git"));
    }
    void packsWorkWithoutGitAndSetupDoesNotRewriteContent() {
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Without history", "1.21.1", "neoforge");
        QVERIFY2(pack.ok, qPrintable(pack.error));
        QVERIFY(!QFileInfo::exists(pack.path + "/.git"));
        const auto before = QJsonDocument(pack.manifest).toJson();
        QVERIFY(!PackRepository::commit(pack.path, "Requires opt-in").ok);
        QVERIFY(!PackRepository::enableGit(pack.path, "", "invalid").ok);
        QVERIFY(!QFileInfo::exists(pack.path + "/.git"));
        auto tracked = PackRepository::enableGit(pack.path, "Local Test", "test@example.invalid");
        QVERIFY2(tracked.ok, qPrintable(tracked.error));
        QVERIFY(tracked.gitEnabled);
        QCOMPARE(QJsonDocument(tracked.manifest).toJson(), before);
        QCOMPARE(runGit(pack.path, {"config", "--local", "user.name"}).trimmed(), QByteArray("Local Test"));
        QCOMPARE(runGit(pack.path, {"config", "--local", "user.email"}).trimmed(),
                 QByteArray("test@example.invalid"));
        auto withoutHistory = PackRepository::inspect(pack.path, false);
        QVERIFY(withoutHistory.ok);
        QVERIFY(withoutHistory.gitEnabled);
        QVERIFY(withoutHistory.history.isEmpty());
        QVERIFY(!PackRepository::gitIdentityError("Bad\nName", "test@example.invalid").isEmpty());
        QVERIFY(!PackRepository::gitIdentityError("Name", "not an email").isEmpty());
    }
    void malformedManifestIsRejected() {
        QTemporaryDir temp;
        const auto pack = PackRepository::create(temp.path(), "Broken", "1.21.1", "fabric");
        QVERIFY(pack.ok);
        QVERIFY(writeFile(QDir(pack.path).filePath("pack.json"), "{broken"));
        QVERIFY(!PackRepository::inspect(pack.path).ok);
    }
    void discoveryReadsBothSourcesWithoutChangingMetadata() {
        QTemporaryDir temp;
        const auto mr = QDir(temp.path()).filePath("ModrinthApp"),
                   cf = QDir(temp.path()).filePath("Instances");
        const auto mrProfile = QDir(mr).filePath("profiles/A"), cfProfile = QDir(cf).filePath("B");
        QVERIFY(QDir().mkpath(mrProfile + "/mods"));
        QVERIFY(QDir().mkpath(cfProfile + "/mods"));
        const QByteArray metadata =
            R"({"name":"Fixture Pack","gameVersion":"1.21.1","baseModLoader":{"name":"fabric-0.16"}})";
        QVERIFY(writeFile(mrProfile + "/minecraftinstance.json", metadata));
        QVERIFY(writeFile(cfProfile + "/minecraftinstance.json", metadata));
        QVERIFY(writeFile(cfProfile + "/mods/example.jar", "fixture"));
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "fixture");
            db.setDatabaseName(mr + "/app.db");
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE instances(name TEXT, path TEXT, icon_path TEXT)"));
            query.prepare("INSERT INTO instances VALUES(?, ?, ?)");
            query.addBindValue("Modrinth Fixture");
            query.addBindValue("A");
            query.addBindValue("");
            QVERIFY(query.exec());
        }
        QSqlDatabase::removeDatabase("fixture");
        const auto found = InstallationDiscovery::scan(mr, cf);
        QCOMPARE(found.installations.size(), 2);
        QCOMPARE(found.installations.first().toMap().value("title").toString(), "Modrinth Fixture");
        QCOMPARE(found.installations.first().toMap().value("launcherDatabasePath").toString(),
                 mr + "/app.db");
        QCOMPARE(found.installations.last().toMap().value("provider").toString(), "curseforge");
        QCOMPARE(found.installations.last().toMap().value("modCount").toInt(), 1);
        QFile after(cfProfile + "/minecraftinstance.json");
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), metadata);
    }
    void discoveryReadsAppliedContentSetWithoutWrites_data() {
        QTest::addColumn<bool>("externalProfile");
        QTest::newRow("default-profile-root") << false;
        QTest::newRow("external-profile-root") << true;
    }
    void discoveryReadsAppliedContentSetWithoutWrites() {
        QFETCH(bool, externalProfile);
        QTemporaryDir temp;
        const auto mr = temp.path() + "/ModrinthApp";
        const auto profile = externalProfile ? temp.path() + "/ExternalInstance" : mr + "/profiles/A";
        QVERIFY(QDir().mkpath(mr));
        QVERIFY(QDir().mkpath(profile));
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "current-schema");
            db.setDatabaseName(mr + "/app.db");
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE instances(name TEXT,path TEXT,icon_path TEXT,applied_content_set_id "
                           "TEXT,id TEXT)"));
            QVERIFY(q.exec("CREATE TABLE instance_content_sets(id TEXT,game_version TEXT,loader "
                           "TEXT,loader_version TEXT)"));
            q.prepare("INSERT INTO instances VALUES('Current schema',?,'','set','local:verified-instance')");
            q.addBindValue(externalProfile ? profile : QString("A"));
            QVERIFY(q.exec());
            QVERIFY(q.exec("INSERT INTO instance_content_sets VALUES('set','1.21.1','neoforge','21.1.248')"));
        }
        QSqlDatabase::removeDatabase("current-schema");
        QFile before(mr + "/app.db");
        QVERIFY(before.open(QIODevice::ReadOnly));
        const auto bytes = before.readAll();
        before.close();
        const auto result = InstallationDiscovery::scan(mr, temp.path() + "/empty");
        QCOMPARE(result.installations.size(), 1);
        QCOMPARE(result.installations.first().toMap().value("launcherInstanceId").toString(),
                 QString("local:verified-instance"));
        const auto row = result.installations.first().toMap();
        QCOMPARE(row.value("path").toString(), profile);
        QCOMPARE(row.value("launcherDatabasePath").toString(), mr + "/app.db");
        QCOMPARE(row.value("gameVersion").toString(), QString("1.21.1"));
        QCOMPARE(row.value("loader").toString(), QString("neoforge"));
        QCOMPARE(row.value("loaderVersion").toString(), QString("21.1.248"));
        QVERIFY(before.open(QIODevice::ReadOnly));
        QCOMPARE(before.readAll(), bytes);
    }
    void liveCurseforgeCatalogueAndReleases() {
        if (!qEnvironmentVariableIsSet("MODPACK_STUDIO_LIVE_TEST") ||
            qEnvironmentVariableIsEmpty("CURSEFORGE_API_KEY"))
            QSKIP("Live CurseForge verification requires explicit opt-in and a session API key.");
        CatalogueClient client;
        client.setCurseforgeKey(qgetenv("CURSEFORGE_API_KEY"));
        bool fetched = false;
        CataloguePage page;
        CatalogueQuery query;
        query.provider = "curseforge";
        query.type = "mod";
        query.versions = {"1.21.1"};
        query.loaders = {"neoforge"};
        query.sort = "downloads";
        client.fetch(query, 0, 3, [&](CataloguePage result) {
            page = result;
            fetched = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(fetched, 25000);
        QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
        QVERIFY(!page.projects.isEmpty());
        QVERIFY(page.projects.size() <= 3);
        auto project = page.projects.first();
        QCOMPARE(project.provider, QString("curseforge"));
        QCOMPARE(project.sourceIds.value("curseforge"), project.id);
        QCOMPARE(project.sourceDownloads.value("curseforge"), project.downloads);
        QVERIFY(!project.title.isEmpty());
        QVERIFY(!project.authors.isEmpty());
        project.sourceIds = {{"curseforge", project.id}};

        bool loaded = false;
        ProjectDetail detail;
        connect(&client, &CatalogueClient::detailFinished, this,
                [&](int generation, const ProjectDetail &result) {
                    QCOMPARE(generation, 1);
                    detail = result;
                    loaded = true;
                });
        client.loadProject(project, "1.21.1", "neoforge", 1);
        QTRY_VERIFY_WITH_TIMEOUT(loaded, 25000);
        QVERIFY2(detail.errors.isEmpty(), qPrintable(detail.errors.join(';')));
        QVERIFY(!detail.description.isEmpty());
        QVERIFY(!detail.releases.isEmpty());
        for (const auto &release : detail.releases) {
            QCOMPARE(release.provider, QString("curseforge"));
            QCOMPARE(release.projectId, project.id);
            QVERIFY(release.gameVersions.contains("1.21.1"));
            QVERIFY(release.loaders.contains("neoforge"));
            QCOMPARE(release.sha1.size(), 40);
            QVERIFY(!release.filename.isEmpty());
            QVERIFY(!release.published.isEmpty());
        }
        bool changelogLoaded = false;
        QString changelogError;
        client.loadChangelog(detail.releases.first(), [&](QString, QString error) {
            changelogError = error;
            changelogLoaded = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(changelogLoaded, 25000);
        QVERIFY2(changelogError.isEmpty(), qPrintable(changelogError));

        bool facetsLoaded = false;
        QVariantMap facets;
        QString facetsError;
        client.facets([&](QVariantMap result, QString error) {
            facets = result;
            facetsError = error;
            facetsLoaded = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(facetsLoaded, 25000);
        QVERIFY2(facetsError.isEmpty(), qPrintable(facetsError));
        QVERIFY(!facets.value("curseforgeTags").toList().isEmpty());
        QVERIFY(facets.value("versions").toStringList().contains("1.21.1"));
    }
    void matchingFilesLinkProviderProjectsWithoutMatchingTheirNames() {
        QCOMPARE(verifiedProjectPairs({{"cf", {"hash-a", "hash-b"}}}, {{"hash-a", "mr"}, {"hash-b", "mr"}}),
                 (QMap<QString, QString>{{"cf", "mr"}}));
        QVERIFY(verifiedProjectPairs({{"cf", {"a", "b"}}}, {{"a", "first"}, {"b", "second"}}).isEmpty());
        QVERIFY(verifiedProjectPairs({{"first-cf", {"a"}}, {"second-cf", {"a"}}}, {{"a", "mr"}}).isEmpty());
        QVERIFY(verifiedProjectPairs({{"cf", {"unknown"}}}, {}).isEmpty());
        auto cf = project("curseforge", "11"), mr = project("modrinth", "abc");
        cf.sourceIds = {{"curseforge", "11"}, {"modrinth", "abc"}};
        cf.downloads = 200;
        mr.downloads = 100;
        cf.title = "A different provider title";
        for (const auto &rows : {QVector<Project>{cf, mr}, QVector<Project>{mr, cf}}) {
            const auto merged = mergeProjects(rows);
            QCOMPARE(merged.size(), 1);
            QCOMPARE(merged.first().provider, QString("modrinth"));
            QCOMPARE(merged.first().sourceIds.value("curseforge"), QString("11"));
            QCOMPARE(merged.first().sourceIds.value("modrinth"), QString("abc"));
            QCOMPARE(merged.first().totalDownloads(), 300);
        }
        cf.sourceIds.remove("modrinth");
        cf.title = mr.title;
        QCOMPARE(mergeProjects({cf, mr}).size(), 2);
    }
    void liveModrinthSearchAndReleases() {
        if (!qEnvironmentVariableIsSet("MODPACK_STUDIO_LIVE_TEST"))
            QSKIP("Set MODPACK_STUDIO_LIVE_TEST=1 for live provider verification.");
        CatalogueClient client;
        bool searched = false, loaded = false;
        QVector<Project> projects;
        ProjectDetail detail;
        connect(&client, &CatalogueClient::searchFinished, this,
                [&](int, const QVector<Project> &rows, const QStringList &errors) {
                    searched = true;
                    projects = rows;
                    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(';')));
                });
        client.search("sodium", "modrinth", "1.21.1", "fabric", 1);
        QTRY_VERIFY_WITH_TIMEOUT(searched, 25000);
        QVERIFY(!projects.isEmpty());
        auto project = projects.first();
        project.sourceIds.insert("modrinth", project.id);
        connect(&client, &CatalogueClient::detailFinished, this, [&](int, const ProjectDetail &result) {
            detail = result;
            loaded = true;
        });
        client.loadProject(project, "1.21.1", "fabric", 2);
        QTRY_VERIFY_WITH_TIMEOUT(loaded, 25000);
        QVERIFY2(detail.errors.isEmpty(), qPrintable(detail.errors.join(';')));
        QVERIFY(!detail.releases.isEmpty());
        QVERIFY(!detail.releases.first().sha1.isEmpty());
        QVERIFY(detail.releases.first().loaders.contains("fabric"));
        QVERIFY(detail.releases.first().gameVersions.contains("1.21.1"));
    }
};
QTEST_GUILESS_MAIN(FoundationTests)
#include "FoundationTests.moc"
