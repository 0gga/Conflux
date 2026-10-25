#include "TestPerformance.h"
#include "features/content/ContentOperationReservations.h"
#include "features/content/ContentTargetIndex.h"
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QtTest>
using namespace studio;
namespace {
QVariantMap row(const QString &key, const QString &title = {}) {
    return {{"contentKey", key},
            {"title", title},
            {"relativePath", "mods/" + key + ".jar"},
            {"category", "mods"},
            {"sourceIds", QVariantMap{{"modrinth", key}}}};
}
QVariantList reference(const QVariantList &rows, const QStringList &targets) {
    QVariantList result;
    for (const auto &value : rows)
        if (targets.contains(contentRowKey(value.toMap())))
            result << value;
    return result;
}
} // namespace
class ContentTargetIndexTests final : public QObject {
    Q_OBJECT
  private slots:
    void resultsPreserveInventoryOrderAndDuplicateOccurrences() {
        ContentTargetIndex index;
        const QVariantList rows{
            row("b", "first"), row("a"), row("b", "second"), row(""),
            QVariantMap{{"category", "mods"}, {"relativePath", "mods/fallback.jar.disabled"}}};
        const QStringList targets{"a", "b", "b", "", "mods:mods/fallback.jar", "absent"};
        QCOMPARE(index.resolve(rows, targets), rows);
        QCOMPARE(index.resolve(rows, {"b"}), QVariantList({rows.at(0), rows.at(2)}));
        QCOMPARE(index.resolve(rows, {}), QVariantList{});
        QCOMPARE(index.resolve({}, targets), QVariantList{});
    }
    void snapshotChangesCannotReuseObsoleteRowsOrKeys() {
        ContentTargetIndex index;
        QVariantList first{row("one", "before"), row("two")};
        QCOMPARE(index.resolve(first, {"one"}), QVariantList{first.first()});
        auto changed = first;
        changed[0] = row("one", "after");
        QCOMPARE(index.resolve(changed, {"one"}), QVariantList{changed.first()});
        QCOMPARE(index.resolve(first, {"one"}), QVariantList{first.first()});
        first[0] = row("three");
        QVERIFY(index.resolve(first, {"one"}).isEmpty());
        QCOMPARE(index.resolve(first, {"three"}), QVariantList{first.first()});
    }
    void seededSnapshotsMatchIndependentOrderedReference() {
        ContentTargetIndex index;
        QRandomGenerator random(314159);
        QVariantList rows;
        for (int generation = 0; generation < 300; ++generation) {
            if (generation % 3 == 0) {
                rows.clear();
                for (int i = 0; i < 150; ++i)
                    rows << row(QString::number(random.bounded(50u)), QString::number(generation));
            } else {
                rows[random.bounded(quint32(rows.size()))] = row(QString::number(random.bounded(50u)));
            }
            QStringList targets;
            for (int i = 0; i < 20; ++i)
                targets << QString::number(random.bounded(60u));
            QCOMPARE(index.resolve(rows, targets), reference(rows, targets));
        }
    }
    void indexedReservationTargetsRetainPathsProviderAliasesAndCategories() {
        ContentTargetIndex index;
        auto original = row("one"), other = row("two");
        original["sourceIds"] = QVariantMap{{"modrinth", "one"}, {"curseforge", "42"}};
        ContentOperation operation;
        operation.targets = {"one", "one"};
        auto prediction = original;
        prediction["relativePath"] = "mods/new.jar.disabled";
        operation.predictions = {prediction};
        const QVariantList rows{other, original, original};
        const auto legacy = ContentOperationReservations::keys(operation, rows);
        const auto indexed =
            ContentOperationReservations::keysForTargets(operation, index.resolve(rows, operation.targets));
        QCOMPARE(QSet<QString>(legacy.begin(), legacy.end()), QSet<QString>(indexed.begin(), indexed.end()));
        QVERIFY(indexed.contains("path:mods/one.jar"));
        QVERIFY(indexed.contains("path:mods/new.jar"));
        QVERIFY(indexed.contains("project:mods:curseforge:42"));
        QVERIFY(!indexed.contains("project:mods:modrinth:two"));
    }
    void repeatedSingleTargetsHaveBoundedCost() {
        ContentTargetIndex index;
        QVariantList rows;
        rows.reserve(5000);
        for (int i = 0; i < 5000; ++i)
            rows << row(QString::number(i));
        QElapsedTimer elapsed;
        elapsed.start();
        for (int i = 0; i < 5000; ++i)
            QCOMPARE(index.resolve(rows, {QString::number(i)}), QVariantList{rows.at(i)});
        qInfo() << "Resolve 5000 targets over 5000 rows including index preparation ms" << elapsed.elapsed();
        QVERIFY(test_performance::releaseBudgetMet(elapsed.elapsed(), 150));
    }
};
QTEST_GUILESS_MAIN(ContentTargetIndexTests)
#include "ContentTargetIndexTests.moc"
