#include "ContentFixtures.h"
#include "DependencyFixtures.h"
#include "TestPerformance.h"
#include "features/content/ContentActions.h"
#include "features/content/ContentOperationState.h"
#include "features/content/OptionalDependencies.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/BrowseSettingsStore.h"
#include "platform/OptionalDependencySource.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QPointer>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>
using namespace studio;
namespace {
QVariantMap pack(const QString &path = "instance-a") {
    return {{"path", path}, {"name", path}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
}
QVariantMap reference(const QString &id = "optional", const QString &provider = "modrinth") {
    return {{"provider", provider}, {"projectId", id}};
}
ContentInstallRequest request(const QString &id = "optional", const QString &provider = "modrinth") {
    Release release;
    release.provider = provider;
    release.projectId = id;
    release.id = id + "-v1";
    release.title = id;
    release.version = "1.0";
    release.filename = id + ".jar";
    release.url = "https://example.test/file.jar";
    release.sha1 =
        QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha1).toHex());
    release.gameVersions = {"1.21.1"};
    release.loaders = {"fabric"};
    return {release, "Project " + id, {{provider, id}}, "mod"};
}
class Source final : public IOptionalDependencySource {
  public:
    struct Call {
        QVariantMap pack;
        QVariantList files;
        ContentDependency reference;
        QStringList channels;
        Callback callback;
    };
    QList<Call> calls;
    bool held = false, throwing = false, cancelThrows = false;
    int cancellations = 0;
    QString error;
    QVariantMap projectExtras;
    std::function<void(ContentInstallRequest &)> transform;
    void resolve(const QVariantMap &pack, const QVariantList &files, const ContentDependency &ref,
                 const QStringList &channels, Callback callback) override {
        calls << Call{pack, files, ref, channels, callback};
        if (throwing)
            throw std::runtime_error("Lookup fixture exception");
        if (!held)
            complete(calls.size() - 1);
    }
    void complete(qsizetype index) {
        const auto call = calls.at(index);
        auto value = request(call.reference.projectId, call.reference.provider);
        if (!call.reference.versionId.isEmpty())
            value.release.id = call.reference.versionId;
        if (transform)
            transform(value);
        auto project = projectExtras;
        project["title"] = value.title;
        call.callback(value, project, error);
    }
    void cancel() override {
        ++cancellations;
        if (cancelThrows)
            throw std::runtime_error("Cancel fixture exception");
    }
    void setCurseforgeKey(const QByteArray &) override {}
};
struct Harness {
    QTemporaryDir temp;
    std::shared_ptr<BrowseSettingsStore> store =
        std::make_shared<BrowseSettingsStore>(temp.path() + "/settings.ini");
    std::shared_ptr<InstallationPreferences> preferences = std::make_shared<InstallationPreferences>(store);
    std::shared_ptr<PackReleasePolicy> policy = std::make_shared<PackReleasePolicy>(store);
    std::shared_ptr<Source> source = std::make_shared<Source>();
    OptionalDependencies model{source, preferences, policy};
};
QString key(const OptionalDependencies &model, qsizetype index = 0) {
    return model.rows().at(index).toMap().value("key").toString();
}
QString state(const OptionalDependencies &model, qsizetype index = 0) {
    return model.rows().at(index).toMap().value("state").toString();
}
} // namespace
class OptionalDependencyTests final : public QObject {
    Q_OBJECT
  private slots:
    void explicitRequiredReviewIgnoresOnlyAutomaticPromptPreference() {
        Harness h;
        h.preferences->setPromptOptionalDependencies(false);
        h.model.offer(pack(), {}, {reference("automatic")});
        QVERIFY(!h.model.visible());
        h.model.review(pack(), {}, {reference("required")});
        QTRY_COMPARE(state(h.model), "ready");
        QVERIFY(h.model.requiredReview());
        QCOMPARE(h.source->calls.size(), 1);
        int installed = 0;
        h.model.setInstaller([&](auto, auto project) {
            ++installed;
            return project.value("targetPack").toMap() == pack();
        });
        QCOMPARE(installed, 0);
        h.preferences->setPromptOptionalDependencies(true);
        h.model.offer(pack("automatic-b"), {}, {reference("automatic-b")});
        h.model.review(pack("required-b"), {}, {reference("required-b")});
        h.preferences->setPromptOptionalDependencies(false);
        QVERIFY(h.model.visible());
        QCOMPARE(state(h.model), "ready");
        h.model.install(key(h.model));
        QCOMPARE(installed, 1);
        h.model.dismiss();
        QTRY_COMPARE(h.model.pack().value("path").toString(), QString("required-b"));
        QTRY_COMPARE(state(h.model), "ready");
        QVERIFY(h.model.requiredReview());
        QCOMPARE(h.source->calls.size(), 2);
        h.model.dismiss();
        QVERIFY(!h.model.visible());
        QVERIFY(!h.model.requiredReview());
    }
    void completedRootOffersOptionalContentThroughTheSamePerItemQueue() {
        class Files final : public IContentFileStore {
          public:
            QVariantList rows;
            QVariantList snapshot(const QVariantMap &, const QVariantList &) const override {
                return rows;
            }
            ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const override {
                return {};
            }
            ContentWriteResult remove(const QVariantMap &, const QVariantList &) const override {
                return {};
            }
            ContentWriteResult importFiles(const QVariantMap &, const QList<QUrl> &,
                                           const QString &) const override {
                return {};
            }
        };
        class Leaf final : public IContentInstaller {
          public:
            std::shared_ptr<Files> files;
            QStringList installed;
            explicit Leaf(std::shared_ptr<Files> store) : files(std::move(store)) {}
            void install(const QVariantMap &pack, const QVariantList &,
                         const QVector<ContentInstallRequest> &requests, Callback done) override {
                for (const auto &request : requests) {
                    installed << request.release.projectId;
                    files->rows << contentInstallPrediction(request, pack, {});
                }
                done({true, {}});
            }
            void cancel() override {}
            bool cancellable() const override {
                return false;
            }
        };
        class Required final : public IContentDependencySource {
          public:
            QStringList calls;
            void resolve(const QVariantMap &, const QVariantList &, const ContentDependency &ref,
                         const QStringList &, Callback done) override {
                calls << ref.projectId;
                done(request(ref.projectId, ref.provider), {});
            }
            void cancel() override {}
        };
        Harness h;
        auto files = std::make_shared<Files>();
        auto leaf = std::make_shared<Leaf>(files);
        auto required = std::make_shared<Required>();
        auto coordinator = std::make_shared<ContentDependencyInstaller>(leaf, required, files);
        ContentActions actions(files, std::make_shared<ContentCatalogueFixture>(), coordinator,
                               std::make_shared<SettingsContentPreferences>(h.temp.path() + "/content.ini"));
        actions.setReleasePolicy(h.policy);
        connect(coordinator.get(), &ContentDependencyInstaller::optionalDependencies, &h.model,
                &OptionalDependencies::offer);
        connect(&actions, &ContentActions::installationFinished, &h.model,
                &OptionalDependencies::installationFinished);
        h.model.setInstaller(
            [&](auto release, auto project) { return actions.installRelease(release, project); });
        actions.setContext(pack(), {});
        auto root = request("root");
        root.release.dependencyRefs = {
            QJsonObject{{"dependency_type", "required"}, {"project_id", "required"}},
            QJsonObject{{"dependency_type", "optional"}, {"project_id", "optional"}}};
        QVERIFY(actions.installRelease(
            root.release.toVariant(),
            {{"title", root.title}, {"type", "mod"}, {"sourceIds", root.sourceIds.toVariantMap()}}));
        QTRY_VERIFY(!actions.busy());
        QTRY_COMPARE(state(h.model), "ready");
        QCOMPARE(leaf->installed, QStringList({"root", "required"}));
        QCOMPARE(required->calls, QStringList{"required"});
        QCOMPARE(files->rows.size(), 2);
        h.model.install(key(h.model));
        QTRY_VERIFY(!actions.busy());
        QTRY_COMPARE(state(h.model), "installed");
        QCOMPARE(leaf->installed, QStringList({"root", "required", "optional"}));
        QCOMPARE(files->rows.size(), 3);
        QCOMPARE(actions.operations()->failedCount(), 0);
    }
    void providerAdapterLoadsOnlyOptionalMetadata_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<bool>("metadataFails");
        QTest::newRow("modrinth") << QString("modrinth") << false;
        QTest::newRow("modrinth-headline-failure-keeps-download-choice") << QString("modrinth") << true;
        QTest::newRow("curseforge") << QString("curseforge") << false;
    }
    void providerAdapterLoadsOnlyOptionalMetadata() {
        QFETCH(QString, provider);
        QFETCH(bool, metadataFails);
        dependency_test::Server server;
        const auto id = provider == "modrinth" ? QString("optional") : QString("11");
        if (provider == "modrinth") {
            server.modrinth(id, "v1", "fixture bytes");
            server.json(
                "/project/optional",
                QJsonObject{{"id", id}, {"title", "Friendly optional project"}, {"project_type", "mod"}});
            if (metadataFails)
                server.statuses["/project/optional"] = 503;
        } else {
            server.curseforge(11, 101, "fixture bytes");
            server.json("/mods/11", QJsonObject{{"data", QJsonObject{{"id", 11},
                                                                     {"gameId", 432},
                                                                     {"name", "Friendly optional project"},
                                                                     {"allowModDistribution", true}}}});
        }
        OptionalDependencySource source(nullptr, server.base(), server.base());
        source.setCurseforgeKey("fixture-key");
        bool done = false;
        source.resolve(pack(), {}, {provider, id, {}}, {"release"},
                       [&](ContentInstallRequest request, QVariantMap info, QString error) {
                           QVERIFY2(error.isEmpty(), qPrintable(error));
                           QCOMPARE(request.release.provider, provider);
                           QCOMPARE(request.release.projectId, id);
                           QVERIFY(!request.release.sha1.isEmpty());
                           if (!metadataFails)
                               QCOMPARE(request.title, "Friendly optional project");
                           else
                               QVERIFY(!info.value("warning").toString().isEmpty());
                           done = true;
                       });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        for (const auto &path : server.requests)
            QVERIFY(!path.startsWith("/files/"));
        if (provider == "modrinth")
            QVERIFY(!server.requestHeaders.value("/project/optional").toLower().contains("x-api-key"));
        else
            QVERIFY(server.requestHeaders.value("/mods/11").toLower().contains("x-api-key: fixture-key"));
    }
    void preferenceDefaultsOnAndPersistsAcrossReaders() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/prefs.ini";
        {
            auto store = std::make_shared<BrowseSettingsStore>(path);
            store->save("unrelated", "preserved");
            InstallationPreferences preferences(store);
            QVERIFY(preferences.promptOptionalDependencies());
            QSignalSpy changed(&preferences, &InstallationPreferences::changed);
            preferences.setPromptOptionalDependencies(false);
            preferences.setPromptOptionalDependencies(false);
            QCOMPARE(changed.size(), 1);
        }
        auto store = std::make_shared<BrowseSettingsStore>(path);
        InstallationPreferences reopened(store);
        QVERIFY(!reopened.promptOptionalDependencies());
        QCOMPARE(store->value("unrelated").toString(), "preserved");
    }
    void onlyExplicitInstallActionsEnterTheCapturedInstanceQueue() {
        Harness h;
        h.source->projectExtras["sourceIds"] = QVariantMap{{"curseforge", "unrelated-display-metadata"}};
        int installs = 0;
        QVariantMap target, selected;
        h.model.setInstaller([&](auto release, auto project) {
            selected = release;
            target = project;
            ++installs;
            return true;
        });
        h.model.offer(pack(), {}, {reference()});
        QTRY_COMPARE(state(h.model), "ready");
        QCOMPARE(installs, 0);
        h.model.install(key(h.model));
        h.model.install(key(h.model));
        QCOMPARE(installs, 1);
        QCOMPARE(selected.value("id").toString(), "optional-v1");
        QCOMPARE(state(h.model), "queued");
        QCOMPARE(target.value("targetPack").toMap(), pack());
        QCOMPARE(target.value("sourceIds").toMap().value("modrinth").toString(), "optional");
        QVERIFY(!target.value("sourceIds").toMap().contains("curseforge"));
        const auto exact = request().release.toVariant();
        h.model.installationFinished("another-instance", {exact}, {}, "unrelated");
        QCOMPARE(state(h.model), "queued");
        h.model.installationFinished("instance-a", {request("other").release.toVariant()}, {}, "unrelated");
        QCOMPARE(state(h.model), "queued");
        h.model.installationFinished(
            "instance-a", {exact}, {QVariantMap{{"category", "mods"}, {"sha1", request().release.sha1}}}, {});
        QCOMPARE(state(h.model), "installed");
    }
    void disabledInstalledContentAndInvalidReferencesAreNotOffered() {
        Harness h;
        const QVariantList files{QVariantMap{
            {"category", "mods"}, {"enabled", false}, {"sourceIds", QVariantMap{{"modrinth", "existing"}}}}};
        h.model.offer(pack(), files,
                      {reference("existing"), reference(""), reference("wrong", "unknown"), reference("new"),
                       reference("new")});
        QTRY_COMPARE(state(h.model), "ready");
        QCOMPARE(h.model.rows().size(), 1);
        QCOMPARE(h.source->calls.size(), 1);
        QCOMPARE(h.source->calls.first().reference.projectId, "new");
    }
    void anExactFileOnAnotherProviderIsAlreadyInstalled() {
        Harness h;
        const auto candidate = request("11", "curseforge");
        const QVariantList files{QVariantMap{{"category", "mods"},
                                             {"sha1", candidate.release.sha1.toUpper()},
                                             {"sourceIds", QVariantMap{{"modrinth", "another-source"}}}}};
        int installs = 0;
        h.model.setInstaller([&](auto, auto) {
            ++installs;
            return true;
        });
        h.model.offer(pack(), files, {reference("11", "curseforge")});
        QTRY_COMPARE(state(h.model), "installed");
        h.model.install(key(h.model));
        QCOMPARE(installs, 0);
    }
    void largeInstalledInventoriesDoNotRequirePerReferenceRescans() {
        Harness h;
        QVariantList files, refs;
        for (int i = 0; i < 5000; ++i)
            files << QVariantMap{{"category", "mods"},
                                 {"sourceIds", QVariantMap{{"modrinth", QString::number(i)}}}};
        for (int i = 0; i < 256; ++i)
            refs << reference(QString::number(4700 + i));
        QElapsedTimer elapsed;
        elapsed.start();
        h.model.offer(pack(), files, refs);
        const auto milliseconds = elapsed.elapsed();
        QVERIFY(!h.model.visible());
        QVERIFY(h.source->calls.isEmpty());
        qInfo() << "Filter256 optional references against5000 installed files ms" << milliseconds;
        QVERIFY(test_performance::releaseBudgetMet(milliseconds, 150));
    }
    void disablePromptCancelsOnlyItsReadsAndSuppressesFutureOffers() {
        Harness h;
        h.source->held = true;
        h.model.offer(pack(), {}, {reference()});
        QTRY_COMPARE(h.source->calls.size(), 1);
        h.preferences->setPromptOptionalDependencies(false);
        QVERIFY(!h.model.visible());
        h.source->complete(0);
        QVERIFY(!h.model.visible());
        h.model.offer(pack(), {}, {reference("other")});
        QCOMPARE(h.source->calls.size(), 1);
        QVERIFY(h.source->cancellations > 0);
    }
    void queuedInstanceOffersAndLateCallbacksStayIsolated() {
        Harness h;
        h.source->held = true;
        h.model.offer(pack(), {}, {reference("first")});
        QTRY_COMPARE(h.source->calls.size(), 1);
        h.model.offer(pack("instance-b"), {}, {reference("second")});
        h.model.offer(pack("instance-b"), {}, {reference("second"), reference("third")});
        h.model.dismiss();
        QTRY_COMPARE(h.source->calls.size(), 2);
        QCOMPARE(h.model.pack().value("path").toString(), "instance-b");
        QCOMPARE(h.model.rows().size(), 2);
        h.source->complete(0);
        QCOMPARE(state(h.model), "checking");
        h.source->complete(1);
        QTRY_COMPARE(h.source->calls.size(), 3);
        h.source->complete(2);
        QCOMPARE(state(h.model, 0), "ready");
        QCOMPARE(state(h.model, 1), "ready");
    }
    void lookupErrorsAndExceptionsRemainIndependentlyRetryable() {
        Harness h;
        h.source->throwing = true;
        h.model.offer(pack(), {}, {reference()});
        QTRY_COMPARE(state(h.model), "error");
        h.source->throwing = false;
        h.source->error = "Provider unavailable";
        h.model.retry(key(h.model));
        QTRY_COMPARE(h.source->calls.size(), 2);
        QCOMPARE(state(h.model), "error");
        h.source->error.clear();
        h.model.retry(key(h.model));
        QTRY_COMPARE(state(h.model), "ready");
        h.model.setInstaller([](auto, auto) -> bool { throw std::runtime_error("Queue fixture exception"); });
        h.model.install(key(h.model));
        QCOMPARE(state(h.model), "error");
        h.source->cancelThrows = true;
        h.model.dismiss();
        QVERIFY(!h.model.visible());
    }
    void incompatibleCandidateFactsCannotAuthorizeInstallation_data() {
        QTest::addColumn<QString>("fault");
        for (const auto &fault :
             {"provider", "project", "version", "game", "loader", "channel", "hash", "url", "id"})
            QTest::newRow(fault) << QString(fault);
    }
    void incompatibleCandidateFactsCannotAuthorizeInstallation() {
        QFETCH(QString, fault);
        Harness h;
        h.source->transform = [&](ContentInstallRequest &request) {
            if (fault == "provider")
                request.release.provider = "curseforge";
            if (fault == "project")
                request.release.projectId = "wrong";
            if (fault == "version")
                request.release.id = "wrong";
            if (fault == "game")
                request.release.gameVersions = {"1.20.1"};
            if (fault == "loader")
                request.release.loaders = {"neoforge"};
            if (fault == "channel")
                request.release.channel = "beta";
            if (fault == "hash")
                request.release.sha1.clear();
            if (fault == "url")
                request.release.url.clear();
            if (fault == "id")
                request.release.id.clear();
        };
        auto ref = reference();
        ref["versionId"] = "optional-v1";
        h.model.offer(pack(), {}, {ref});
        QTRY_COMPARE(state(h.model), "error");
        h.model.installationFinished("instance-a", {request("unrelated").release.toVariant()},
                                     {QVariantMap{{"category", "mods"}, {"sha1", request().release.sha1}}},
                                     {});
        QCOMPARE(state(h.model), "error");
    }
    void policyChangesReResolveOnlyUnqueuedSuggestions() {
        Harness h;
        h.source->transform = [](ContentInstallRequest &request) { request.release.channel = "beta"; };
        h.model.offer(pack(), {}, {reference()});
        QTRY_COMPARE(state(h.model), "error");
        h.policy->setChannels("instance-a", {"beta"});
        QTRY_COMPARE(state(h.model), "ready");
        QCOMPARE(h.source->calls.last().channels, QStringList{"beta"});
        h.model.setInstaller([](auto, auto) { return true; });
        h.model.install(key(h.model));
        QCOMPARE(state(h.model), "queued");
        h.policy->setChannels("instance-a", {"release"});
        QCOMPARE(state(h.model), "queued");
        h.model.installationFinished("instance-a", {request().release.toVariant()}, {},
                                     "Version no longer allowed");
        QCOMPARE(state(h.model), "error");
    }
    void disposalDuringPublicationStopsThePendingRead() {
        Harness h;
        auto *model = new OptionalDependencies(h.source, h.preferences, h.policy);
        QPointer<OptionalDependencies> weak(model);
        connect(model, &OptionalDependencies::changed, model, [model] { delete model; });
        model->offer(pack(), {}, {reference()});
        QVERIFY(!weak);
        QVERIFY(h.source->calls.isEmpty());
    }
    void repeatedSourceCallbacksCannotReplaceAnAcceptedResult() {
        Harness h;
        h.model.offer(pack(), {}, {reference()});
        QTRY_COMPARE(state(h.model), "ready");
        h.source->error = "obsolete duplicate callback";
        h.source->complete(0);
        QCOMPARE(state(h.model), "ready");
        QVERIFY(h.model.rows().first().toMap().value("error").toString().isEmpty());
    }
};
QTEST_GUILESS_MAIN(OptionalDependencyTests)
#include "OptionalDependencyTests.moc"
