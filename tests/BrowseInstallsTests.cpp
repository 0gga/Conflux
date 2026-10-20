#include "domain/ContentTypes.h"
#include "features/explore/BrowseInstalls.h"
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
    void staleVersionLookupCannotInstallIntoAnotherInstance() {
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
        QCOMPARE(installs.count(), 0);
        model.install(project());
        source->pending.last()({release}, {});
        QCOMPARE(installs.count(), 1);
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
