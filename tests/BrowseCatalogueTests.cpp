#include "ContentFixtures.h"
#include "features/content/ContentActions.h"
#include "features/explore/BrowseCatalogue.h"
#include "features/explore/TolerantCatalogueSearch.h"
#include "features/preferences/BrowsePreferences.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/BrowseSettingsStore.h"
#include "services/CatalogueClient.h"
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <QtTest>
#include <algorithm>
#include <limits>
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
    bool delayedFacets = false;
    QList<std::function<void(QVariantMap, QString)>> facetCalls;
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
        if (call.query.sort == "downloads")
            std::stable_sort(projects.begin(), projects.end(),
                             [](const Project &a, const Project &b) { return a.downloads > b.downloads; });
        call.callback({projects.mid(call.offset, call.limit), projects.size(), {}});
    }
    void facets(std::function<void(QVariantMap, QString)> callback) override {
        facetCalls << callback;
        if (!delayedFacets)
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
    void credentialChangesInvalidatePagesAndIgnoreOutdatedFacets() {
        auto fixture = source(3);
        fixture->delayedFacets = true;
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(fixture, visibility);
        model.setContext("pack", {}, {});
        QTRY_VERIFY(!model.busy());
        model.loadOptions();
        QCOMPARE(fixture->facetCalls.size(), 1);
        model.setKey("fixture-one");
        QTRY_VERIFY(!model.busy());
        model.setKey("fixture-two");
        QTRY_VERIFY(!model.busy());
        QCOMPARE(fixture->facetCalls.size(), 3);
        fixture->facetCalls[2]({{"versions", QStringList{"current"}}}, {});
        fixture->facetCalls[0]({{"versions", QStringList{"stale"}}}, "obsolete error");
        fixture->facetCalls[1]({{"versions", QStringList{"stale"}}}, {});
        QCOMPARE(model.options().value("versions").toStringList(), QStringList{"current"});
        QVERIFY(model.notice().isEmpty());
        const auto calls = fixture->calls.size();
        model.setKey("fixture-two");
        QCOMPARE(fixture->calls.size(), calls);
        model.setKey({});
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.options().isEmpty());
        QCOMPARE(fixture->facetCalls.size(), 4);
        for (int index = calls; index < fixture->calls.size(); ++index)
            QCOMPARE(fixture->calls[index].query.provider, QString("modrinth"));
    }
    void facetFailuresRemainVisibleAndRefreshCanRetry() {
        auto fixture = source(3);
        fixture->delayedFacets = true;
        BrowseCatalogue model(fixture,
                              std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>()));
        model.setContext("pack", {}, {});
        QTRY_VERIFY(!model.busy());
        model.loadOptions();
        model.loadOptions();
        QCOMPARE(fixture->facetCalls.size(), 1);
        fixture->facetCalls[0]({}, "Facet service unavailable");
        QCOMPARE(model.notice(), QString("Facet service unavailable"));
        model.refresh();
        QCOMPARE(fixture->facetCalls.size(), 2);
        fixture->facetCalls[1]({{"versions", QStringList{"1.21.1"}}}, {});
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.notice().isEmpty());
        QCOMPARE(model.options().value("versions").toStringList(), QStringList{"1.21.1"});
    }
    void callbacksAfterCatalogueDestructionAreHarmless() {
        auto fixture = source(3);
        fixture->delayed = true;
        fixture->delayedFacets = true;
        auto model = std::make_unique<BrowseCatalogue>(
            fixture, std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>()));
        model->setContext("pack", {}, {});
        model->loadOptions();
        model.reset();
        fixture->deliver(0);
        fixture->facetCalls[0]({}, "late error");
        QCoreApplication::processEvents();
        QCOMPARE(fixture->calls.size(), 1);
    }
    void filteringThousandsOfInstalledItemsHasABoundedReadBudgetAndYieldsToEvents() {
        auto fixture = source(1500);
        BrowseCatalogue model(fixture,
                              std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>()));
        model.setContext("large-pack", {}, {});
        QTRY_VERIFY(!model.busy());
        QVariantList installed;
        for (int i = 0; i < 1200; ++i)
            installed << QVariantMap{{"sourceIds", QVariantMap{{"modrinth", QString::number(i)}}},
                                     {"category", "mods"}};
        model.setInstalled(installed);
        int heartbeats = 0;
        QTimer::singleShot(0, &model, [&] { ++heartbeats; });
        const auto before = fixture->calls.size();
        QElapsedTimer elapsed;
        elapsed.start();
        model.configure({{"hideInstalled", true}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(fixture->calls.size() - before, 24);
        QVERIFY(model.hasNext());
        QVERIFY(heartbeats > 0);
        QVERIFY(model.projects().isEmpty());
        QVERIFY(!model.notice().isEmpty());
        model.next();
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 20);
        QCOMPARE(model.projects().first().toMap().value("id").toString(), QString("1200"));
        QVERIFY2(elapsed.elapsed() < 5000, "Installed filtering exceeded its UI responsiveness budget");
    }
    void providerFailuresAreVisibleAndAnExplicitRefreshRecovers() {
        auto fixture = source(3);
        fixture->delayed = true;
        BrowseCatalogue model(fixture,
                              std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>()));
        model.setContext("pack", {}, {});
        fixture->calls[0].callback({{}, 0, "Provider unavailable"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.notice(), QString("Provider unavailable"));
        QVERIFY(model.projects().isEmpty());
        QVERIFY(!model.hasNext());
        model.refresh();
        fixture->deliver(1);
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.notice().isEmpty());
        QCOMPARE(model.projects().size(), 3);
    }
    void mergedSortOrdersRespectProviderDatesAndFollowers_data() {
        QTest::addColumn<QString>("sort");
        for (const auto &sort : {"follows", "updated", "newest"})
            QTest::newRow(sort) << QString(sort);
    }
    void mergedSortOrdersRespectProviderDatesAndFollowers() {
        QFETCH(QString, sort);
        auto fixture = source(1);
        auto cf = project(2, "curseforge");
        cf.followers = 500;
        cf.updated = "2026-10-22T12:00:00Z";
        cf.created = "2026-10-22T12:00:00Z";
        fixture->rows["curseforge"] = {cf};
        fixture->rows["modrinth"][0].followers = 1;
        fixture->rows["modrinth"][0].updated = "2020-10-22T12:00:00Z";
        fixture->rows["modrinth"][0].created = "2020-10-22T12:00:00Z";
        BrowseCatalogue model(fixture,
                              std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>()));
        model.setKey("fixture");
        model.setContext("pack", {}, {});
        model.configure({{"sort", sort}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().first().toMap().value("provider").toString(), QString("curseforge"));
        QCOMPARE(model.notice().isEmpty(), sort != "follows");
    }
    void curseforgeOnlyViewHydratesModrinthPriority_data() {
        QTest::addColumn<QString>("reply");
        for (const auto &reply : {"success", "empty", "error", "stale", "destroyed"})
            QTest::newRow(reply) << QString(reply);
    }
    void curseforgeOnlyViewHydratesModrinthPriority() {
        QFETCH(QString, reply);
        auto fixture = source(1);
        fixture->rows["curseforge"] = {project(10, "curseforge")};
        const QHash<QString, QString> ids{{"modrinth:0", "pair"}, {"curseforge:10", "pair"}};
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        auto model = std::make_unique<BrowseCatalogue>(fixture, visibility, ids);
        model->setKey("fixture");
        model->setContext("pack", {}, {});
        QTRY_VERIFY(!model->busy());
        fixture->delayed = true;
        model->configure({{"source", "curseforge"}});
        const int base = fixture->calls.size() - 1;
        fixture->deliver(base);
        QTRY_COMPARE(fixture->calls.size(), base + 2);
        const auto hydration = fixture->calls.last();
        QCOMPARE(hydration.query.projectIds, QStringList{"0"});
        if (reply == "destroyed") {
            model.reset();
            fixture->deliver(base + 1);
            QCoreApplication::processEvents();
            return;
        }
        if (reply == "stale") {
            model->configure({{"query", "new query"}});
            fixture->deliver(base + 1);
            QVERIFY(model->busy());
            fixture->deliver(base + 2);
            QTRY_COMPARE(fixture->calls.size(), base + 4);
            fixture->deliver(base + 3);
            QTRY_VERIFY(!model->busy());
            return;
        }
        if (reply == "success")
            fixture->deliver(base + 1);
        else
            hydration.callback({{}, 0, reply == "error" ? "metadata offline" : QString{}});
        QTRY_VERIFY(!model->busy());
        QCOMPARE(model->projects().size(), 1);
        QCOMPARE(model->projects().first().toMap().value("provider").toString(),
                 reply == "success" ? "modrinth" : "curseforge");
        QCOMPARE(model->notice().isEmpty(), reply != "error");
        if (reply == "error")
            QVERIFY(model->notice().contains("metadata offline"));
    }
    void downloadHydrationErrorsAndObsoleteRepliesDoNotBreakNewQueries_data() {
        QTest::addColumn<bool>("stale");
        QTest::newRow("error") << false;
        QTest::newRow("stale") << true;
    }
    void downloadHydrationErrorsAndObsoleteRepliesDoNotBreakNewQueries() {
        QFETCH(bool, stale);
        auto fixture = source(1);
        fixture->rows["curseforge"] = {project(10, "curseforge")};
        const QHash<QString, QString> ids{{"modrinth:0", "pair"}, {"curseforge:10", "pair"}};
        BrowseCatalogue model(
            fixture, std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>()), ids);
        model.setKey("fixture");
        model.setContext("pack", {}, {});
        QTRY_VERIFY(!model.busy());
        fixture->delayed = true;
        model.configure({{"sort", "downloads"}, {"source", "modrinth"}});
        const int base = fixture->calls.size() - 1;
        fixture->deliver(base);
        QTRY_COMPARE(fixture->calls.size(), base + 2);
        const auto hydration = fixture->calls.last();
        QCOMPARE(hydration.query.provider, QString("curseforge"));
        if (stale) {
            model.configure({{"sort", "relevance"}});
            hydration.callback({{}, 0, "obsolete"});
            QVERIFY(model.notice().isEmpty());
            fixture->deliver(base + 2);
        } else
            hydration.callback({{}, 0, "counterpart unavailable"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 1);
        QCOMPARE(model.notice().isEmpty(), stale);
    }
    void exhaustedFilteredLastPageReturnsToTheLastNonemptyPage() {
        auto fixture = source(60);
        BrowseCatalogue model(fixture,
                              std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>()));
        model.setContext("pack", {}, {});
        QTRY_VERIFY(!model.busy());
        QVariantList installed;
        for (int i = 20; i < 60; ++i)
            installed << QVariantMap{{"sourceIds", QVariantMap{{"modrinth", QString::number(i)}}},
                                     {"category", "mods"}};
        model.setInstalled(installed);
        model.configure({{"hideInstalled", true}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 20);
        QVERIFY(model.hasNext());
        model.next();
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.page(), 1);
        QCOMPARE(model.projects().size(), 20);
        QVERIFY(!model.hasNext());
        QCOMPARE(model.pageCount(), 1);
        const auto calls = fixture->calls.size();
        model.next();
        model.previous();
        model.goToPage(1);
        model.setInstalled(installed);
        QCOMPARE(fixture->calls.size(), calls);
    }
    void nativeJumpHandlesProviderBecomingCompletelyEmpty() {
        auto fixture = source(60);
        BrowseCatalogue model(fixture,
                              std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>()));
        model.setContext("pack", {}, {});
        QTRY_VERIFY(!model.busy());
        fixture->rows.clear();
        model.goToPage(3);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.page(), 1);
        QCOMPARE(model.pageCount(), 1);
        QVERIFY(model.projects().isEmpty());
        QVERIFY(!model.hasNext());
    }
    void bufferedFinalProviderRowsKeepNextAvailableWithoutFetchingAgain() {
        auto fixture = source(75);
        BrowseCatalogue model(fixture,
                              std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>()));
        model.setContext("pack", {}, {});
        model.goToPage(4);
        QCOMPARE(model.page(), 1); // busy navigation is a no-op
        QTRY_VERIFY(!model.busy());
        model.next();
        QTRY_VERIFY(!model.busy());
        model.next();
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.page(), 3);
        QVERIFY(model.hasNext());
        const int before = fixture->calls.size();
        model.next();
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 15);
        QCOMPARE(fixture->calls.size(), before);
        QVERIFY(!model.hasNext());
    }
    void hiddenInstalledCombinations_data() {
        QTest::addColumn<bool>("hidden");
        QTest::addColumn<bool>("hideInstalled");
        for (bool hidden : {false, true})
            for (bool hide : {false, true})
                QTest::newRow(qPrintable(QString("hidden%1-installed%2").arg(hidden).arg(hide)))
                    << hidden << hide;
    }
    void hiddenInstalledCombinations() {
        QFETCH(bool, hidden);
        QFETCH(bool, hideInstalled);
        auto fixture = source(4);
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        visibility->setHidden(project(0).toVariant(), true);
        visibility->setHidden(project(1).toVariant(), true);
        BrowseCatalogue model(fixture, visibility);
        model.setContext("pack", {}, {});
        model.setInstalled(
            {QVariantMap{{"sourceIds", QVariantMap{{"modrinth", "0"}}}, {"category", "mods"}},
             QVariantMap{{"sourceIds", QVariantMap{{"modrinth", "2"}}}, {"category", "mods"}}});
        model.configure({{"hidden", hidden}, {"hideInstalled", hideInstalled}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.hideInstalledActive(), hideInstalled && !hidden);
        QCOMPARE(model.projects().size(), hidden || !hideInstalled ? 2 : 1);
        QCOMPARE(model.projects().first().toMap().value("id").toString(), hidden          ? "0"
                                                                          : hideInstalled ? "3"
                                                                                          : "2");
    }
    void cachedTypesUseCurrentInventoryInsteadOfRestoringAnOldInventory() {
        auto fixture = source(3);
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(fixture, visibility);
        model.setContext("pack", {}, {});
        model.configure({{"hideInstalled", true}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 3);
        model.configure({{"type", "resourcepack"}});
        QTRY_VERIFY(!model.busy());
        model.setInstalled(
            {QVariantMap{{"sourceIds", QVariantMap{{"modrinth", "0"}}}, {"category", "mods"}}});
        model.configure({{"type", "mod"}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 2);
        QCOMPARE(model.projects().first().toMap().value("id").toString(), QString("1"));
        model.setContext("other", {}, {});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 3);
        model.setContext("pack", {}, {});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 2);
    }
    void hiddenChangesInvalidateOtherCachedTypesAndContexts() {
        auto fixture = source(3);
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(fixture, visibility);
        connect(visibility.get(), &ContentVisibility::changed, &model, &BrowseCatalogue::refresh);
        model.setContext("pack", {}, {});
        QTRY_VERIFY(!model.busy());
        model.setContext("other", {}, {});
        QTRY_VERIFY(!model.busy());
        model.hide(project(0).toVariant());
        QTRY_VERIFY(!model.busy());
        model.setContext("pack", {}, {});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 2);
        model.configure({{"type", "resourcepack"}});
        QTRY_VERIFY(!model.busy());
        model.hide(project(1).toVariant());
        QTRY_VERIFY(!model.busy());
        model.configure({{"type", "mod"}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 1);
    }
    void typeAndFiltersStartOnlyOneFinalRequest() {
        auto fixture = source(5);
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(fixture, visibility);
        model.setContext("pack", {}, {});
        QTRY_VERIFY(!model.busy());
        fixture->delayed = true;
        const int before = fixture->calls.size();
        model.configure({{"type", "resourcepack"}, {"query", "final"}, {"source", "modrinth"}, {"size", 10}});
        QCOMPARE(fixture->calls.size(), before + 1);
        QCOMPARE(fixture->calls.last().query.text, QString("final"));
        QCOMPARE(fixture->calls.last().query.type, QString("resourcepack"));
        fixture->deliver(before);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(fixture->calls.size(), before + 1);
    }
    void queuedPumpCannotDuplicateNewGenerationRequest() {
        auto fixture = source(5);
        fixture->delayed = true;
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(fixture, visibility);
        model.setContext("pack", {}, {});
        fixture->deliver(0);                 // queues old pump
        model.configure({{"hidden", true}}); // no hidden identities, no fetch
        model.configure({{"hidden", false}, {"query", "new"}});
        QCOMPARE(fixture->calls.size(), 2);
        QCoreApplication::processEvents();
        QCOMPARE(fixture->calls.size(), 2); // old timer must not launch another offset zero
        fixture->deliver(1);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 5);
    }
    void returningToInFlightViewRestartsSafelyAndIgnoresOldReplies() {
        auto fixture = source(3);
        fixture->delayed = true;
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(fixture, visibility);
        model.setContext("first", {}, {});
        model.setContext("second", {}, {});
        model.setContext("first", {}, {});
        QCOMPARE(fixture->calls.size(), 3);
        fixture->deliver(0);
        fixture->deliver(1);
        QVERIFY(model.busy());
        fixture->deliver(2);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 3);
        QCOMPARE(fixture->calls.size(), 3);
    }
    void invalidConfigurationIsAnIdempotentNoOp_data() {
        QTest::addColumn<QVariantMap>("changes");
        QTest::newRow("invalid-size") << QVariantMap{{"size", 17}};
        QTest::newRow("fractional-size") << QVariantMap{{"size", 20.5}};
        QTest::newRow("sort") << QVariantMap{{"sort", "bogus"}};
        QTest::newRow("source") << QVariantMap{{"source", "bogus"}};
        QTest::newRow("type") << QVariantMap{{"type", "bogus"}};
        QTest::newRow("string-bool") << QVariantMap{{"hideInstalled", "false"}};
        QTest::newRow("integer-bool") << QVariantMap{{"hidden", 1}};
        QTest::newRow("unknown") << QVariantMap{{"unknown", "x"}};
        QTest::newRow("unchanged") << QVariantMap{{"query", ""}};
    }
    void invalidConfigurationIsAnIdempotentNoOp() {
        QFETCH(QVariantMap, changes);
        auto fixture = source(2);
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(fixture, visibility);
        model.setContext("pack", {}, {});
        QTRY_VERIFY(!model.busy());
        const auto before = model.filters();
        const int calls = fixture->calls.size();
        QSignalSpy changed(&model, &BrowseCatalogue::changed);
        model.configure(changes);
        QCOMPARE(model.filters(), before);
        QVERIFY(!model.busy());
        QCOMPARE(fixture->calls.size(), calls);
        QCOMPARE(changed.size(), 0);
    }
    void scrollRejectsNonFiniteValuesAndDoesNotRepeatNotifications() {
        BrowseCatalogue model(source(),
                              std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>()));
        QSignalSpy changed(&model, &BrowseCatalogue::scrollChanged);
        model.setScrollPosition(40);
        model.setScrollPosition(40);
        model.setScrollPosition(std::numeric_limits<qreal>::infinity());
        model.setScrollPosition(std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(model.scrollPosition(), qreal(40));
        QCOMPARE(changed.size(), 1);
        model.setScrollPosition(-3);
        QCOMPARE(model.scrollPosition(), qreal(0));
        QCOMPARE(changed.size(), 2);
    }
    void visibilityWritesAndSignalsOnlyForActualIdentityChanges() {
        auto store = std::make_shared<MemoryBrowseSettings>();
        ContentVisibility visibility(store);
        QSignalSpy changed(&visibility, &ContentVisibility::changed);
        visibility.setHidden({}, true);
        visibility.setHidden(project(1).toVariant(), false);
        QCOMPARE(changed.size(), 0);
        QVERIFY(store->data.isEmpty());
        visibility.setHidden(project(1).toVariant(), true);
        visibility.setHidden(project(1).toVariant(), true);
        QCOMPARE(changed.size(), 1);
        visibility.setHidden(project(1).toVariant(), false);
        QCOMPARE(changed.size(), 2);
    }
    void typoFallbackIsBoundedKeepsFiltersCachesAndNeverMasksProviderErrors() {
        auto source = std::make_shared<SearchFixture>();
        source->delayed = true;
        TolerantCatalogueSearch search(source);
        CatalogueQuery query;
        query.text = "ModXample";
        query.versions = {"1.21.1"};
        query.loaders = {"neoforge"};
        query.tags = {"mr:optimization"};
        CataloguePage result;
        bool done = false;
        search.fetch(query, 0, 20, [&](CataloguePage page) {
            result = page;
            done = true;
        });
        QCOMPARE(source->calls.size(), 1);
        source->calls[0].callback({{}, 0, {}});
        QCOMPARE(source->calls.size(), 2);
        QCOMPARE(source->calls[1].limit, 100);
        QCOMPARE(source->calls[1].query.text, QString("mod"));
        QCOMPARE(source->calls[1].query.versions, query.versions);
        QCOMPARE(source->calls[1].query.loaders, query.loaders);
        QCOMPARE(source->calls[1].query.tags, query.tags);
        auto matching = project(1);
        matching.title = "ModExample";
        auto other = project(2);
        other.title = "Unrelated";
        source->calls[1].callback({{matching, other}, 10000, {}});
        QVERIFY(done);
        QCOMPARE(result.total, 1);
        QCOMPARE(result.projects.first().id, matching.id);
        done = false;
        search.fetch(query, 0, 20, [&](CataloguePage page) {
            result = page;
            done = true;
        });
        QVERIFY(done);
        QCOMPARE(source->calls.size(), 2);
        auto otherAuthor = query;
        otherAuthor.authorIds = {{"modrinth", "different-author"}};
        search.fetch(otherAuthor, 0, 20, [](CataloguePage) {});
        QCOMPARE(source->calls.size(), 3);
        search.setKey({});
        done = false;
        search.fetch(query, 0, 20, [&](CataloguePage page) {
            result = page;
            done = true;
        });
        source->calls[3].callback({{}, 0, "Provider rate limit"});
        QVERIFY(done);
        QCOMPARE(result.error, QString("Provider rate limit"));
        QCOMPARE(source->calls.size(), 4);
    }

    void directPageEntryUsesNativeOffsetsAndKeepsEarlierPagesReachable() {
        auto api = source(260);
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(api, visibility);
        model.setContext("instance", {"1.21.1"}, {"fabric"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.pageCount(), 13);
        const auto first = model.projects();
        model.goToPage(10);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.page(), 10);
        QCOMPARE(model.projects().first().toMap().value("id").toString(), QString("180"));
        QCOMPARE(api->calls.last().offset, 180);
        model.goToPage(2);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.page(), 2);
        QCOMPARE(api->calls.last().offset, 20);
        model.previous();
        QCOMPARE(model.projects(), first);
        model.goToPage(99999);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.page(), 13);
        QVERIFY(!model.hasNext());
        model.goToPage(-1);
        QCOMPARE(model.page(), 1);
        QCOMPARE(model.pageCount(), 13);
    }
    void directPageEntryRebasesWhenTheProviderResultCountShrinks() {
        auto api = source(260);
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(api, visibility);
        model.setContext("instance", {"1.21.1"}, {"fabric"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.pageCount(), 13);
        api->rows["modrinth"].resize(37);
        model.goToPage(13);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.page(), 2);
        QCOMPARE(model.pageCount(), 2);
        QCOMPARE(model.projects().size(), 17);
        QCOMPARE(model.projects().first().toMap().value("id").toString(), QString("20"));
        QVERIFY(!model.hasNext());
    }
    void mergedPageEntryPreservesUniqueProjectsAcrossTheSkippedPages() {
        auto api = source(90);
        for (int i = 0; i < 90; ++i)
            api->rows["curseforge"] << project(i, "curseforge");
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(api, visibility, {{"modrinth:3", "pair"}, {"curseforge:3", "pair"}});
        model.setKey("fixture-key");
        QTRY_VERIFY(!model.busy());
        const auto first = model.projects();
        model.goToPage(4);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.page(), 4);
        QCOMPARE(model.projects().size(), 20);
        QSet<QString> ids;
        for (const auto &row : first)
            ids.insert(row.toMap().value("provider").toString() + ':' + row.toMap().value("id").toString());
        for (int page = 2; page <= 4; ++page) {
            model.goToPage(page);
            QTRY_VERIFY(!model.busy());
            for (const auto &row : model.projects()) {
                const auto map = row.toMap();
                const auto id = map.value("provider").toString() + ':' + map.value("id").toString();
                QVERIFY(!ids.contains(id));
                ids.insert(id);
            }
        }
        QCOMPARE(ids.size(), 80);
    }
    void downloadSortFindsCombinedLeadersBeyondBothProviderPageBoundaries() {
        auto api = source(90);
        api->rows["modrinth"][50].id = "paired-mr";
        for (int i = 0; i < 90; ++i)
            api->rows["curseforge"] << project(i, "curseforge");
        auto paired = project(100, "curseforge");
        paired.id = "paired-cf";
        api->rows["curseforge"] << paired;
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(api, visibility,
                              {{"modrinth:paired-mr", "pair"}, {"curseforge:paired-cf", "pair"}});
        model.setKey("fixture-key");
        QTRY_VERIFY(!model.busy());
        model.configure({{"sort", "downloads"}});
        QTRY_VERIFY(!model.busy());
        const auto rows = model.projects();
        QCOMPARE(rows.first().toMap().value("id").toString(), QString("paired-mr"));
        QCOMPARE(rows.first().toMap().value("downloadsRaw").toLongLong(), qint64(1850));
        qint64 previous = std::numeric_limits<qint64>::max();
        for (const auto &row : rows) {
            const auto count = row.toMap().value("downloadsRaw").toLongLong();
            QVERIFY(count <= previous);
            previous = count;
        }
        bool lookedAhead = false;
        for (const auto &call : api->calls)
            if (call.query.sort == "downloads" && call.offset >= 50)
                lookedAhead = true;
        QVERIFY(lookedAhead);
    }
    void authorViewsKeepExactProviderIdsAndRestoreOrdinaryPages() {
        auto api = source();
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(api, visibility);
        model.setContext("instance", {"1.21.1"}, {"fabric"});
        QTRY_VERIFY(!model.busy());
        model.next();
        QTRY_VERIFY(!model.busy());
        model.setScrollPosition(60);
        const QVariantMap author{{"name", "Author"},
                                 {"ids", QVariantMap{{"modrinth", "mr-exact"}, {"curseforge", "123"}}}};
        model.setContext("instance", {"1.21.1"}, {"fabric"}, author);
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.filters().value("type").toString(), QString("all"));
        QVERIFY(api->calls.last().query.versions.isEmpty());
        QCOMPARE(api->calls.last().query.authorIds.value("modrinth"), QString("mr-exact"));
        QCOMPARE(api->calls.last().query.authorIds.value("curseforge"), QString("123"));
        model.setContext("instance", {"1.21.1"}, {"fabric"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.page(), 2);
        QCOMPARE(model.scrollPosition(), 60.0);
    }
    void packDiscoveryIgnoresRememberedLoaderConstraints() {
        auto api = source();
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(api, visibility);
        model.setContext("instance", {"1.21.1"}, {"neoforge"});
        QTRY_VERIFY(!model.busy());
        model.configure({{"loaders", QStringList{"fabric", "quilt"}}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(api->calls.last().query.loaders, (QStringList{"fabric", "quilt"}));
        for (const auto &type : {"resourcepack", "datapack", "shader"}) {
            model.configure({{"type", type}});
            QTRY_VERIFY(!model.busy());
            QCOMPARE(api->calls.last().query.type, QString(type));
            QVERIFY(api->calls.last().query.loaders.isEmpty());
            QCOMPARE(api->calls.last().query.versions, QStringList{"1.21.1"});
        }
        model.configure({{"type", "mod"}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.filters().value("loaders").toStringList(), (QStringList{"fabric", "quilt"}));
    }
    void loaderChoicesAreEditableAndIndependentOfVersionLockAndBridgeDefaults() {
        auto api = source();
        auto visibility = std::make_shared<ContentVisibility>(std::make_shared<MemoryBrowseSettings>());
        BrowseCatalogue model(api, visibility);
        model.setContext("a", {"1.21.1"}, {"neoforge", "fabric"});
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.notice().isEmpty());
        QVERIFY(!model.filters().value("unlocked").toBool());
        model.configure({{"loaders", QStringList{"quilt"}}});
        model.setContext("a", {"1.21.1"}, {"neoforge"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.filters().value("loaders").toStringList(), QStringList{"quilt"});
        model.configure({{"unlocked", true}, {"versions", QStringList{"1.20.1"}}});
        model.configure({{"unlocked", false}});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.filters().value("versions").toStringList(), QStringList{"1.21.1"});
        QCOMPARE(model.filters().value("loaders").toStringList(), QStringList{"quilt"});
        model.setContext("b", {"1.21.1"}, {"neoforge"});
        model.setContext("b", {"1.21.1"}, {"neoforge", "fabric"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.filters().value("loaders").toStringList(), (QStringList{"neoforge", "fabric"}));
    }
    void compatibilityAndPageStateAreSeparatePerTypeAndPersistPerInstance() {
        QTemporaryDir temp;
        const auto file = temp.path() + "/filters.ini";
        auto store = std::make_shared<BrowseSettingsStore>(file);
        auto prefs = std::make_shared<BrowseFilterPreferences>(store);
        auto visibility = std::make_shared<ContentVisibility>(store);
        {
            BrowseCatalogue model(source(), visibility, {}, prefs);
            model.setContext("a", {"1.21.1"}, {"neoforge"});
            model.configure({{"unlocked", true},
                             {"versions", QStringList{"1.20.1"}},
                             {"loaders", QStringList{"fabric", "quilt"}}});
            QTRY_VERIFY(!model.busy());
            model.next();
            QTRY_VERIFY(!model.busy());
            model.setScrollPosition(117);
            model.configure({{"type", "resourcepack"}});
            model.configure(
                {{"unlocked", true}, {"versions", QStringList{"1.19.2"}}, {"loaders", QStringList{}}});
            QTRY_VERIFY(!model.busy());
            model.setContext("b", {"1.21.1"}, {"forge"});
            model.configure({{"unlocked", true}, {"versions", QStringList{"1.18.2"}}});
            QTRY_VERIFY(!model.busy());
            model.setContext("a", {}, {}); // pending metadata must not erase a saved view
            model.configure({{"type", "mod"}});
            model.setContext("a", {"1.21.1"}, {"neoforge", "fabric"});
            QTRY_VERIFY(!model.busy());
            QCOMPARE(model.page(), 2);
            QCOMPARE(model.scrollPosition(), 117.0);
            QCOMPARE(model.filters().value("versions").toStringList(), QStringList{"1.20.1"});
            QCOMPARE(model.filters().value("loaders").toStringList(), (QStringList{"fabric", "quilt"}));
        }
        auto restored =
            std::make_shared<BrowseFilterPreferences>(std::make_shared<BrowseSettingsStore>(file));
        BrowseCatalogue model(source(), visibility, {}, restored);
        model.setContext("a", {"1.21.1"}, {"neoforge"});
        QCOMPARE(model.filters().value("versions").toStringList(), QStringList{"1.20.1"});
        QCOMPARE(model.filters().value("loaders").toStringList(), (QStringList{"fabric", "quilt"}));
        model.configure({{"type", "resourcepack"}});
        QCOMPARE(model.filters().value("versions").toStringList(), QStringList{"1.19.2"});
        QVERIFY(model.filters().value("loaders").toStringList().isEmpty());
        model.setContext("b", {"1.21.1"}, {"forge"});
        QCOMPARE(model.filters().value("versions").toStringList(), QStringList{"1.18.2"});
        model.setContext("global", {}, {});
        QVERIFY(model.filters().value("versions").toStringList().isEmpty());
        QTRY_VERIFY(!model.busy());
    }
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
    void browsingDefaultsPersistWithoutOverwritingVisitedViewsOrCompatibility() {
        QTemporaryDir temp;
        const auto file = temp.path() + "/defaults.ini";
        BrowsePreferences preferences(std::make_shared<BrowseSettingsStore>(file));
        QVERIFY(preferences.setDefault("size", 50));
        QVERIFY(preferences.setDefault("sort", "downloads"));
        QVERIFY(preferences.setDefault("source", "modrinth"));
        QVERIFY(preferences.setDefault("hideInstalled", true));
        const auto good = preferences.defaults();
        QVERIFY(!preferences.setDefault("size", 20.5));
        QVERIFY(!preferences.setDefault("sort", "invalid"));
        QVERIFY(!preferences.setDefault("source", "invalid"));
        QVERIFY(!preferences.setDefault("hideInstalled", "false"));
        QCOMPARE(preferences.defaults(), good);
        BrowsePreferences reloaded(std::make_shared<BrowseSettingsStore>(file));
        QCOMPARE(reloaded.defaults(), good);
        auto store = std::make_shared<MemoryBrowseSettings>();
        auto visibility = std::make_shared<ContentVisibility>(store);
        BrowseCatalogue model(source(), visibility);
        model.setDefaults(reloaded.defaults());
        model.setContext("first", {"1.21.1"}, {"neoforge"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 50);
        QCOMPARE(model.filters().value("sort").toString(), QString("downloads"));
        model.configure({{"size", 10}, {"loaders", QStringList{"fabric"}}});
        QVERIFY(preferences.setDefault("size", 20));
        model.setDefaults(preferences.defaults());
        model.setContext("second", {"1.20.1"}, {"forge"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.projects().size(), 20);
        model.setContext("first", {"1.21.1"}, {"neoforge"});
        QTRY_VERIFY(!model.busy());
        QCOMPARE(model.filters().value("size").toInt(), 10);
        QCOMPARE(model.filters().value("loaders").toStringList(), QStringList{"fabric"});
        preferences.resetDefaults();
        QVERIFY(preferences.connectorFabric());
        QCOMPARE(preferences.defaults().value("size").toInt(), 20);
        QCOMPARE(preferences.defaults().value("hideInstalled").toBool(), false);
    }
    void invalidSavedBrowsingDefaultsFallBackWithoutWrites() {
        auto store = std::make_shared<MemoryBrowseSettings>();
        store->data["browse/defaults"] =
            QVariantMap{{"size", 3}, {"sort", "invalid"}, {"source", "invalid"}, {"hideInstalled", "false"}};
        const auto before = store->data;
        BrowsePreferences preferences(store);
        QCOMPARE(preferences.defaults().value("size").toInt(), 20);
        QCOMPARE(preferences.defaults().value("source").toString(), QString("merged"));
        QCOMPARE(preferences.defaults().value("hideInstalled").toBool(), false);
        QCOMPARE(store->data, before);
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
