#include "ContentFixtures.h"
#include "TestPerformance.h"
#include "features/content/ContentActions.h"
#include "features/content/InstalledContentVersions.h"
#include "features/preferences/ContentPreferences.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>
using namespace studio;
namespace {
class VersionsCatalogue final : public IContentCatalogue {
  public:
    struct Request {
        QVariantMap row;
        QString game, loaders;
        VersionsCallback callback;
    };
    QList<Request> requests;
    int cancellations = 0;
    bool holdMetadata = false;
    IndexCallback metadataCompletion;
    std::function<void()> onCancel;
    void identify(const QVariantMap &, const QVariantList &rows, IndexCallback callback,
                  IndexProgress = {}) override {
        if (holdMetadata)
            metadataCompletion = std::move(callback);
        else
            callback(rows, {});
    }
    void refresh(const QVariantList &rows, const QString &, const QString &,
                 IndexCallback callback) override {
        callback(rows, {});
    }
    void versions(const QVariantMap &row, const QString &game, const QString &loaders,
                  VersionsCallback callback) override {
        requests << Request{row, game, loaders, std::move(callback)};
    }
    void cancelVersions() override {
        ++cancellations;
        const auto action = onCancel;
        if (action)
            action();
    }
    void setCurseforgeKey(const QByteArray &) override {}
    void finish(qsizetype index, QVector<Release> releases, QString error = {}) {
        const auto callback = requests.at(index).callback;
        callback(std::move(releases), std::move(error));
    }
};
// This fixture implements only the read contract needed by the version model.
class VersionSource final : public IContentVersionSource {
  public:
    using Request = VersionsCatalogue::Request;
    QList<Request> requests;
    std::function<void()> onCancel;
    std::function<void(Request)> onRequest;
    void versions(const QVariantMap &row, const QString &game, const QString &loaders,
                  VersionsCallback callback) override {
        Request request{row, game, loaders, std::move(callback)};
        requests << request;
        const auto action = onRequest;
        if (action)
            action(std::move(request));
    }
    void cancelVersions() override {
        const auto action = onCancel;
        if (action)
            action();
    }
    void finish(qsizetype index, QVector<Release> releases, QString error = {}) {
        const auto callback = requests.at(index).callback;
        callback(std::move(releases), std::move(error));
    }
};
QVariantMap file(const QString &id) {
    return {{"title", id},
            {"relativePath", "mods/" + id + ".jar"},
            {"category", "mods"},
            {"enabled", true},
            {"sha1", QString(40, 'a')},
            {"sourceIds", QVariantMap{{"modrinth", id}}}};
}
Release release(const QString &project, const QString &id, const QString &version = "2") {
    Release result;
    result.provider = "modrinth";
    result.projectId = project;
    result.id = id;
    result.version = version;
    result.sha1 = QString(40, 'b');
    result.filename = project + ".jar";
    result.url = "https://example.invalid/" + result.filename;
    result.gameVersions = {"1.21.1"};
    result.loaders = {"neoforge"};
    return result;
}
struct Fixture {
    QTemporaryDir temp;
    std::shared_ptr<VersionsCatalogue> source = std::make_shared<VersionsCatalogue>();
    std::shared_ptr<ContentInstallerFixture> installer = std::make_shared<ContentInstallerFixture>();
    ContentActions content{std::make_shared<ContentFileFixture>(), source, installer,
                           std::make_shared<SettingsContentPreferences>(temp.filePath("prefs.ini"))};
    Fixture() {
        content.setContext({{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "neoforge"}},
                           {file("A"), file("B")});
    }
};
} // namespace
class ContentVersionTests final : public QObject {
    Q_OBJECT
  private slots:
    void narrowVersionSourcePublishesCoherentStateAndSettlesOnce() {
        auto source = std::make_shared<VersionSource>();
        InstalledContentVersions model(source);
        QVERIFY(model.target().isEmpty());
        QVERIFY(model.versions().isEmpty());
        QVERIFY(model.groups().isEmpty());
        QVERIFY(model.error().isEmpty());
        QVERIFY(!model.busy());
        QVariantList observed;
        connect(&model, &InstalledContentVersions::changed, this, [&] {
            observed << QVariantMap{{"busy", model.busy()},
                                    {"target", model.target()},
                                    {"rows", model.versions()},
                                    {"groups", model.groups()}};
        });
        model.open(file("A"), "1.21.1", "neoforge|fabric");
        QCOMPARE(source->requests.size(), 1);
        QCOMPARE(source->requests.first().game, QString("1.21.1"));
        QCOMPARE(source->requests.first().loaders, QString("neoforge|fabric"));
        QVERIFY(model.busy());
        source->finish(0, {release("A", "chosen")});
        const auto groups = model.groups();
        source->finish(0, {}, "late duplicate");
        QVERIFY(!model.busy());
        QVERIFY(model.error().isEmpty());
        QCOMPARE(model.versions().first().toMap().value("id").toString(), QString("chosen"));
        QCOMPARE(model.groups(), groups);
        QCOMPARE(observed.size(), 2);
        QVERIFY(observed.first().toMap().value("busy").toBool());
        QVERIFY(observed.first().toMap().value("rows").toList().isEmpty());
        QVERIFY(!observed.last().toMap().value("busy").toBool());
        QCOMPARE(observed.last().toMap().value("groups").toList(), groups);
    }
    void synchronousCancellationRepliesCannotReplaceTheNewTarget() {
        auto source = std::make_shared<VersionSource>();
        InstalledContentVersions model(source);
        model.open(file("A"), "old-game", "old-loader");
        source->onCancel = [&] { source->finish(0, {release("A", "cancelled")}, "cancelled"); };
        model.open(file("B"), "new-game", "new-loader");
        QVERIFY(model.busy());
        QVERIFY(model.versions().isEmpty());
        QVERIFY(model.error().isEmpty());
        QCOMPARE(model.target().value("title").toString(), QString("B"));
        QCOMPARE(source->requests.size(), 2);
        source->finish(1, {release("B", "new")});
        QCOMPARE(model.versions().first().toMap().value("id").toString(), QString("new"));
        model.reset();
        QVERIFY(model.target().isEmpty());
        QVERIFY(model.groups().isEmpty());
        QVERIFY(!model.busy());
        QVERIFY(model.error().isEmpty());
        source->finish(1, {release("B", "too-late")});
        QVERIFY(model.versions().isEmpty());
    }
    void reentrantCancellationKeepsOnlyTheNewerOpen() {
        auto source = std::make_shared<VersionSource>();
        InstalledContentVersions model(source);
        bool redirected = false;
        source->onCancel = [&] {
            if (!redirected) {
                redirected = true;
                model.open(file("B"), "B-game", "B-loader");
            }
        };
        model.open(file("A"), "A-game", "A-loader");
        QCOMPARE(source->requests.size(), 1);
        QCOMPARE(source->requests.first().row.value("title").toString(), QString("B"));
        QCOMPARE(source->requests.first().game, QString("B-game"));
        source->finish(0, {release("B", "current")});
        QCOMPARE(model.versions().first().toMap().value("id").toString(), QString("current"));
    }
    void destroyingDuringCancellationNeverDispatchesTheNewLookup() {
        auto source = std::make_shared<VersionSource>();
        QPointer<InstalledContentVersions> model = new InstalledContentVersions(source);
        model->open(file("A"), "1.21.1", "neoforge");
        source->onCancel = [&] { delete model.data(); };
        model->open(file("B"), "1.21.1", "neoforge");
        QVERIFY(model.isNull());
        QCOMPARE(source->requests.size(), 1);
        source->finish(0, {release("A", "late")});
    }
    void destroyingDuringPublicationNeverDispatchesWork() {
        auto source = std::make_shared<VersionSource>();
        QPointer<InstalledContentVersions> model = new InstalledContentVersions(source);
        connect(model, &InstalledContentVersions::changed, this, [&] { delete model.data(); });
        model->open(file("A"), "1.21.1", "neoforge");
        QVERIFY(model.isNull());
        QVERIFY(source->requests.isEmpty());
    }
    void ownerDestructionAndLateRepliesAreIndependent() {
        auto source = std::make_shared<VersionSource>();
        auto model = std::make_unique<InstalledContentVersions>(source);
        model->open(file("A"), "1.21.1", "neoforge");
        model.reset();
        source->finish(0, {release("A", "late")});
        source->finish(0, {}, "late failure");
    }
    void resetDuringPublicationInvalidatesDispatch() {
        auto source = std::make_shared<VersionSource>();
        InstalledContentVersions model(source);
        connect(&model, &InstalledContentVersions::changed, this, [&] { model.reset(); });
        model.open(file("A"), "1.21.1", "neoforge");
        QVERIFY(source->requests.isEmpty());
        QVERIFY(model.target().isEmpty());
        QVERIFY(!model.busy());
    }
    void readExceptionsRemainScopedAndCanBeRetried_data() {
        QTest::addColumn<bool>("standard");
        QTest::newRow("standard") << true;
        QTest::newRow("unknown") << false;
    }
    void readExceptionsRemainScopedAndCanBeRetried() {
        QFETCH(bool, standard);
        auto source = std::make_shared<VersionSource>();
        InstalledContentVersions model(source);
        source->onRequest = [standard](auto) {
            if (standard)
                throw std::runtime_error("fixture failure");
            throw 7;
        };
        model.open(file("A"), "1.21.1", "neoforge");
        QVERIFY(!model.busy());
        QVERIFY(!model.error().isEmpty());
        QVERIFY(model.groups().isEmpty());
        source->onRequest = {};
        model.open(file("A"), "1.21.1", "neoforge");
        QVERIFY(model.error().isEmpty());
        source->finish(1, {release("A", "retry")});
        QVERIFY(!model.busy());
        QVERIFY(model.error().isEmpty());
        QCOMPARE(model.versions().first().toMap().value("id").toString(), QString("retry"));
    }
    void cancellationFailureDoesNotBlockTheChosenLookup() {
        auto source = std::make_shared<VersionSource>();
        source->onCancel = [] { throw std::runtime_error("cannot abort"); };
        InstalledContentVersions model(source);
        model.open(file("A"), "1.21.1", "neoforge");
        QCOMPARE(source->requests.size(), 1);
        source->finish(0, {release("A", "chosen")});
        QVERIFY(model.error().isEmpty());
        QVERIFY(!model.busy());
    }
    void aPortThrowAfterSuccessfulDeliveryDoesNotReplaceSuccess() {
        auto source = std::make_shared<VersionSource>();
        source->onRequest = [](auto request) {
            request.callback({release("A", "chosen")}, {});
            throw std::runtime_error("after delivery");
        };
        InstalledContentVersions model(source);
        model.open(file("A"), "1.21.1", "neoforge");
        QVERIFY(model.error().isEmpty());
        QVERIFY(!model.busy());
        QCOMPARE(model.versions().first().toMap().value("id").toString(), QString("chosen"));
    }
    void versionGroupReadsReuseThePublishedSnapshot() {
        auto source = std::make_shared<VersionSource>();
        InstalledContentVersions model(source);
        model.open(file("A"), "1.21.1", "neoforge");
        QVector<Release> releases;
        for (int i = 0; i < 4000; ++i)
            releases << release("A", QString::number(i), QString::number(i / 2));
        source->finish(0, releases);
        const auto groups = model.groups();
        QCOMPARE(groups.size(), 2000);
        QElapsedTimer timer;
        timer.start();
        qsizetype count = 0;
        for (int i = 0; i < 10000; ++i)
            count += model.groups().size();
        QCOMPARE(count, qsizetype(20000000));
        QVERIFY(model.groups().isSharedWith(groups));
        qInfo() << "10000 version group reads over4000 releases ms" << timer.elapsed();
        QVERIFY(test_performance::releaseBudgetMet(timer.elapsed(), 100));
        model.open(file("B"), "1.21.1", "neoforge");
        QVERIFY(model.groups().isEmpty());
        source->finish(1, {release("B", "next")});
        QCOMPARE(model.groups().size(), 1);
    }
    void emptyTargetClearsBusyStateAndInvalidatesTheRequest() {
        Fixture fixture;
        auto &content = fixture.content;
        QTRY_VERIFY(!content.busy());
        content.openVersions("mods/A.jar");
        QVERIFY(content.versionsBusy());
        QCOMPARE(fixture.source->requests.size(), 1);
        content.openVersions("mods/missing.jar");
        QVERIFY(!content.versionsBusy());
        QVERIFY(content.versionTarget().isEmpty());
        fixture.source->finish(0, {release("A", "old-response")});
        QVERIFY(content.versions().isEmpty());
        QVERIFY(content.versionGroups().isEmpty());
    }
    void staleReplyCannotRepopulateAnEmptyTarget() {
        Fixture fixture;
        auto &content = fixture.content;
        QTRY_VERIFY(!content.busy());
        content.openVersions("mods/A.jar");
        content.openVersions("missing");
        fixture.source->finish(0, {release("A", "stale")});
        QVERIFY(content.versions().isEmpty());
        QVERIFY(content.versionTarget().isEmpty());
        QVERIFY(!content.versionsBusy());
    }
    void reentrantOpenOnlyDispatchesTheLatestTarget() {
        Fixture fixture;
        auto &content = fixture.content;
        QTRY_VERIFY(!content.busy());
        bool redirected = false;
        connect(&content, &ContentActions::changed, this, [&] {
            if (!redirected && content.versionsBusy() && content.versionTarget().value("title") == "A") {
                redirected = true;
                content.openVersions("mods/B.jar");
            }
        });
        content.openVersions("mods/A.jar");
        QVERIFY(redirected);
        QCOMPARE(fixture.source->requests.size(), 1);
        QCOMPARE(fixture.source->requests.first().row.value("title").toString(), QString("B"));
        QCOMPARE(fixture.source->requests.first().game, QString("1.21.1"));
        QCOMPARE(fixture.source->requests.first().loaders, QString("neoforge"));
        fixture.source->finish(0, {release("B", "latest")});
        QVERIFY(!content.versionsBusy());
        QCOMPARE(content.versions().first().toMap().value("id").toString(), QString("latest"));
    }
    void changingInstancesClearsVersionSelectionAndPendingState() {
        Fixture fixture;
        auto &content = fixture.content;
        QTRY_VERIFY(!content.busy());
        content.openVersions("mods/A.jar");
        content.setContext(
            {{"path", fixture.temp.filePath("other")}, {"minecraft", "1.20.1"}, {"loader", "fabric"}},
            {file("C")});
        QVERIFY(!content.versionsBusy());
        QVERIFY(content.versionTarget().isEmpty());
        fixture.source->finish(0, {release("A", "stale")});
        QVERIFY(content.versions().isEmpty());
        content.installVersion(0);
        QVERIFY(fixture.installer->requested.isEmpty());
        QTRY_VERIFY(!content.busy());
    }
    void changedCompatibilityInvalidatesVersions_data() {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("value");
        QTest::newRow("game") << QString("minecraft") << QString("1.20.1");
        QTest::newRow("loader") << QString("loader") << QString("fabric");
    }
    void changedCompatibilityInvalidatesVersions() {
        QFETCH(QString, key);
        QFETCH(QString, value);
        Fixture fixture;
        auto &content = fixture.content;
        QTRY_VERIFY(!content.busy());
        content.openVersions("mods/A.jar");
        QVariantMap pack{{"path", fixture.temp.path()}, {"minecraft", "1.21.1"}, {"loader", "neoforge"}};
        pack[key] = value;
        content.setContext(pack, {file("A"), file("B")});
        QVERIFY(!content.versionsBusy());
        QVERIFY(content.versionTarget().isEmpty());
        fixture.source->finish(0, {release("A", "wrong-context")});
        QVERIFY(content.versions().isEmpty());
        content.installVersion(0);
        QVERIFY(fixture.installer->requested.isEmpty());
        QTRY_VERIFY(!content.busy());
    }
    void unrelatedPackMetadataDoesNotDiscardTheVersionRequest() {
        Fixture fixture;
        auto &content = fixture.content;
        QTRY_VERIFY(!content.busy());
        content.openVersions("mods/A.jar");
        content.setContext({{"path", fixture.temp.path()},
                            {"minecraft", "1.21.1"},
                            {"loader", "neoforge"},
                            {"title", "New title"}},
                           {file("A"), file("B")});
        QVERIFY(content.versionsBusy());
        QCOMPARE(content.versionTarget().value("title").toString(), QString("A"));
        fixture.source->finish(0, {release("A", "still-current")});
        QVERIFY(!content.versionsBusy());
        QCOMPARE(content.versions().first().toMap().value("id").toString(), QString("still-current"));
        QTRY_VERIFY(!content.busy());
    }
    void obsoleteMetadataDuringVersionCancellationCannotRestoreTheOldInstance() {
        Fixture fixture;
        auto &content = fixture.content;
        QTRY_VERIFY(!content.busy());
        fixture.source->holdMetadata = true;
        content.refresh(false);
        QTRY_VERIFY(bool(fixture.source->metadataCompletion));
        content.openVersions("mods/A.jar");
        fixture.source->holdMetadata = false;
        fixture.source->onCancel = [&] {
            auto complete = std::move(fixture.source->metadataCompletion);
            if (complete)
                complete({file("A"), file("B")}, {});
        };
        content.setContext(
            {{"path", fixture.temp.filePath("other")}, {"minecraft", "1.21.1"}, {"loader", "neoforge"}},
            {file("C")});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.rows().size(), 1);
        QCOMPARE(content.rows().first().toMap().value("title").toString(), QString("C"));
        QVERIFY(content.versionTarget().isEmpty());
    }
    void destroyingTheFacadeDuringVersionPublicationDropsTheLookup() {
        QTemporaryDir temp;
        auto source = std::make_shared<VersionsCatalogue>();
        QPointer<ContentActions> content = new ContentActions(
            std::make_shared<ContentFileFixture>(), source, std::make_shared<ContentInstallerFixture>(),
            std::make_shared<SettingsContentPreferences>(temp.filePath("prefs.ini")));
        content->setContext({{"path", temp.path()}, {"minecraft", "1.21.1"}, {"loader", "neoforge"}},
                            {file("A")});
        QTRY_VERIFY(!content->busy());
        connect(content, &ContentActions::changed, this, [&] {
            if (content->versionsBusy())
                delete content.data();
        });
        content->openVersions("mods/A.jar");
        QVERIFY(content.isNull());
        QVERIFY(source->requests.isEmpty());
    }
    void currentLocksAndFreezeStillBlockAChosenVersion() {
        Fixture fixture;
        auto &content = fixture.content;
        QTRY_VERIFY(!content.busy());
        content.openVersions("mods/A.jar");
        fixture.source->finish(0, {release("A", "chosen")});
        content.setLocked("mods/A.jar", true);
        content.installVersion(0);
        QVERIFY(fixture.installer->requested.isEmpty());
        content.setLocked("mods/A.jar", false);
        content.freeze("mods/A.jar", true);
        content.installVersion(0);
        QVERIFY(fixture.installer->requested.isEmpty());
        content.freeze("mods/A.jar", false);
        content.installVersion(-1);
        content.installVersion(1);
        QVERIFY(fixture.installer->requested.isEmpty());
        content.installVersion(0);
        QTRY_COMPARE(fixture.installer->requested.size(), 1);
        QCOMPARE(fixture.installer->requested.first().release.id, QString("chosen"));
        fixture.installer->cancel();
        QTRY_VERIFY(!content.busy());
    }
    void outOfOrderRepliesKeepLatestVersionsAndOriginalGroupIndices() {
        Fixture fixture;
        auto &content = fixture.content;
        QTRY_VERIFY(!content.busy());
        content.openVersions("mods/A.jar");
        content.openVersions("mods/B.jar");
        const auto latest = QVector<Release>{release("B", "second", "2"), release("B", "first", "1"),
                                             release("B", "second-variant", "2")};
        fixture.source->finish(1, latest);
        fixture.source->finish(0, {release("A", "obsolete")});
        QVERIFY(!content.versionsBusy());
        QCOMPARE(content.versionTarget().value("title").toString(), QString("B"));
        QCOMPARE(content.versions().size(), 3);
        const auto groups = content.versionGroups();
        QCOMPARE(groups.size(), 2);
        const auto variants = groups.first().toMap().value("variants").toList();
        QCOMPARE(variants.size(), 2);
        QCOMPARE(variants[0].toMap().value("releaseIndex").toInt(), 0);
        QCOMPARE(variants[1].toMap().value("releaseIndex").toInt(), 2);
        QCOMPARE(content.versionGroups(), groups);
    }
};
QTEST_GUILESS_MAIN(ContentVersionTests)
#include "ContentVersionTests.moc"
