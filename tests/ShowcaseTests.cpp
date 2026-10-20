#include "features/showcase/DescriptionRenderer.h"
#include "features/showcase/ProjectShowcase.h"
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
    void resolve(const InstalledModIdentity &, ResolveCallback callback) override {
        pending << std::move(callback);
    }
    void load(const Project &, const QString &, const QString &, DetailCallback callback) override {
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
    void mixedHtmlImageBannersDoNotEraseMarkdownText() {
        const auto html = renderDescription(
            "![Header](https://example.invalid/banner.png)\n\n<p align=\"center\"><a "
            "href=\"https://example.invalid\"><img "
            "src=\"https://example.invalid/button.png\"></a></p>\n\n<br>![Divider](https://example.invalid/"
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
        QVERIFY(!showcase.busy());
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
