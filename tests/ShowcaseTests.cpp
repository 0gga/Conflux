#include "features/showcase/DescriptionRenderer.h"
#include "features/showcase/ProjectShowcase.h"
#include "features/showcase/ReleaseFilters.h"
#include "platform/InstalledModIdentity.h"
#include "platform/WorldLaunchAdapters.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QUrlQuery>
#include <QtTest>
using namespace studio;
class IdentityFixture final : public IInstalledModIdentity {
  public:
    InstalledModIdentity inspect(const QVariantMap &, const QVariantMap &) const override {
        return {"file-hash", {}, {{"modrinth", "verified"}}};
    }
};
class ShowcaseFixture final : public IProjectShowcaseSource {
  public:
    QList<ResolveCallback> pending;
    DetailCallback detail;
    QString requestedGame, requestedLoader;
    void resolve(const InstalledModIdentity &, ResolveCallback callback) override {
        pending << std::move(callback);
    }
    void load(const Project &, const QString &game, const QString &loader, DetailCallback callback) override {
        requestedGame = game;
        requestedLoader = loader;
        detail = std::move(callback);
    }
    void setCurseforgeKey(const QByteArray &) override {}
};
class LaunchFixture final : public ILaunchUrlOpener {
  public:
    QUrl requested;
    bool registered = true;
    bool available(const QString &) const override {
        return registered;
    }
    bool open(const QUrl &url) override {
        requested = url;
        return true;
    }
};
class ShowcaseTests : public QObject {
    Q_OBJECT
    static void write(const QString &path, const QByteArray &data) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }
  private slots:
    void platformCountsOnlyIncludeAvailablePlatforms() {
        ReleaseFilters filters;
        const QVariantMap release{{"provider", "modrinth"},
                                  {"gameVersions", QStringList{"1.21.1"}},
                                  {"loaders", QStringList{"neoforge"}}};
        const QVariantList groups{QVariantMap{{"label", "Version"}, {"variants", QVariantList{release}}}};
        filters.setContext("only-neoforge", groups, {"1.21.1"}, {"neoforge", "fabric"});
        QCOMPARE(filters.filters().value("loaders").toStringList(), QStringList{"neoforge"});
        QCOMPARE(filters.rows().size(), 1);
        filters.configure({{"loaders", QStringList{"forge", "neoforge"}}});
        QCOMPARE(filters.filters().value("loaders").toStringList(), QStringList{"neoforge"});
        filters.configure({{"versions", QStringList{"1.20.1"}}});
        QVERIFY(filters.rows().isEmpty()); // An unavailable Minecraft target must not broaden compatibility.
    }

    void openPublishesOneSeededStateAndReusesFormattedDescriptions() {
        auto source = std::make_shared<ShowcaseFixture>();
        ProjectShowcase showcase(source, std::make_shared<IdentityFixture>());
        QSignalSpy changed(&showcase, &ProjectShowcase::changed);
        const QVariantMap seed{{"provider", "modrinth"}, {"id", "project"}, {"title", "Seed title"}};
        showcase.openProject({}, seed);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(showcase.project().value("title").toString(), QString("Seed title"));
        Project project;
        project.id = "project";
        project.provider = "modrinth";
        source->pending.last()(project, {});
        ProjectDetail detail;
        detail.description = "# Cached description\n\nReadable text.";
        source->detail(detail);
        QTRY_VERIFY(!showcase.busy());
        const auto original = showcase.descriptionBlocks();
        QVERIFY(!original.isEmpty());
        showcase.close();
        showcase.openProject({}, seed);
        source->pending.last()(project, {});
        source->detail(detail);
        // The content hash hits the bounded parsed cache synchronously.
        QCOMPARE(showcase.descriptionBlocks(), original);
        QTRY_VERIFY(!showcase.busy());
        detail.description = "# Changed text";
        showcase.close();
        showcase.openProject({}, seed);
        source->pending.last()(project, {});
        source->detail(detail);
        QTRY_VERIFY(!showcase.busy());
        QVERIFY(showcase.descriptionBlocks() != original);
    }

    void closingDuringDescriptionFormattingDiscardsTheWorkerResult() {
        auto source = std::make_shared<ShowcaseFixture>();
        ProjectShowcase showcase(source, std::make_shared<IdentityFixture>());
        showcase.openProject({}, {{"provider", "modrinth"}, {"id", "project"}});
        Project project;
        project.id = "project";
        project.provider = "modrinth";
        source->pending.first()(project, {});
        ProjectDetail detail;
        detail.description = QString("# Heading\n\nSome content.\n").repeated(500);
        source->detail(detail);
        QVERIFY(showcase.busy());
        showcase.close();
        QSignalSpy changed(&showcase, &ProjectShowcase::changed);
        QTest::qWait(150);
        QVERIFY(showcase.descriptionBlocks().isEmpty());
        QVERIFY(!showcase.busy());
        QCOMPARE(changed.count(), 0);
    }
    void changelogsLoadOnlyWhenSelectedAndKeepExactSourceIdentity() {
        auto source = std::make_shared<ShowcaseFixture>();
        ProjectShowcase showcase(source, std::make_shared<IdentityFixture>());
        showcase.openProject({}, {{"provider", "modrinth"}, {"id", "project"}, {"type", "mod"}});
        Project project;
        project.id = "project";
        project.provider = "modrinth";
        source->pending.first()(project, {});
        Release older;
        older.provider = "modrinth";
        older.projectId = "project";
        older.id = "old";
        older.version = "1";
        older.published = "2026-09-01";
        older.changelog = "# Old version\nFixed the original issue.";
        Release latest = older;
        latest.id = "new";
        latest.version = "2";
        latest.published = "2026-10-21";
        latest.changelog = "# Latest version\nNew changes.";
        ProjectDetail detail;
        detail.releases = {older, latest};
        source->detail(detail);
        QTRY_VERIFY(!showcase.busy());
        QVERIFY(showcase.changelogRelease().isEmpty());
        QCOMPARE(showcase.changelogVersions().first().toMap().value("id").toString(), QString("new"));
        showcase.selectChangelog(older.toVariant());
        QTRY_VERIFY(!showcase.changelogBusy());
        QCOMPARE(showcase.changelogRelease().value("id").toString(), QString("old"));
        QVERIFY(!showcase.changelogBlocks().isEmpty());
        const auto original = showcase.changelogBlocks();
        showcase.selectChangelog(latest.toVariant());
        QTRY_VERIFY(!showcase.changelogBusy());
        QVERIFY(showcase.changelogBlocks() != original);
        showcase.selectChangelog(older.toVariant());
        QVERIFY(!showcase.changelogBusy());
        QCOMPARE(showcase.changelogBlocks(), original);
        showcase.selectChangelog({{"provider", "curseforge"}, {"id", "old"}});
        QCOMPARE(showcase.changelogRelease().value("provider").toString(), QString("modrinth"));
        showcase.close();
        QVERIFY(showcase.changelogBlocks().isEmpty());
        QVERIFY(showcase.changelogVersions().isEmpty());
    }

    void installedAssetShowcaseRetainsItsDestinationType() {
        auto source = std::make_shared<ShowcaseFixture>();
        ProjectShowcase showcase(source, std::make_shared<IdentityFixture>());
        showcase.open({{"minecraft", "1.21.1"}, {"loader", "fabric"}},
                      {{"relativePath", "datapacks/example.zip"}, {"category", "datapacks"}});
        QTRY_COMPARE(source->pending.size(), 1);
        Project project;
        project.id = "project";
        project.provider = "modrinth";
        project.type = "mod";
        source->pending.first()(project, {});
        QCOMPARE(showcase.project().value("type").toString(), QString("datapack"));
    }
    void modShowcasesLoadAllCompatibilityOptions() {
        auto source = std::make_shared<ShowcaseFixture>();
        ProjectShowcase showcase(source, std::make_shared<IdentityFixture>());
        showcase.openProject({{"minecraft", "1.21.1"}, {"loader", "neoforge"}},
                             {{"provider", "modrinth"}, {"id", "project"}, {"type", "mod"}});
        QCOMPARE(source->pending.size(), 1);
        Project project;
        project.id = "project";
        project.provider = "modrinth";
        project.type = "mod";
        source->pending.first()(project, {});
        QVERIFY(source->detail);
        QVERIFY(source->requestedGame.isEmpty());
        QVERIFY(source->requestedLoader.isEmpty());
    }
    void releaseFiltersKeepVariantsAndCombineOrSelections() {
        auto release = [](QString id, QString channel, QString game, QString loader, QString provider) {
            return QVariantMap{{"id", id},
                               {"channel", channel},
                               {"gameVersions", QStringList{game}},
                               {"loaders", QStringList{loader}},
                               {"provider", provider},
                               {"sha1", id + "-hash"}};
        };
        const auto a = release("a", "release", "1.21.1", "neoforge", "modrinth");
        const auto b = release("b", "beta", "1.21.1", "fabric", "curseforge");
        const auto c = release("c", "alpha", "1.20.1", "forge", "modrinth");
        const QVariantList groups{QVariantMap{{"label", "1.0"}, {"variants", QVariantList{a, b}}},
                                  QVariantMap{{"label", "0.9"}, {"variants", QVariantList{c}}}};
        ReleaseFilters filters;
        filters.setContext("pack/project", groups, {"1.21.1"}, {"neoforge"});
        QCOMPARE(filters.rows().size(), 1);
        QCOMPARE(filters.rows().first().toMap().value("variants").toList(), QVariantList{a});
        QCOMPARE(filters.options().value("versions").toStringList(), (QStringList{"1.21.1", "1.20.1"}));
        filters.configure({{"versions", QStringList{"1.21.1", "1.20.1"}},
                           {"loaders", QStringList{"fabric", "forge"}},
                           {"channels", QStringList{"beta", "alpha"}}});
        QCOMPARE(filters.rows().size(), 2);
        QCOMPARE(filters.rows().first().toMap().value("variants").toList(), QVariantList{b});
        filters.configure({{"source", "modrinth"}});
        QCOMPARE(filters.rows().size(), 1);
        QCOMPARE(filters.rows().first().toMap().value("variants").toList(), QVariantList{c});
        filters.clear();
        QCOMPARE(filters.rows(), groups);
        filters.configure({{"channels", QStringList{"release", "invalid"}}, {"source", "curseforge"}});
        QVERIFY(filters.rows().isEmpty());
        QCOMPARE(filters.filters().value("channels").toStringList(), QStringList{"release"});
    }
    void dataPackAndLoaderPlatformsAreSelectableTogether() {
        const auto release = [](QString id, QStringList loaders) {
            return QVariantMap{{"id", id},
                               {"loaders", loaders},
                               {"gameVersions", QStringList{"1.21.1"}},
                               {"sha1", id + "-exact-hash"}};
        };
        const auto zip = release("zip", {"datapack"});
        const auto jar = release("jar", {"neoforge"});
        const auto fabric = release("fabric", {"fabric"});
        ReleaseFilters filters;
        filters.setContext("instance/data", {QVariantMap{{"variants", QVariantList{zip, jar, fabric}}}},
                           {"1.21.1"}, {});
        QVERIFY(filters.options().value("loaders").toStringList().contains("datapack"));
        filters.configure({{"loaders", QStringList{"datapack", "neoforge"}}});
        QCOMPARE(filters.rows().first().toMap().value("variants").toList(), (QVariantList{zip, jar}));
        filters.configure({{"loaders", QStringList{"datapack"}}});
        QCOMPARE(filters.rows().first().toMap().value("variants").toList(), QVariantList{zip});
        filters.configure({{"loaders", QStringList{}}});
        QCOMPARE(filters.rows().first().toMap().value("variants").toList(), (QVariantList{zip, jar, fabric}));
    }
    void releaseFilterDefaultsSeedOnceAndResetForNewContext() {
        ReleaseFilters filters;
        filters.setContext("pending", {}, {}, {});
        filters.setContext("pending", {}, {"1.21.1"}, {"fabric"});
        QCOMPARE(filters.filters().value("versions").toStringList(), QStringList{"1.21.1"});
        filters.configure({{"versions", QStringList{"1.20.1"}}});
        filters.setContext("pending", {QVariantMap{{"label", "refresh"}}}, {"1.21.1"}, {"fabric"});
        QCOMPARE(filters.filters().value("versions").toStringList(), QStringList{"1.20.1"});
        filters.clear();
        filters.setContext("pending", {}, {"1.21.1"}, {"fabric"});
        QVERIFY(filters.filters().value("versions").toStringList().isEmpty());
        filters.setContext("next", {}, {"1.20.1"}, {"forge"});
        QCOMPARE(filters.filters().value("loaders").toStringList(), QStringList{"forge"});
    }
    void sourceFormattingKeepsImageRowsLinksTablesAndCode() {
        const auto rendered = describeContent(
            "![Banner](https://example.invalid/banner.png)\n\n<p align='center'><a "
            "href='https://example.invalid/community'><img src='https://example.invalid/community.png' "
            "alt='Community'></a> <img "
            "src='https://example.invalid/source.png'></p>\n\n<br>![Divider](https://example.invalid/"
            "divider.png)\n\n### Features\n- **Visible text**\n\n| Game | Status |\n| --- | --- |\n| "
            "1.21.1 "
            "| Supported |\n\n```xml\n<keep>This is code</keep>\n```",
            true);
        QVERIFY(rendered.html.contains("Visible text"));
        QVERIFY(rendered.html.contains("Features"));
        QVERIFY(rendered.html.contains("Supported"));
        QVERIFY(rendered.html.contains("&lt;keep&gt;"));
        int images = 0, maxRow = 0;
        bool socialLink = false, table = false;
        for (const auto &value : rendered.blocks) {
            const auto block = value.toMap();
            table |= block.value("kind") == "table";
            maxRow = qMax(maxRow, int(block.value("items").toList().size()));
            for (const auto &item : block.value("items").toList()) {
                ++images;
                socialLink |= item.toMap().value("link") == "https://example.invalid/community";
            }
        }
        QCOMPARE(images, 4);
        QCOMPARE(maxRow, 2);
        QVERIFY(socialLink);
        QVERIFY(table);
    }
    void descriptionRejectsLocalImagesScriptsAndProviderPalette() {
        const auto rendered = describeContent(
            "<div style='color:black'><script>secret()</script><img src='file:///private.png'><img "
            "src='data:image/png;base64,no'><p><b>Readable</b></p><img "
            "src='https://example.invalid/banner.png' onerror='secret()'></div>",
            false);
        QVERIFY(rendered.html.contains("Readable"));
        QVERIFY(!rendered.html.contains("style="));
        const auto json = QJsonDocument::fromVariant(rendered.blocks).toJson();
        QVERIFY(!json.contains("file:///"));
        QVERIFY(!json.contains("data:image"));
        QVERIFY(!json.contains("secret()"));
        QVERIFY(json.contains("https://example.invalid/banner.png"));
    }
    void mixedHtmlImageBannersDoNotEraseMarkdownText() {
        const auto html = renderDescription(
            "![Header](https://example.invalid/banner.png)\n\n<p align=\"center\"><a "
            "href=\"https://example.invalid\"><img "
            "src=\"https://example.invalid/button.png\"></a></p>\n\n<br>![Divider](https://"
            "example.invalid/"
            "divider.png)\n\n**Readable description**\n\n### Features\n- First feature\n- Second "
            "feature\n\n<table><tr><td>Compatibility</td></tr></table>",
            true);
        QVERIFY(html.contains("Readable description"));
        QVERIFY(html.contains("First feature"));
        QVERIFY(html.contains("Compatibility"));
        QVERIFY(!html.contains("<img"));
        QVERIFY(!html.contains("<table"));
    }
    void descriptionsDiscardProviderPaletteAndEmptyLayout() {
        const auto html = renderDescription(
            "<div style='color:black;background:white'><h2>Visible heading</h2><p><b>Readable "
            "body</b></p><table style='color:black'><tr><td>Table value</td></tr></table><p><img "
            "src='file:///bad.png'></p></div>",
            false);
        QVERIFY(html.contains("Readable body"));
        QVERIFY(html.contains("Table value"));
        QVERIFY(!html.contains("style="));
        QVERIFY(!html.contains("<table"));
        QVERIFY(!html.contains("<img"));
        QVERIFY(!html.contains("<p></p>"));
        QTextDocument doc;
        doc.setHtml(html);
        QVERIFY(doc.toPlainText().contains("Readable body"));
    }

    void staleLookupCannotReplaceNewProject() {
        auto source = std::make_shared<ShowcaseFixture>();
        ProjectShowcase showcase(source, std::make_shared<IdentityFixture>());
        showcase.open({}, {{"relativePath", "mods/first.jar"}});
        QTRY_COMPARE(source->pending.size(), 1);
        showcase.open({}, {{"relativePath", "mods/second.jar"}});
        QTRY_COMPARE(source->pending.size(), 2);
        Project project;
        project.id = "old";
        project.title = "Old";
        project.provider = "modrinth";
        source->pending.at(0)(project, {});
        QVERIFY(!source->detail);
        project.id = "new";
        project.title = "New";
        source->pending.at(1)(project, {});
        QVERIFY(source->detail);
        QCOMPARE(showcase.project().value("title").toString(), QString("New"));
        ProjectDetail detail;
        detail.description = "# The actual project\n\n[Website](https://example.invalid/project)\n\n![Inline "
                             "image](file:///unavailable/image.png)";
        Release cf;
        cf.provider = "curseforge";
        cf.id = "cf";
        cf.projectId = "project";
        cf.version = "1.0";
        cf.filename = "forge.jar";
        Release mr = cf;
        mr.provider = "modrinth";
        mr.id = "mr";
        mr.filename = "fabric.jar";
        detail.releases = {cf, mr};
        source->detail(detail);
        QTRY_VERIFY(!showcase.busy());
        QCOMPARE(showcase.versions().size(), 1);
        const auto variants = showcase.versions().first().toMap().value("variants").toList();
        QCOMPARE(variants.size(), 2);
        QCOMPARE(variants.first().toMap().value("provider").toString(), QString("modrinth"));
        QVERIFY(showcase.description().contains("The actual project"));
        QVERIFY(showcase.description().contains("<h1"));
        QVERIFY(!showcase.description().contains("<img"));
        showcase.close();
        source->detail(detail);
        QVERIFY(showcase.versions().isEmpty());
    }
    void unknownProjectAndProviderErrorsStayVisible() {
        auto source = std::make_shared<ShowcaseFixture>();
        ProjectShowcase showcase(source, std::make_shared<IdentityFixture>());
        showcase.open({}, {{"relativePath", "mods/local.jar"}});
        QTRY_COMPARE(source->pending.size(), 1);
        source->pending.first()({}, "Provider unavailable");
        QVERIFY(!showcase.busy());
        QCOMPARE(showcase.notice(), QString("Provider unavailable"));
    }
    void worldLaunchValidatesFolderAndEncodesExactTarget() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString name = QString::fromUtf8("C++ & world ? # \xc3\xa9");
        // Windows does not allow ? in folder names; URL encoding is tested separately below.
        const QString folder = QString::fromUtf8("C++ & world \xc3\xa9");
        write(temp.path() + "/saves/" + folder + "/level.dat", "world fixture");
        auto opener = std::make_shared<LaunchFixture>();
        auto adapter = std::make_shared<ModrinthWorldLaunchAdapter>();
        WorldLaunch launch({adapter}, opener, std::make_shared<NativePackFileStore>());
        QVariantMap pack{{"path", temp.path()},
                         {"provider", "modrinth"},
                         {"minecraft", "1.21.1"},
                         {"launcherInstanceId", "local:example/id"}};
        launch.setPack(pack);
        QVERIFY(launch.available());
        launch.play("saves/" + folder);
        QCOMPARE(opener->requested.scheme(), QString("modrinth"));
        QCOMPARE(QUrlQuery(opener->requested).queryItemValue("singleplayer_world", QUrl::FullyDecoded),
                 folder);
        QVERIFY(opener->requested.toEncoded().contains("local%3Aexample%2Fid"));
        const auto special = adapter->worldUrl(pack, name);
        QCOMPARE(QUrlQuery(special).queryItemValue("singleplayer_world", QUrl::FullyDecoded), name);
        opener->requested = QUrl{};
        launch.play("saves/../elsewhere");
        QVERIFY(opener->requested.isEmpty());
        write(temp.path() + "/saves/empty/README.txt", "not a world");
        launch.play("saves/empty");
        QVERIFY(opener->requested.isEmpty());
        pack["minecraft"] = "1.19.4";
        launch.setPack(pack);
        QVERIFY(!launch.available());
        pack["minecraft"] = "unknown";
        launch.setPack(pack);
        QVERIFY(!launch.available());
        pack["provider"] = "curseforge";
        launch.setPack(pack);
        QVERIFY(!launch.available());
    }
    void identityRequiresTheInstalledBytes() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        write(temp.path() + "/mods/file.jar", "actual bytes");
        NativeInstalledModIdentity identity(std::make_shared<NativePackFileStore>());
        const auto hash =
            QString::fromLatin1(QCryptographicHash::hash("actual bytes", QCryptographicHash::Sha1).toHex());
        const QVariantMap row{{"relativePath", "mods/file.jar"}, {"category", "mods"}};
        QVariantMap pack{{"path", temp.path()},
                         {"mods", QVariantList{QVariantMap{{"installPath", "mods/file.jar"},
                                                           {"provider", "modrinth"},
                                                           {"projectId", "wrong"},
                                                           {"sha1", "stale"}}}}};
        auto result = identity.inspect(pack, row);
        QCOMPARE(result.sha1, hash);
        QVERIFY(result.sourceIds.isEmpty());
        pack["mods"] = QVariantList{QVariantMap{{"installPath", "mods/file.jar"},
                                                {"provider", "modrinth"},
                                                {"projectId", "right"},
                                                {"sha1", hash}}};
        result = identity.inspect(pack, row);
        QCOMPARE(result.sourceIds.value("modrinth"), QString("right"));
        QVERIFY(
            !identity.inspect(pack, {{"relativePath", "../file.jar"}, {"category", "mods"}}).error.isEmpty());
    }
};
QTEST_MAIN(ShowcaseTests)
#include "ShowcaseTests.moc"
