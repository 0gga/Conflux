#include "features/showcase/ProjectShowcase.h"
#include "features/showcase/VersionChangelogs.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)
using namespace studio;
class ChangelogSource final : public IProjectShowcaseSource {
  public:
    QList<ChangelogCallback> pending;
    QStringList calls;
    QVector<Release> releases;
    bool automatic = true;
    void loadChangelog(const Release &release, ChangelogCallback callback) override {
        calls << release.provider + ':' + release.id;
        if (automatic)
            callback(release.changelog, {});
        else
            pending << std::move(callback);
    }
    void resolve(const InstalledModIdentity &, ResolveCallback callback) override {
        Project p;
        p.provider = "modrinth";
        p.id = "project";
        callback(p, {});
    }
    void load(const Project &, const QString &, const QString &, DetailCallback callback) override {
        ProjectDetail detail;
        detail.releases = releases;
        callback(detail);
    }
    void setCurseforgeKey(const QByteArray &) override {}
};
class VersionChangelogTests : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    static Release release(const QString &id, const QString &version, const QString &loader = "fabric",
                           const QString &game = "1.21.1") {
        Release r;
        r.provider = "modrinth";
        r.projectId = "project";
        r.id = id;
        r.version = version;
        r.loaders = {loader};
        r.gameVersions = {game};
        r.published = "2026-10-21";
        r.changelog = "# Changes\n\nFixed something.";
        return r;
    }
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
        qputenv("CONFLUX_STYLE_SETTINGS_DIR", settings.path().toUtf8());
    }
    void groupsPlatformAndGameVariants_data() {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<QString>("expected");
        QTest::newRow("exact") << "2.0.0" << "2.0.0" << "2.0.0";
        QTest::newRow("suffix") << "2.0.0-fabric+mc1.21.1" << "2.0.0-neoforge+mc1.20.1" << "2.0.0";
        QTest::newRow("prefix") << "fabric-2.0.0-1.21.1" << "neoforge-2.0.0-1.20.1" << "2.0.0";
        QTest::newRow("brackets") << "2.0.0 (fabric) [1.21.1]" << "2.0.0 (neoforge) [1.20.1]" << "2.0.0";
        QTest::newRow("prerelease") << "2.0.0-beta.1+mc1.21.1-fabric" << "2.0.0-beta.1+mc1.20.1-neoforge"
                                    << "2.0.0-beta.1";
        QTest::newRow("version-equals-game") << "1.21.1-fabric" << "1.21.1-neoforge" << "1.21.1";
        QTest::newRow("v-prefix") << "v2.0.0-fabric" << "2.0.0-neoforge" << "2.0.0";
    }
    void groupsPlatformAndGameVariants() {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(QString, expected);
        auto source = std::make_shared<ChangelogSource>();
        VersionChangelogs logs(source);
        auto first = release("a", a), second = release("b", b, "neoforge", "1.20.1");
        logs.setReleases({first, second});
        QCOMPARE(logs.rowCount(), 1);
        QCOMPARE(logs.versions().first().toMap().value("version").toString(), expected);
        logs.select(second.toVariant());
        QTRY_VERIFY(!logs.selectedBusy());
        QCOMPARE(logs.selectedIndex(), 0);
        QCOMPARE(source->calls.size(), 1);
        logs.select(first.toVariant());
        QCOMPARE(source->calls.size(), 1);
        QVERIFY(!logs.selectedBlocks().isEmpty());
        QCOMPARE(first.version, a);
    }
    void differentContentVersionsKeepIdenticalBodies() {
        auto source = std::make_shared<ChangelogSource>();
        VersionChangelogs logs(source);
        logs.setReleases({release("a", "2.0.0"), release("b", "2.0.1"), release("c", "2.0.0-beta.1")});
        QCOMPARE(logs.rowCount(), 3);
        QVERIFY(source->calls.isEmpty());
        logs.request(0);
        logs.request(1);
        logs.request(2);
        QTRY_COMPARE(source->calls.size(), 3);
        QTRY_VERIFY(logs.data(logs.index(2), Qt::UserRole + 1).toMap().value("loaded").toBool());
    }
    void snapshotGameLabelsShareTheirContentVersion() {
        auto source = std::make_shared<ChangelogSource>();
        VersionChangelogs logs(source);
        logs.setReleases({release("snapshot", "0.164.0+26.4", "fabric", "26.4-snapshot-4"),
                          release("stable", "0.164.0+26.3", "fabric", "26.3"),
                          release("next", "0.164.1+26.4", "fabric", "26.4-snapshot-5")});
        QCOMPARE(logs.rowCount(), 2);
        QCOMPARE(logs.versions().first().toMap().value("version").toString(), QString("0.164.0"));
        QCOMPARE(logs.versions().last().toMap().value("version").toString(), QString("0.164.1"));
    }
    void prefersModrinthWithinTheVerifiedProjectAndRejectsForeignAliases() {
        auto source = std::make_shared<ChangelogSource>();
        VersionChangelogs logs(source);
        auto mr = release("mr", "2.0"), cf = mr;
        cf.id = "cf";
        cf.provider = "curseforge";
        logs.setReleases({cf, mr});
        QCOMPARE(logs.rowCount(), 1);
        logs.select(cf.toVariant());
        QTRY_VERIFY(!logs.selectedBusy());
        QCOMPARE(source->calls, QStringList{"modrinth:mr"});
        logs.select({{"provider", "curseforge"}, {"id", "mr"}});
        QCOMPARE(source->calls.size(), 1);
    }
    void boundsConcurrentLoadsAndDeduplicatesRequests() {
        auto source = std::make_shared<ChangelogSource>();
        source->automatic = false;
        VersionChangelogs logs(source);
        QVector<Release> releases;
        for (int i = 0; i < 10; ++i)
            releases << release(QString::number(i), QString::number(i));
        logs.setReleases(releases);
        for (int i = 0; i < 10; ++i) {
            logs.request(i);
            logs.request(i);
        }
        QCOMPARE(source->calls.size(), 2);
        for (int i = 0; i < 10; ++i) {
            QTRY_VERIFY(!source->pending.isEmpty());
            auto callback = source->pending.takeFirst();
            callback("# Version", {});
            callback("duplicate", {});
        }
        QTRY_VERIFY(logs.data(logs.index(9), Qt::UserRole + 1).toMap().value("loaded").toBool());
        QCOMPARE(source->calls.size(), 10);
    }
    void failuresArePerVersionAndExplicitlyRetryable() {
        auto source = std::make_shared<ChangelogSource>();
        source->automatic = false;
        VersionChangelogs logs(source);
        logs.setReleases({release("a", "1"), release("b", "2")});
        logs.request(0);
        logs.request(1);
        source->pending.takeFirst()({}, "Unavailable");
        source->pending.takeFirst()("# Good", {});
        QTRY_VERIFY(logs.data(logs.index(1), Qt::UserRole + 1).toMap().value("loaded").toBool());
        QVERIFY(!logs.data(logs.index(0), Qt::UserRole + 1).toMap().value("error").toString().isEmpty());
        logs.request(0);
        QCOMPARE(source->calls.size(), 2);
        logs.request(0, true);
        QCOMPARE(source->calls.size(), 3);
        source->pending.takeFirst()("# Recovered", {});
        QTRY_VERIFY(logs.data(logs.index(0), Qt::UserRole + 1).toMap().value("loaded").toBool());
    }
    void projectChangesDiscardStaleNetworkAndWorkerResults() {
        auto source = std::make_shared<ChangelogSource>();
        source->automatic = false;
        VersionChangelogs logs(source);
        logs.setReleases({release("old", "1")});
        logs.request(0);
        auto stale = source->pending.takeFirst();
        logs.setReleases({release("new", "2")});
        logs.request(0);
        stale("# Wrong project", {});
        source->pending.takeFirst()("# Current project", {});
        QTRY_VERIFY(logs.data(logs.index(0), Qt::UserRole + 1).toMap().value("loaded").toBool());
        QCOMPARE(logs.versions().first().toMap().value("id").toString(), QString("new"));
        logs.clear();
        QCOMPARE(logs.rowCount(), 0);
        QCOMPARE(logs.selectedIndex(), -1);
    }
    void allHistoryRendersAsAVirtualizedDividedList() {
        auto source = std::make_shared<ChangelogSource>();
        for (int i = 200; i > 0; --i)
            source->releases << release(QString::number(i), QString::number(i));
        ProjectShowcase showcase(source, {});
        showcase.openProject({}, {{"provider", "modrinth"}, {"id", "project"}});
        QTRY_VERIFY(!showcase.busy());
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                                             "/components/ChangelogView.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> item(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"showcase", QVariant::fromValue(&showcase)}})));
        QVERIFY(item);
        QQuickWindow window;
        window.resize(720, 500);
        item->setParentItem(window.contentItem());
        item->setWidth(720);
        item->setHeight(500);
        item->setProperty("mode", "all");
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *history = item->findChild<QQuickItem *>("changelogHistory");
        QVERIFY(history);
        QTRY_COMPARE(history->property("count").toInt(), 200);
        QTRY_VERIFY(source->calls.size() > 1);
        QVERIFY(source->calls.size() < 30);
        QVERIFY(!item->findChild<QObject *>("changelogVersionSelector"));
        QList<QQuickItem *> dividers;
        std::function<void(QQuickItem *)> findDividers = [&](QQuickItem *parent) {
            for (auto *child : parent->childItems()) {
                if (child->objectName() == "changelogDivider" && child->isVisible())
                    dividers << child;
                findDividers(child);
            }
        };
        findDividers(item.get());
        QVERIFY(!dividers.isEmpty());
        auto *viewport = history->findChild<QQuickItem *>("contentListViewport");
        QVERIFY(viewport);
        QVERIFY(QMetaObject::invokeMethod(viewport, "positionViewAtIndex", Q_ARG(int, 199), Q_ARG(int, 2)));
        QTRY_VERIFY(showcase.changelogModel()
                        ->data(showcase.changelogModel()->index(199, 0, {}), Qt::UserRole + 1)
                        .toMap()
                        .value("loaded")
                        .toBool());
        QVERIFY(source->calls.size() < 50);
        const auto output = qEnvironmentVariable("CONFLUX_UI_ARTIFACTS");
        if (!output.isEmpty()) {
            QDir().mkpath(output);
            QVERIFY(window.grabWindow().save(output + "/changelog-history.png"));
        }
    }
};
QTEST_MAIN(VersionChangelogTests)
#include "VersionChangelogTests.moc"
