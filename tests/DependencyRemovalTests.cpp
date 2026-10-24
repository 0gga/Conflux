#include "ContentFixtures.h"
#include "features/content/ContentActions.h"
#include "features/content/DependencyRemovals.h"
#include "features/preferences/ContentPreferences.h"
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
using namespace studio;
namespace {
QVariantMap row(QString id, QStringList refs = {}) {
    QVariantList deps;
    for (const auto &ref : refs)
        deps << QVariantMap{{"id", ref}, {"required", true}};
    return {{"title", id},
            {"modId", id},
            {"category", "mods"},
            {"sha1", id},
            {"relativePath", "mods/" + id + ".jar"},
            {"enabled", true},
            {"runtimeDependencies", deps}};
}
QString key(QString id) {
    return contentRowKey(row(id));
}
class Files final : public IContentFileStore {
  public:
    mutable QMutex mutex;
    mutable QVariantList rows;
    mutable int reads = 0, writes = 0;
    QVariantList snapshot(const QVariantMap &, const QVariantList &) const override {
        QMutexLocker guard(&mutex);
        return rows;
    }
    ContentFileState reload(const QVariantMap &pack, const QVariantList &, bool) const override {
        QMutexLocker guard(&mutex);
        ++reads;
        return {pack, rows, {}};
    }
    ContentWriteResult remove(const QVariantMap &, const QVariantList &targets) const override {
        QMutexLocker guard(&mutex);
        ++writes;
        QSet<QString> keys;
        for (const auto &v : targets)
            keys.insert(contentRowKey(v.toMap()));
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [&](auto v) { return keys.contains(contentRowKey(v.toMap())); }),
                   rows.end());
        return {true, {}, {}, true};
    }
    ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const override {
        return {};
    }
    ContentWriteResult importFiles(const QVariantMap &, const QList<QUrl> &, const QString &) const override {
        return {};
    }
};
} // namespace
class DependencyRemovalTests final : public QObject {
    Q_OBJECT
  private slots:
    void promptKeepsSharedAndLockedDependenciesAndQueuesOnlyExplicitActions() {
        DependencyRemovals model;
        const QVariantMap pack{{"path", "original"}};
        const auto a = row("A", {"B", "C"}), b = row("B"), c = row("C"), d = row("D", {"C"});
        int calls = 0;
        QVariantMap destination;
        QVariantList accepted;
        model.setRemover([&](auto p, auto r) {
            ++calls;
            destination = p;
            accepted = r;
            return true;
        });
        model.observe(pack, {a, b, c, d}, {b, c, d}, {key("A")}, {}, false);
        QVERIFY(model.visible());
        QCOMPARE(model.rows().size(), 2);
        QCOMPARE(model.unusedCount(), 1);
        QCOMPARE(calls, 0);
        model.remove(key("C"));
        QCOMPARE(calls, 0);
        model.removeUnused();
        QCOMPARE(calls, 1);
        QCOMPARE(destination, pack);
        QCOMPARE(accepted.first().toMap().value("title").toString(), QString("B"));
        model.observe(pack, {b, c, d}, {c, d}, {key("B")}, {}, true);
        QCOMPARE(model.rows().first().toMap().value("state").toString(), QString("removed"));
        QCOMPARE(model.rows().last().toMap().value("state").toString(), QString("kept"));
    }
    void cleanupRevalidatesFreshInventoryBeforeRemovingFiles_data() {
        QTest::addColumn<QString>("change");
        QTest::newRow("new-parent") << QString("parent");
        QTest::newRow("new-lock") << QString("lock");
        QTest::newRow("changed-file") << QString("hash");
    }
    void cleanupRevalidatesFreshInventoryBeforeRemovingFiles() {
        QFETCH(QString, change);
        QTemporaryDir temp;
        auto files = std::make_shared<Files>();
        auto catalogue = std::make_shared<ContentCatalogueFixture>();
        ContentActions content(files, catalogue, std::make_shared<ContentInstallerFixture>(),
                               std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini"));
        const QVariantMap pack{{"path", temp.path()}};
        const auto a = row("A", {"B"}), b = row("B");
        files->rows = {a, b};
        DependencyRemovals model;
        model.setRemover([&](auto p, auto r) { return content.removeDependencies(p, r); });
        connect(&content, &ContentActions::removalFinished, &model, &DependencyRemovals::observe);
        content.setContext(pack, {a, b});
        QTRY_VERIFY(!content.busy());
        content.remove("mods/A.jar");
        QTRY_VERIFY(!content.busy());
        QVERIFY(model.visible());
        QCOMPARE(files->writes, 1);
        QCOMPARE(model.unusedCount(), 1);
        {
            QMutexLocker guard(&files->mutex);
            if (change == "parent")
                files->rows << row("New", {"B"});
            else {
                auto changed = files->rows.first().toMap();
                changed[change == "lock" ? "launcherLocked" : "sha1"] =
                    change == "lock" ? QVariant(true) : QVariant("changed");
                files->rows[0] = changed;
            }
        }
        model.removeUnused();
        QTRY_VERIFY(!content.busy());
        QCOMPARE(files->writes, 1);
        QCOMPARE(model.rows().first().toMap().value("state").toString(),
                 change == "hash" ? QString("error") : QString("kept"));
        if (change == "parent")
            QVERIFY(model.rows().first().toMap().value("dependencyHint").toString().contains("New"));
        if (change == "lock")
            QCOMPARE(model.rows().first().toMap().value("reason").toString(), QString("Locked"));
        QCOMPARE(content.operations()->failedCount(), change == "hash" ? 1 : 0);
    }
    void dismissalDoesNotCancelAcceptedWritesAndNextInstanceOfferOpens() {
        DependencyRemovals model;
        model.setRemover([](auto, auto) { return true; });
        auto a = row("A", {"B"}), b = row("B");
        model.observe({{"path", "first"}}, {a, b}, {b}, {key("A")}, {}, false);
        model.removeUnused();
        model.observe({{"path", "second"}}, {a, b}, {b}, {key("A")}, {}, false);
        model.dismiss();
        QTRY_COMPARE(model.pack().value("path").toString(), QString("second"));
        QVERIFY(model.visible());
    }
    void failedRemovalsStayInlineAndCanRetry() {
        DependencyRemovals model;
        model.setRemover([](auto, auto) { return true; });
        auto a = row("A", {"B"}), b = row("B");
        QVariantMap pack{{"path", "first"}};
        model.observe(pack, {a, b}, {b}, {key("A")}, {}, false);
        model.remove(key("B"));
        model.observe(pack, {b}, {b}, {key("B")}, "Fixture write failed", true);
        QCOMPARE(model.rows().first().toMap().value("state").toString(), QString("error"));
        model.retry(key("B"));
        QCOMPARE(model.rows().first().toMap().value("state").toString(), QString("queued"));
    }
    void acceptedRemovalRetainsItsDestinationThroughReentrantDismissal() {
        DependencyRemovals model;
        auto a = row("A", {"B"}), b = row("B");
        QVariantMap destination;
        bool dismissed = false;
        model.setRemover([&](auto pack, auto) {
            destination = pack;
            return true;
        });
        model.observe({{"path", "first"}}, {a, b}, {b}, {key("A")}, {}, false);
        connect(&model, &DependencyRemovals::changed, &model, [&] {
            if (!dismissed && model.visible() && model.rows().first().toMap().value("state") == "queued") {
                dismissed = true;
                model.dismiss();
                model.observe({{"path", "second"}}, {a, b}, {b}, {key("A")}, {}, false);
            }
        });
        model.removeUnused();
        QVERIFY(dismissed);
        QCOMPARE(destination.value("path").toString(), QString("first"));
        QCOMPARE(model.pack().value("path").toString(), QString("second"));
    }
    void closedUnusedCyclesCanBeRemovedTogetherButSharedCyclesCannot() {
        DependencyRemovals model;
        auto root = row("Root", {"A"}), a = row("A", {"B"}), b = row("B", {"A"});
        int accepted = 0;
        model.setRemover([&](auto, auto rows) {
            accepted = rows.size();
            return true;
        });
        model.observe({{"path", "first"}}, {root, a, b}, {a, b}, {key("Root")}, {}, false);
        QCOMPARE(model.unusedCount(), 2);
        model.remove(key("A"));
        QCOMPARE(accepted, 0);
        model.removeUnused();
        QCOMPARE(accepted, 2);
        QCOMPARE(model.unusedCount(), 0);
    }
};
QTEST_GUILESS_MAIN(DependencyRemovalTests)
#include "DependencyRemovalTests.moc"
