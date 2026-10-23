#include "domain/ContentTypes.h"
#include "features/explore/BrowseInstalls.h"
#include "platform/BrowseSettingsStore.h"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>
using namespace studio;
class DeferredVersionCatalogue final : public IContentCatalogue {
  public:
    QVector<VersionsCallback> pending;
    void refresh(const QVariantList &rows, const QString &, const QString &, IndexCallback done) override {
        done(rows, {});
    }
    void versions(const QVariantMap &, const QString &, const QString &, VersionsCallback done) override {
        pending << std::move(done);
    }
    void setCurseforgeKey(const QByteArray &) override {}
};
class BrowseInstallsTests : public QObject {
    Q_OBJECT
    static QVariantMap project() {
        return {{"provider", "modrinth"},
                {"id", "project"},
                {"type", "mod"},
                {"sourceIds", QVariantMap{{"modrinth", "project"}}}};
    }
    static QVariantMap row() {
        return {{"category", "mods"},
                {"sha1", "hash"},
                {"enabled", true},
                {"sourceIds", QVariantMap{{"modrinth", "project"}}},
                {"installedRelease",
                 QVariantMap{{"provider", "modrinth"}, {"projectId", "project"}, {"id", "version"}}}};
    }
  private slots:
    void explicitStagesControlQuickAndSelectedInstalls_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<int>("mask");
        for (const auto &provider : {"modrinth", "curseforge"})
            for (int mask = 0; mask < 8; ++mask)
                QTest::newRow(qPrintable(QString("%1-%2").arg(provider).arg(mask)))
                    << QString(provider) << mask;
    }
    void explicitStagesControlQuickAndSelectedInstalls() {
        QFETCH(QString, provider);
        QFETCH(int, mask);
        QTemporaryDir temp;
        auto policy = std::make_shared<PackReleasePolicy>(
            std::make_shared<BrowseSettingsStore>(temp.path() + "/stages.ini"));
        const QStringList stages{"release", "beta", "alpha"};
        QStringList selection;
        for (int i = 0; i < 3; ++i)
            if (mask & (1 << i))
                selection << stages[i];
        policy->setChannels("a", selection);
        auto source = std::make_shared<DeferredVersionCatalogue>();
        BrowseInstalls model(source);
        model.setReleasePolicy(policy);
        model.setCurseforgeKey("fixture");
        model.setTargetPath("a");
        model.setContext({{"path", "a"}, {"minecraft", "1.21.1"}}, {}, false);
        QSignalSpy installs(&model, &BrowseInstalls::installRequested);
        QVector<Release> releases;
        for (const auto &stage : stages) {
            Release release;
            release.provider = provider;
            release.channel = stage;
            release.url = "https://example.invalid/file.jar";
            release.sha1 = QString(40, 'a');
            release.gameVersions = {"1.21.1"};
            releases.prepend(release); // Newest alpha, then beta, then release.
        }
        model.install(project());
        QCOMPARE(source->pending.size(), 1);
        source->pending.first()(releases, {});
        QCOMPARE(installs.size(), 1);
        const auto expected = selection.isEmpty() ? QString("release") : selection.last();
        QCOMPARE(installs.first().first().toMap().value("channel").toString(), expected);
        for (const auto &release : releases) {
            const auto count = installs.size();
            model.installVersion(release.toVariant(), project());
            QCOMPARE(installs.size(), count + int((selection.isEmpty() ? QStringList{"release"} : selection)
                                                      .contains(release.channel)));
        }
    }
    void changingPolicyInvalidatesPendingLookupAndDoesNotBroadenAnotherInstance() {
        QTemporaryDir temp;
        auto policy = std::make_shared<PackReleasePolicy>(
            std::make_shared<BrowseSettingsStore>(temp.path() + "/stages.ini"));
        auto source = std::make_shared<DeferredVersionCatalogue>();
        BrowseInstalls model(source);
        model.setReleasePolicy(policy);
        model.setTargetPath("a");
        model.setContext({{"path", "a"}, {"minecraft", "1.21.1"}}, {}, false);
        QSignalSpy installs(&model, &BrowseInstalls::installRequested);
        model.install(project());
        QVERIFY(model.lookupsPending());
        policy->setChannels("a", {"beta"});
        QVERIFY(!model.lookupsPending());
        Release release;
        release.provider = "modrinth";
        release.url = "https://example.invalid/mod.jar";
        release.sha1 = QString(40, 'a');
        release.gameVersions = {"1.21.1"};
        source->pending.first()({release}, {});
        QCOMPARE(installs.size(), 0);
        model.install(project());
        QTRY_COMPARE(source->pending.size(), 2);
        release.channel = "beta";
        source->pending.last()({release}, {});
        QCOMPARE(installs.size(), 1);
        QCOMPARE(policy->channels("b"), QStringList{"release"});
    }
    void badgeReadsAndIdenticalContextUpdatesStayWithinBudget() {
        BrowseInstalls model(std::make_shared<DeferredVersionCatalogue>());
        QVariantList rows;
        for (int i = 0; i < 5000; ++i) {
            auto file = row();
            file.remove("installedRelease");
            file["sourceIds"] = QVariantMap{{"modrinth", QString::number(i)}};
            file["sha1"] = QString::number(i);
            rows << file;
        }
        const QVariantMap pack{{"path", "a"}};
        model.setTargetPath("a");
        model.setContext(pack, rows, false);
        auto last = project();
        last["sourceIds"] = QVariantMap{{"modrinth", "4999"}};
        QElapsedTimer elapsed;
        elapsed.start();
        for (int i = 0; i < 10000; ++i)
            QVERIFY(model.state(last).value("installed").toBool());
        qInfo() << "10000 indexed badge reads over5000 mods ms" << elapsed.elapsed();
        QVERIFY(elapsed.elapsed() < 1000);
        QSignalSpy changes(&model, &BrowseInstalls::changed);
        for (int i = 0; i < 100; ++i)
            model.setContext(pack, rows, false);
        QCOMPARE(changes.count(), 0);
    }
    void exactReleaseHashesInformCardsAcrossProvidersWithoutTitleGuessing() {
        BrowseInstalls model(std::make_shared<DeferredVersionCatalogue>());
        auto file = row();
        file.remove("sourceIds");
        file.remove("installedRelease");
        model.setTargetPath("a");
        model.setContext({{"path", "a"}}, {file}, false);
        auto p = project();
        p["sourceIds"] = QVariantMap{{"modrinth", "project"}, {"curseforge", "42"}};
        QVERIFY(!model.state(p).value("installed").toBool());
        auto release = QVariantMap{{"provider", "modrinth"}, {"projectId", "project"}, {"sha1", "hash"}};
        const QVariantList groups{QVariantMap{{"variants", QVariantList{release}}}};
        const int revision = model.revision();
        model.rememberReleases(p, groups);
        QVERIFY(model.revision() > revision);
        QVERIFY(model.state(p).value("installed").toBool());
        QVERIFY(!model.state(p).value("canInstall").toBool());
        auto cf = p;
        cf["provider"] = "curseforge";
        cf["id"] = "42";
        cf["sourceIds"] = QVariantMap{{"curseforge", "42"}};
        QVERIFY(model.state(cf).value("installed").toBool());
        model.setContext({{"path", "a"}}, {file}, false);
        QVERIFY(model.state(cf).value("installed").toBool());
        model.setTargetPath("b");
        model.setContext({{"path", "b"}}, {}, false);
        QVERIFY(!model.state(cf).value("installed").toBool());
        file["sha1"] = "other binary";
        model.setContext({{"path", "b"}}, {file}, false);
        model.rememberReleases(p, groups);
        QVERIFY(!model.state(cf).value("installed").toBool());
    }
    void verifiedPinsCountAsInstalledBeforeProviderMetadataArrives() {
        BrowseInstalls model(std::make_shared<DeferredVersionCatalogue>());
        auto file = row();
        file.remove("sourceIds");
        model.setTargetPath("a");
        model.setContext({{"path", "a"}}, {file}, false);
        QVERIFY(model.state(project()).value("installed").toBool());
        file["installedRelease"] = QVariantMap{
            {"provider", "modrinth"}, {"projectId", "project"}, {"id", "version"}, {"sha1", "stale-hash"}};
        model.setContext({{"path", "a"}}, {file}, false);
        QVERIFY(!model.state(project()).value("installed").toBool());
    }
    void approvedCurseforgeOnlyProjectsCanInstallAndDisconnectUpdatesTheirState() {
        auto source = std::make_shared<DeferredVersionCatalogue>();
        BrowseInstalls model(source);
        model.setTargetPath("a");
        model.setContext({{"path", "a"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}}, {}, false);
        const QVariantMap p{{"provider", "curseforge"},
                            {"id", "11"},
                            {"type", "mod"},
                            {"sourceIds", QVariantMap{{"curseforge", "11"}}}};
        QVERIFY(!model.state(p).value("canInstall").toBool());
        QSignalSpy changes(&model, &BrowseInstalls::changed);
        model.setCurseforgeKey("fixture-key");
        QCOMPARE(changes.count(), 1);
        QVERIFY(model.state(p).value("canInstall").toBool());
        QCOMPARE(model.state(p).value("label").toString(), QString("Install"));
        QSignalSpy installs(&model, &BrowseInstalls::installRequested);
        model.install(p);
        QCOMPARE(source->pending.size(), 1);
        Release release;
        release.provider = "curseforge";
        release.projectId = "11";
        release.id = "101";
        release.sha1 = "hash";
        release.url = "https://example.invalid/mod.jar";
        release.gameVersions = {"1.21.1"};
        release.loaders = {"fabric"};
        source->pending.first()({release}, {});
        QCOMPARE(installs.count(), 1);
        model.setCurseforgeKey({});
        QVERIFY(!model.state(p).value("canInstall").toBool());
    }
    void installedStateIsScopedToTheRequestedInstance() {
        auto source = std::make_shared<DeferredVersionCatalogue>();
        BrowseInstalls model(source);
        model.setTargetPath("a");
        model.setContext({{"path", "a"}}, {row()}, false);
        QVERIFY(model.state(project()).value("installed").toBool());
        QVERIFY(model.installed({{"provider", "modrinth"}, {"projectId", "project"}, {"id", "version"}}));
        QVERIFY(!model.installed({{"provider", "modrinth"},
                                  {"projectId", "project"},
                                  {"id", "version"},
                                  {"sha1", "different binary"}}));
        QVERIFY(!model.installed({{"provider", "modrinth"}, {"projectId", "project"}, {"id", "other"}}));
        model.setTargetPath("b");
        QVERIFY(!model.ready());
        QVERIFY(!model.state(project()).value("installed").toBool());
        model.setContext({{"path", "b"}}, {}, false);
        QVERIFY(model.ready());
        QVERIFY(model.state(project()).value("canInstall").toBool());
        QVERIFY(!model.installed({{"sha1", "hash"}}));
    }
    void acceptedVersionLookupRetainsItsOriginalInstanceAcrossNavigation() {
        auto source = std::make_shared<DeferredVersionCatalogue>();
        BrowseInstalls model(source);
        model.setTargetPath("a");
        model.setContext({{"path", "a"}, {"minecraft", "1.21.1"}}, {}, false);
        QSignalSpy installs(&model, &BrowseInstalls::installRequested);
        model.install(project());
        QCOMPARE(source->pending.size(), 1);
        model.setTargetPath("b");
        model.setContext({{"path", "b"}, {"minecraft", "1.21.1"}}, {}, false);
        Release release;
        release.provider = "modrinth";
        release.projectId = "project";
        release.id = "new";
        release.sha1 = "hash";
        release.url = "https://example.invalid/mod.jar";
        release.gameVersions = {"1.21.1"};
        source->pending.first()({release}, {});
        QCOMPARE(installs.count(), 1);
        QCOMPARE(installs.first()[1].toMap().value("targetPack").toMap().value("path").toString(),
                 QString("a"));
        model.install(project());
        QTRY_COMPARE(source->pending.size(), 2);
        source->pending.last()({release}, {});
        QCOMPARE(installs.count(), 2);
        QCOMPARE(installs.last()[1].toMap().value("targetPack").toMap().value("path").toString(),
                 QString("b"));
    }
    void independentProjectLookupsAreOptimisticAndQueuedWithoutGlobalBusy() {
        auto source = std::make_shared<DeferredVersionCatalogue>();
        BrowseInstalls model(source);
        model.setTargetPath("a");
        model.setContext({{"path", "a"}, {"minecraft", "1.21.1"}}, {}, false);
        auto first = project(), second = project();
        second["id"] = "other";
        second["sourceIds"] = QVariantMap{{"modrinth", "other"}};
        model.install(first);
        model.install(second);
        QVERIFY(!model.busy());
        QVERIFY(model.state(first).value("installed").toBool());
        QVERIFY(model.state(second).value("installed").toBool());
        QCOMPARE(source->pending.size(), 1);
        QSignalSpy errors(&model, &BrowseInstalls::operationFailed);
        source->pending.first()({}, "Fixture provider error");
        QCOMPARE(errors.count(), 1);
        QVERIFY(!model.state(first).value("installed").toBool());
        QTRY_COMPARE(source->pending.size(), 2);
        model.cancelPending();
        source->pending.last()({}, "Cancelled lookup");
        QCOMPARE(errors.count(), 1);
        QVERIFY(!model.state(second).value("installed").toBool());
    }
    void dataPacksAreInstanceScopedAndDisabledFilesRemainInstalled() {
        BrowseInstalls model(std::make_shared<DeferredVersionCatalogue>());
        auto file = row();
        file["category"] = "datapacks";
        file["relativePath"] = "datapacks/example.zip";
        file["enabled"] = false;
        auto p = project();
        p["type"] = "datapack";
        model.setTargetPath("a");
        model.setContext({{"path", "a"}}, {file}, false);
        QVERIFY(model.state(p).value("installed").toBool());
        QVERIFY(model.state(p).value("label").toString().contains("disabled"));
        file["relativePath"] = "saves/one/datapacks/example.zip";
        model.setContext({{"path", "a"}}, {file}, false);
        QVERIFY(!model.state(p).value("installed").toBool());
        QVERIFY(model.state(p).value("canInstall").toBool());
    }
    void enabledConnectorOffersFabricWithoutChangingThePrimaryLoader() {
        BrowseInstalls model(std::make_shared<DeferredVersionCatalogue>());
        model.setTargetPath("a");
        QVariantMap bridge{{"category", "mods"}, {"modId", "connector"}, {"enabled", true}};
        model.setContext({{"path", "a"}, {"loader", "neoforge"}}, {bridge}, false);
        QVERIFY(model.connectorAvailable());
        QCOMPARE(model.versionLoaders(), QString("neoforge|fabric"));
        QCOMPARE(model.pack().value("loader").toString(), QString("neoforge"));
        bridge["enabled"] = false;
        model.setContext({{"path", "a"}, {"loader", "neoforge"}}, {bridge}, false);
        QVERIFY(!model.connectorAvailable());
    }
    void nativeDataPackVariantsUseTheVerifiedModDependencyInstaller() {
        BrowseInstalls model(std::make_shared<DeferredVersionCatalogue>());
        model.setTargetPath("a");
        model.setContext({{"path", "a"}}, {}, false);
        QSignalSpy installs(&model, &BrowseInstalls::installRequested);
        auto p = project();
        p["type"] = "datapack";
        model.installVersion({{"filename", "pack.jar"}, {"provider", "modrinth"}, {"id", "version"}}, p);
        QCOMPARE(installs.count(), 1);
        QCOMPARE(installs.first()[1].toMap().value("type").toString(), QString("mod"));
        QCOMPARE(installs.first().size(), 2);
    }
    void aCombinedResourceDataZipHasDestinationSpecificInstalledState() {
        BrowseInstalls model(std::make_shared<DeferredVersionCatalogue>());
        model.setTargetPath("a");
        auto file = row();
        file["category"] = "resourcepacks";
        model.setContext({{"path", "a"}}, {file}, false);
        const QVariantMap release{{"filename", "pack.zip"}, {"sha1", "hash"}};
        QVERIFY(model.installed(release, "resourcepack"));
        QVERIFY(!model.installed(release, "datapack"));
    }
    void labelsAndSeparateProviderDownloadCounts() {
        QCOMPARE(contentLabel("fabric"), QString("Fabric"));
        QCOMPARE(contentLabel("forge"), QString("Forge"));
        QCOMPARE(contentLabel("neoforge"), QString("NeoForge"));
        QCOMPARE(contentLabel("quilt"), QString("Quilt"));
        Project mr;
        mr.provider = "modrinth";
        mr.id = "mr";
        mr.downloads = 100;
        Project cf = mr;
        cf.provider = "curseforge";
        cf.id = "cf";
        cf.downloads = 200;
        const auto merged = mergeProjects({cf, mr}, {{"modrinth:mr", "shared"}, {"curseforge:cf", "shared"}});
        QCOMPARE(merged.size(), 1);
        const auto stats = merged.first().toVariant().value("downloadStats").toList();
        QCOMPARE(stats.size(), 2);
        QCOMPARE(stats[0].toMap().value("provider").toString(), QString("modrinth"));
        QCOMPARE(stats[0].toMap().value("raw").toLongLong(), 100);
        QCOMPARE(stats[1].toMap().value("raw").toLongLong(), 200);
    }
};
QTEST_GUILESS_MAIN(BrowseInstallsTests)
#include "BrowseInstallsTests.moc"
