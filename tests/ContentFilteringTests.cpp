#include "ContentFixtures.h"
#include "domain/TextSearch.h"
#include "features/content/ContentActions.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/BrowseSettingsStore.h"
#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QPersistentModelIndex>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>
using namespace studio;
namespace {
QVariantList inventory(int count) {
    QVariantList rows;
    for (int i = 0; i < count; ++i) {
        const auto id = QString::number(i).rightJustified(5, '0');
        rows << QVariantMap{{"contentKey", "key" + id},
                            {"category", i % 3 ? "mods" : "resourcepacks"},
                            {"relativePath", "mods/file" + id + ".jar"},
                            {"fileName", "file" + id + ".jar"},
                            {"title", "Example Mod " + id},
                            {"version", "1.21.1"},
                            {"author", "Author " + QString::number(i % 73)},
                            {"enabled", i % 2 == 0},
                            {"sha1", QString(35, 'a') + id},
                            {"sourceIds", QVariantMap{{"modrinth", id}}},
                            {"updateAvailable", i % 5 == 0}};
    }
    return rows;
}
} // namespace
class ContentFilteringTests final : public QObject {
    Q_OBJECT
  private slots:
    void filteringLoadedContentHasNoAdditionalIoAndBoundedCpu_data() {
        QTest::addColumn<int>("count");
        QTest::newRow("owner-sized-310") << 310;
        QTest::newRow("large-3000") << 3000;
    }
    void filteringLoadedContentHasNoAdditionalIoAndBoundedCpu() {
        QFETCH(int, count);
        QTemporaryDir temp;
        auto catalogue = std::make_shared<ContentCatalogueFixture>();
        auto store = std::make_shared<BrowseSettingsStore>(temp.path() + "/view.ini");
        ContentActions content(std::make_shared<ContentFileFixture>(), catalogue,
                               std::make_shared<ContentInstallerFixture>(),
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        content.setVisibility(std::make_shared<ContentVisibility>(store));
        content.setContext({{"path", temp.path()}}, inventory(count));
        QTRY_VERIFY(!content.busy());
        const auto calls = catalogue->refreshCount;
        QElapsedTimer elapsed;
        elapsed.start();
        for (int i = 0; i < 12; ++i) {
            content.setCategory(i % 2 ? "all" : "mods");
            content.setQuery(i % 3 ? "1211" : "Exmple");
            content.setFilter("state", i % 2 ? "enabled" : "");
            QVERIFY(!content.rows().isEmpty());
            QCOMPARE(content.authors().size(), 73);
        }
        const auto milliseconds = elapsed.elapsed();
        qInfo() << count << "loaded rows / 36 filter transitions ms" << milliseconds;
        QCOMPARE(catalogue->refreshCount, calls);
        QVERIFY2(milliseconds < (count == 310 ? 75 : 450),
                 "Loaded filtering does excessive per-row/model work.");
    }
    void preparedSearchMatchesTheExistingUnicodeAndTypoContract() {
        QVariantList rows;
        const QStringList titles{QString::fromUtf8("Caf\xc3\xa9 ModExample"),
                                 "ModXample",
                                 "Example-Mod",
                                 "1.21.1",
                                 "Skin Layers",
                                 "M O D"};
        for (int i = 0; i < titles.size(); ++i)
            rows << QVariantMap{
                {"title", titles[i]}, {"fileName", "file_" + QString::number(i)}, {"version", "1.21.1"}};
        ContentViewIndex view;
        view.remember(rows, "a", 0, {});
        for (const auto &query :
             {"", "1211", "ModXample", "cafe", "skinlayers", "SkLayers", "1.20.1", "zzzz", "MOD"}) {
            QSet<QString> expected, actual;
            for (const auto &value : rows) {
                const auto row = value.toMap();
                if (textMatches(row.value("title").toString() + ' ' + row.value("fileName").toString() +
                                    "  1.21.1",
                                query))
                    expected.insert(row.value("title").toString());
            }
            for (const auto &value : view.filter(query, "all", "name-asc", {}))
                actual.insert(value.toMap().value("title").toString());
            QCOMPARE(actual, expected);
        }
    }
    void viewCacheTracksInventoryInstanceAndVisibilityWithoutRepeatedLookup() {
        auto rows = inventory(12);
        ContentViewIndex view;
        bool hidden = false;
        int lookups = 0;
        auto visibility = [&](const QVariantMap &) {
            ++lookups;
            return hidden;
        };
        view.remember(rows, "a", 0, visibility);
        QCOMPARE(lookups, 12);
        for (int i = 0; i < 10; ++i) {
            view.remember(rows, "a", 0, visibility);
            QCOMPARE(view.filter({}, "all", "name-asc", {}).size(), 12);
        }
        QCOMPARE(lookups, 12);
        hidden = true;
        view.remember(rows, "a", 1, visibility);
        QVERIFY(view.filter({}, "all", "name-asc", {}).isEmpty());
        QCOMPARE(view.filter({}, "all", "name-asc", {{"hidden", true}}).size(), 12);
        auto changed = rows.first().toMap();
        changed["author"] = "New Author";
        rows[0] = changed;
        view.remember(rows, "a", 1, visibility);
        QVERIFY(view.authors().contains("New Author"));
        view.remember(rows, "b", 1, visibility);
        QCOMPARE(lookups, 48);
    }
    void duplicateAndEmptyKeysRemainOrderedAndPersistentByOccurrence() {
        ContentListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVariantList rows{QVariantMap{{"contentKey", "same"}, {"title", "one"}},
                          QVariantMap{{"contentKey", "same"}, {"title", "two"}},
                          QVariantMap{{"contentKey", ""}, {"title", "empty"}},
                          QVariantMap{{"relativePath", "fallback"}, {"title", "fallback"}}};
        model.setRows(rows);
        QPersistentModelIndex first(model.index(0)), second(model.index(1)), empty(model.index(2));
        const QVariantList next{rows[2], rows[3], rows[0]};
        model.setRows(next);
        QCOMPARE(model.rowCount(), 3);
        QVERIFY(first.isValid());
        QVERIFY(!second.isValid());
        QVERIFY(empty.isValid());
        QCOMPARE(first.row(), 2);
        QCOMPARE(empty.row(), 0);
        for (int i = 0; i < next.size(); ++i)
            QCOMPARE(model.data(model.index(i), Qt::UserRole + 1), next[i]);
    }
    void largeReorderingDoesNotScanEveryRowForEveryTarget() {
        ContentListModel model;
        auto rows = inventory(5000);
        model.setRows(rows);
        const QPersistentModelIndex persistent(model.index(100));
        const auto saved = model.data(persistent, Qt::UserRole + 1);
        std::reverse(rows.begin(), rows.end());
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QElapsedTimer elapsed;
        elapsed.start();
        model.setRows(rows);
        const auto milliseconds = elapsed.elapsed();
        qInfo() << "5000 rows reverse model reorder ms" << milliseconds;
        QVERIFY(persistent.isValid());
        QCOMPARE(model.data(persistent, Qt::UserRole + 1), saved);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(model.data(model.index(0), Qt::UserRole + 1), rows.first());
        QVERIFY2(milliseconds < 150, "Model reordering repeatedly scans QVariant row keys.");
    }
    void modelReconciliationPreservesReferenceRowsAndPersistentIdentity() {
        ContentListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const auto source = inventory(80);
        model.setRows(source);
        QRandomGenerator random(711);
        for (int step = 0; step < 120; ++step) {
            QVariantList next;
            for (const auto &value : source)
                if (random.bounded(4)) {
                    auto row = value.toMap();
                    row["title"] = QString::number(step);
                    next << row;
                }
            std::shuffle(next.begin(), next.end(), random);
            QList<QPair<QPersistentModelIndex, QString>> tracked;
            for (int i = 0; i < model.rowCount(); i += 7)
                tracked << qMakePair(
                    QPersistentModelIndex(model.index(i)),
                    model.data(model.index(i), Qt::UserRole + 1).toMap().value("contentKey").toString());
            model.setRows(next);
            QCOMPARE(model.rowCount(), next.size());
            for (int i = 0; i < next.size(); ++i)
                QCOMPARE(model.data(model.index(i), Qt::UserRole + 1), next[i]);
            for (const auto &[index, key] : tracked) {
                const bool retained = std::any_of(next.begin(), next.end(), [&](const QVariant &v) {
                    return v.toMap().value("contentKey") == key;
                });
                QCOMPARE(index.isValid(), retained);
                if (retained)
                    QCOMPARE(model.data(index, Qt::UserRole + 1).toMap().value("contentKey").toString(), key);
            }
        }
    }
};
QTEST_GUILESS_MAIN(ContentFilteringTests)
#include "ContentFilteringTests.moc"
