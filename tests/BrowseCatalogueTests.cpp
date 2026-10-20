#include "ContentFixtures.h"
#include "features/content/ContentActions.h"
#include "features/explore/BrowseCatalogue.h"
#include "features/preferences/BrowsePreferences.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/BrowseSettingsStore.h"
#include "services/CatalogueClient.h"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
using namespace studio;
class MemoryBrowseSettings final : public IBrowseSettingsStore {
  public:
    QVariantMap data;
    QVariant value(const QString &key, const QVariant &fallback = {}) const override {
        return data.value(key, fallback);
    }
    void save(const QString &key, const QVariant &value) override {
        data[key] = value;
    }
};
class SearchFixture final : public ICatalogueSearch {
  public:
    struct Call {
        CatalogueQuery query;
        int offset, limit;
        Callback callback;
    };
    QMap<QString, QVector<Project>> rows;
    QList<Call> calls;
    bool delayed = false;
    void fetch(const CatalogueQuery &query, int offset, int limit, Callback callback) override {
        calls << Call{query, offset, limit, callback};
        if (!delayed)
            deliver(calls.size() - 1);
    }
    void deliver(int index) {
        const auto call = calls[index];
        auto projects = rows.value(call.query.provider);
        if (!call.query.projectIds.isEmpty()) {
            QVector<Project> filtered;
            for (const auto &p : projects)
                if (call.query.projectIds.contains(p.id))
                    filtered << p;
            projects = filtered;
        }
        call.callback({projects.mid(call.offset, call.limit), projects.size(), {}});
    }
    void facets(std::function<void(QVariantMap, QString)> callback) override {
        callback({{"versions", QStringList{"1.21.1", "1.20.1"}}}, {});
    }
    void setKey(const QByteArray &) override {}
};
class BrowseCatalogueTests final : public QObject {
    Q_OBJECT
    static Project project(int index, const QString &provider = "modrinth") {
        Project p;
        p.provider = provider;
        p.id = QString::number(index);
        p.title = provider + " " + p.id;
        p.sourceIds[provider] = p.id;
        p.downloads = 1000 - index;
        return p;
    }
    static std::shared_ptr<SearchFixture> source(int count = 75) {
        auto fixture = std::make_shared<SearchFixture>();
        for (int i = 0; i < count; ++i)
            fixture->rows["modrinth"] << project(i);
        return fixture;
    }
  private slots:
    void providerRequestsEncodeOrWithinCompatibilityGroupsAndRealOffsets() {
        CatalogueQuery q;
        q.versions = {"1.21.1", "1.20.1"};
        q.loaders = {"neoforge", "fabric"};
        q.tags = {"mr:optimization", "mr:technology"};
        q.sort = "updated";
        const QUrlQuery params(CatalogueClient::searchUrl(q, 40, 20));
        const auto facets = QJsonDocument::fromJson(params.queryItemValue("facets").toUtf8()).array();
        QCOMPARE(facets[1].toArray(), QJsonArray({"versions:1.21.1", "versions:1.20.1"}));
        QCOMPARE(facets[2].toArray(), QJsonArray({"categories:neoforge", "categories:fabric"}));
        QCOMPARE(facets[3].toArray(), QJsonArray({"categories:optimization", "categories:technology"}));
        QCOMPARE(params.queryItemValue("offset"), QString("40"));
        QCOMPARE(params.queryItemValue("limit"), QString("20"));
        QCOMPARE(params.queryItemValue("index"), QString("updated"));
        q.provider = "curseforge";
        q.tags = {"cf:123", "cf:456"};
        const QUrlQuery cf(CatalogueClient::searchUrl(q, 40, 20));
        QCOMPARE(QJsonDocument::fromJson(cf.queryItemValue("modLoaderTypes").toUtf8()).array(),
                 QJsonArray({6, 4}));
        QCOMPARE(QJsonDocument::fromJson(cf.queryItemValue("gameVersions").toUtf8()).array(),
                 QJsonArray({"1.21.1", "1.20.1"}));
        QCOMPARE(cf.queryItemValue("index"), QString("40"));
        QCOMPARE(cf.queryItemValue("pageSize"), QString("20"));
    }
    void pageAndScrollRestoreWithoutFetchingTheSamePageAgain() {
        auto api = source();
        auto store = std::make_shared<MemoryBrowseSettings>();
        auto visibility = std::make_shared<ContentVisibility>(store);
        BrowseCatalogue model(api, visibility);
        model.setContext("a", {"1.21.1"}, {"neoforge"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 20);
        QCOMPARE(model.page(), 1);
        model.next();
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.page(), 2);
        QCOMPARE(model.projects().first().toMap().value("id").toString(), QString("20"));
        model.setScrollPosition(187);
        model.setContext("b", {"1.20.1"}, {"fabric"});
        QTRY_VERIFY(!model.busy());
        const auto requests = api->calls.size();
        model.setContext("a", {"1.21.1"}, {"neoforge"});
        QCOMPARE(model.page(), 2);
        QCOMPARE(model.scrollPosition(), 187.0);
        QCOMPARE(api->calls.size(), requests);
        QCOMPARE(model.projects().first().toMap().value("id").toString(), QString("20"));
        model.next();
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().first().toMap().value("id").toString(), QString("40"));
    }
    void pendingInstanceMetadataDoesNotReplaceTheNewQueryWithAnotherContext() {
        auto api = source();
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(api, visibility);
        model.setContext("a", {"1.21.1"}, {"fabric"});
        QTRY_VERIFY(!model.busy());
        model.configure({{"query", "old query"}});
        QTRY_VERIFY(!model.busy());
        model.setContext("b", {}, {});
        QTRY_VERIFY(!model.busy());
        model.configure({{"type", "resourcepack"}, {"query", "new query"}});
        QTRY_VERIFY(!model.busy());
        model.setContext("b", {"1.21.1"}, {"neoforge"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.filters().value("query").toString(), QString("new query"));
        QCOMPARE(model.filters().value("type").toString(), QString("resourcepack"));
        QCOMPARE(model.filters().value("versions").toStringList(), QStringList{"1.21.1"});
    }
    void oldResponsesCannotReplaceNewFilters() {
        auto api = source();
        api->delayed = true;
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(api, visibility);
        model.setContext("a", {}, {});
        QCOMPARE(api->calls.size(), 1);
        model.configure({{"query", "new query"}});
        QCOMPARE(api->calls.size(), 2);
        api->deliver(0);
        QTest::qWait(5);
        QVERIFY(model.projects().isEmpty());
        QVERIFY(model.busy());
        api->deliver(1);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.filters().value("query").toString(), QString("new query"));
        QCOMPARE(model.projects().size(), 20);
    }
    void mergedPagesHydrateModrinthPriorityAndNeverRepeatAnIdentity() {
        auto api = source(60);
        auto pair = project(59);
        pair.downloads = 1;
        pair.title = "Preferred Modrinth title";
        api->rows["modrinth"][59] = pair;
        auto cf = project(90, "curseforge");
        cf.downloads = 999999;
        api->rows["curseforge"] = {cf};
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(api, visibility, {{"modrinth:59", "shared"}, {"curseforge:90", "shared"}});
        model.setKey("fixture key");
        model.configure({{"sort", "downloads"}});
        QTRY_VERIFY(!model.busy());
        const auto first = model.projects().first().toMap();
        QCOMPARE(first.value("title").toString(), QString("Preferred Modrinth title"));
        QCOMPARE(first.value("provider").toString(), QString("modrinth"));
        QCOMPARE(first.value("downloadStats").toList().size(), 2);
        QSet<QString> ids;
        for (int page = 0; page < 4; ++page) {
            for (const auto &value : model.projects()) {
                const auto id = value.toMap().value("id").toString();
                QVERIFY(!ids.contains(id));
                ids.insert(id);
            }
            if (!model.hasNext())
                break;
            model.next();
            QTRY_VERIFY(!model.busy());
        }
        QCOMPARE(ids.size(), 60);
    }
    void hiddenAndInstalledFiltersFillPagesAndCanRestoreContent() {
        auto api = source(45);
        auto store = std::make_shared<MemoryBrowseSettings>();
        auto visibility = std::make_shared<ContentVisibility>(store);
        BrowseCatalogue model(api, visibility);
        connect(visibility.get(), &ContentVisibility::changed, &model, &BrowseCatalogue::refresh);
        model.setContext("a", {}, {});
        QTRY_VERIFY(!model.busy());
        const auto hidden = model.projects().first().toMap();
        model.hide(hidden);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 20);
        QCOMPARE(model.projects().first().toMap().value("id").toString(), QString("1"));
        model.configure({{"hidden", true}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 1);
        model.hide(model.projects().first().toMap(), false);
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.projects().isEmpty());
        model.setInstalled(
            {QVariantMap{{"sourceIds", QVariantMap{{"modrinth", "0"}}}, {"category", "mods"}},
             QVariantMap{{"sourceIds", QVariantMap{{"modrinth", "1"}}}, {"category", "mods"}}});
        model.configure({{"hidden", false}, {"hideInstalled", true}, {"size", 10}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 10);
        QCOMPARE(model.projects().first().toMap().value("id").toString(), QString("2"));
        model.configure({{"unlocked", true}, {"versions", QStringList{"1.20.1", "1.21.1"}}});
        QTRY_VERIFY(!model.busy());
        model.configure({{"unlocked", false}});
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.filters().value("versions").toStringList().isEmpty());
    }
    void preferencesPersistAndHidingDoesNotChangeInstalledFiles() {
        QTemporaryDir temp;
        const auto file = temp.path() + "/browse.ini";
        auto store = std::make_shared<BrowseSettingsStore>(file);
        BrowsePreferences prefs(store);
        prefs.setConnectorFabric(false);
        auto visibility = std::make_shared<ContentVisibility>(store);
        QVariantMap row{{"relativePath", "mods/example.jar"},
                        {"category", "mods"},
                        {"sha1", "hash"},
                        {"sourceIds", QVariantMap{{"modrinth", "project"}}},
                        {"enabled", true},
                        {"title", "Example"}};
        auto installer = std::make_shared<ContentInstallerFixture>();
        ContentActions content(std::make_shared<ContentFileFixture>(),
                               std::make_shared<ContentCatalogueFixture>(), installer,
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/content.ini"));
        content.setVisibility(visibility);
        content.setContext({{"path", "pack"}}, {row});
        QTRY_VERIFY(!content.busy());
        QCOMPARE(content.rows().size(), 1);
        content.hide("mods/example.jar");
        QCOMPARE(content.rows().size(), 0);
        QCOMPARE(content.inventory().first().toMap().value("enabled").toBool(), true);
        QVERIFY(installer->requested.isEmpty());
        content.setFilter("hidden", true);
        QCOMPARE(content.rows().size(), 1);
        BrowsePreferences reloaded(std::make_shared<BrowseSettingsStore>(file));
        QVERIFY(!reloaded.connectorFabric());
        ContentVisibility restored(std::make_shared<BrowseSettingsStore>(file));
        QVERIFY(restored.hidden({{"provider", "modrinth"}, {"id", "project"}}));
        restored.setHidden(row, false, "pack");
        QVERIFY(!restored.hidden(row, "pack"));
        QCOMPARE(row.value("enabled").toBool(), true);
    }
};
QTEST_GUILESS_MAIN(BrowseCatalogueTests)
#include "BrowseCatalogueTests.moc"
