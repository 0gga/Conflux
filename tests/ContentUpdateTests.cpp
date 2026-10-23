#include "ContentUpdateFixtures.h"
#include "platform/BrowseSettingsStore.h"
#include <QPersistentModelIndex>
#include <QtTest>
using namespace update_fixture;
class ContentUpdateTests : public QObject {
    Q_OBJECT
  private slots:
    void policyChangesRecheckQueuedUpdatesAndKeepInstalledRowsVisible_data() {
        QTest::addColumn<QStringList>("targetPolicy");
        QTest::addColumn<QString>("installedStage");
        QTest::addColumn<QString>("candidateStage");
        QTest::newRow("explicit-release") << QStringList{"release"} << QString("release") << QString("beta");
        QTest::newRow("stable-beta-back-to-default")
            << QStringList{} << QString("release") << QString("beta");
        QTest::newRow("beta-alpha-back-to-default") << QStringList{} << QString("beta") << QString("alpha");
    }
    void policyChangesRecheckQueuedUpdatesAndKeepInstalledRowsVisible() {
        QFETCH(QStringList, targetPolicy);
        QFETCH(QString, installedStage);
        QFETCH(QString, candidateStage);
        Harness h;
        auto policy = std::make_shared<studio::PackReleasePolicy>(
            std::make_shared<studio::BrowseSettingsStore>(h.temp.path() + "/stages.ini"));
        policy->setChannels(h.temp.path(), {"release", "beta", "alpha"});
        h.content.setReleasePolicy(policy);
        auto a = row("A"), b = row("B");
        auto next = b.value("updateRelease").toMap();
        next["channel"] = candidateStage;
        auto old = b.value("installedRelease").toMap();
        old["channel"] = installedStage;
        b["installedRelease"] = old;
        b["updateRelease"] = next;
        h.set({a, b});
        QTRY_VERIFY(!h.content.busy());
        h.installer->automatic = false;
        QSignalSpy errors(&h.content, &ContentActions::operationFailed);
        h.content.updateAll();
        QTRY_COMPARE(h.installer->calls.size(), 1);
        QCOMPARE(h.installer->calls.first().first().allowedChannels,
                 (QStringList{"release", "beta", "alpha"}));
        policy->setChannels(h.temp.path(), targetPolicy);
        QCOMPARE(h.content.rows().size(), 2);
        h.installer->finish();
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.installer->calls.size(), 1);
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().first().toString().contains("release stages"));
        QCOMPARE(byId(h.files->get(h.temp.path()), "A").value("version").toString(), QString("2.0"));
        QCOMPARE(byId(h.files->get(h.temp.path()), "B").value("version").toString(), QString("1.0"));
        QCOMPARE(h.content.rows().size(), 2);
        QCOMPARE(h.content.actionableUpdateCount(), 0);
    }
    void defaultReleasePolicyAlsoGuardsManualVersionSwitches() {
        Harness h;
        auto policy = std::make_shared<studio::PackReleasePolicy>(
            std::make_shared<studio::BrowseSettingsStore>(h.temp.path() + "/stages.ini"));
        h.content.setReleasePolicy(policy);
        const auto installed = row("B");
        auto next = installed.value("updateRelease").toMap();
        next["channel"] = "beta";
        h.set({installed});
        QTRY_VERIFY(!h.content.busy());
        QSignalSpy errors(&h.content, &ContentActions::operationFailed);
        h.content.installRelease(next, {{"sourceIds", installed.value("sourceIds")}, {"type", "mod"}});
        QCOMPARE(h.installer->calls.size(), 0);
        QCOMPARE(errors.size(), 1);
        policy->setChannels(h.temp.path(), {"beta"});
        h.content.installRelease(next, {{"sourceIds", installed.value("sourceIds")}, {"type", "mod"}});
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.installer->calls.size(), 1);
        QCOMPARE(h.installer->calls.first().first().allowedChannels, QStringList{"beta"});
        QCOMPARE(byId(h.files->get(h.temp.path()), "B").value("version").toString(), QString("2.0"));
    }
    void explicitPolicyBlocksSingleAndManualUpdatesBeforeQueueing() {
        Harness h;
        auto policy = std::make_shared<studio::PackReleasePolicy>(
            std::make_shared<studio::BrowseSettingsStore>(h.temp.path() + "/stages.ini"));
        policy->setChannels(h.temp.path(), {"release"});
        h.content.setReleasePolicy(policy);
        auto b = row("B");
        auto next = b.value("updateRelease").toMap();
        next["channel"] = "beta";
        b["updateRelease"] = next;
        h.set({b});
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.content.rows().size(), 1);
        QCOMPARE(h.content.updateCount(), 0);
        h.content.update(b.value("relativePath").toString());
        h.content.updateAll();
        h.content.select(b.value("relativePath").toString(), true);
        h.content.updateSelected();
        QSignalSpy errors(&h.content, &ContentActions::operationFailed);
        h.content.installRelease(next, {{"sourceIds", b.value("sourceIds")}, {"type", "mod"}});
        QCOMPARE(errors.size(), 1);
        QCOMPARE(h.installer->calls.size(), 0);
    }
    void protectionAndSelectionMatrix_data() {
        QTest::addColumn<QString>("category");
        QTest::addColumn<QString>("policy");
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<bool>("selected");
        for (const auto &type : {"mods", "resourcepacks", "shaderpacks", "datapacks"})
            for (const auto &policy : {"none", "primary", "secondary", "path", "source", "frozen"})
                for (bool enabled : {false, true})
                    for (bool selected : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-enabled%3-selected%4")
                                                     .arg(type, policy)
                                                     .arg(enabled)
                                                     .arg(selected)))
                            << QString(type) << QString(policy) << enabled << selected;
    }
    void protectionAndSelectionMatrix() {
        QFETCH(QString, category);
        QFETCH(QString, policy);
        QFETCH(bool, enabled);
        QFETCH(bool, selected);
        Harness h;
        const auto a = row("A", category, enabled), b = row("B", category), c = row("C", category);
        if (policy == "primary" || policy == "secondary")
            h.preferences->setLocked(h.temp.path(), policy == "primary" ? "modrinth:A" : "curseforge:cf-A",
                                     true);
        if (policy == "path")
            h.preferences->setLocked(
                h.temp.path(), "path:" + category + "/A-old" + (category == "mods" ? ".jar" : ".zip"), true);
        if (policy == "frozen")
            h.preferences->setFrozen(h.temp.path(), "modrinth:A", true);
        if (policy == "source")
            h.pack["sourceContent"] = QVariantList{QVariantMap{{"projectId", "A"}, {"launcherLocked", true}}};
        h.set({a, b, c});
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.content.updateCount(), 3);
        const bool protectedItem = policy != "none";
        QCOMPARE(h.content.actionableUpdateCount(), protectedItem ? 2 : 3);
        QSignalSpy errors(&h.content, &ContentActions::operationFailed),
            resets(h.content.model(), &QAbstractItemModel::modelReset);
        const auto checks = h.catalogue->refreshCount;
        if (selected) {
            h.content.select(a.value("relativePath").toString(), true);
            h.content.select(b.value("relativePath").toString(), true);
            h.content.updateSelected();
        } else
            h.content.updateAll();
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.installer->calls.size(), (selected ? 2 : 3) - int(protectedItem));
        for (const auto &batch : h.installer->calls) {
            QCOMPARE(batch.size(), 1);
            QVERIFY(!batch.first().resolveDependencies);
        }
        QCOMPARE(h.installer->peak, 1);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(h.catalogue->refreshCount, checks);
        const auto actual = h.files->get(h.temp.path());
        QCOMPARE(byId(actual, "A").value("version").toString(), protectedItem ? "1.0" : "2.0");
        QCOMPARE(byId(actual, "A").value("enabled").toBool(), enabled);
        QCOMPARE(byId(actual, "B").value("version").toString(), QString("2.0"));
        QCOMPARE(byId(actual, "C").value("version").toString(), selected ? "1.0" : "2.0");
    }
    void oneFailureDoesNotUndoOrBlockOtherItems_data() {
        QTest::addColumn<QString>("category");
        QTest::addColumn<int>("position");
        QTest::addColumn<int>("failure");
        for (const auto &type : {"mods", "resourcepacks", "shaderpacks", "datapacks"})
            for (int position = 0; position < 3; ++position)
                for (int failure : {1, 2, 3})
                    QTest::newRow(
                        qPrintable(QString("%1-item%2-failure%3").arg(type).arg(position).arg(failure)))
                        << QString(type) << position << failure;
    }
    void oneFailureDoesNotUndoOrBlockOtherItems() {
        QFETCH(QString, category);
        QFETCH(int, position);
        QFETCH(int, failure);
        Harness h;
        const QStringList ids{"A", "B", "C"};
        h.installer->failures[ids[position]] = failure;
        h.set({row("A", category), row("B", category), row("C", category)});
        QTRY_VERIFY(!h.content.busy());
        QSignalSpy errors(&h.content, &ContentActions::operationFailed),
            changes(&h.content, &ContentActions::contentChanged);
        h.content.updateAll();
        QCOMPARE(h.content.operations()->activeCount(), 3);
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.installer->calls.size(), 3);
        QCOMPARE(h.installer->peak, 1);
        QCOMPARE(errors.size(), 1);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(h.content.operations()->failedCount(), 1);
        const auto actual = h.files->get(h.temp.path());
        for (int i = 0; i < ids.size(); ++i) {
            QCOMPARE(byId(actual, ids[i]).value("version").toString(), i == position ? "1.0" : "2.0");
            QCOMPARE(h.preferences->updatedAt(h.temp.path(), "modrinth:" + ids[i]) > 0, i != position);
        }
        const auto failed = taskId(h.content, ids[position]);
        bool retryable = false;
        for (const auto &entry : h.content.operations()->entries())
            if (entry.toMap().value("id").toULongLong() == failed)
                retryable = entry.toMap().value("retryable").toBool();
        QCOMPARE(retryable, failure != 3);
        if (retryable) {
            h.installer->failures.clear();
            h.content.operations()->retry(failed);
            QTRY_VERIFY(!h.content.busy());
            QCOMPARE(h.installer->calls.size(), 4);
            QCOMPARE(h.installer->calls.last().first().release.projectId, ids[position]);
            QCOMPARE(h.content.operations()->failedCount(), 0);
        } else {
            h.content.retryOperation(failed);
            QCOMPARE(h.installer->calls.size(), 3);
        }
    }
    void pendingItemsAreNotDuplicatedAndUnrelatedNewUpdatesStillQueue() {
        Harness h;
        h.installer->automatic = false;
        h.set({row("A"), row("B"), row("C")});
        QTRY_VERIFY(!h.content.busy());
        h.content.select("mods/A-old.jar", true);
        h.content.select("mods/B-old.jar", true);
        h.content.updateSelected();
        QCOMPARE(h.content.operations()->activeCount(), 2);
        h.content.updateAll();
        h.content.updateAll();
        h.content.updateSelected();
        QCOMPARE(h.content.operations()->activeCount(), 3);
        QCOMPARE(h.installer->calls.size(), 1);
        QSignalSpy errors(&h.content, &ContentActions::operationFailed);
        h.content.update("mods/A-new.jar");
        QCOMPARE(errors.size(), 1);
        h.installer->finish();
        QTRY_COMPARE(h.installer->calls.size(), 2);
        // The next item sees A's confirmed new bytes, not the pre-bulk inventory.
        QCOMPARE(byId(h.installer->inputs.last().rows, "A").value("version").toString(), QString("2.0"));
        h.installer->finish();
        QTRY_COMPARE(h.installer->calls.size(), 3);
        h.installer->finish();
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.content.updateCount(), 0);
    }
    void cancellationOnlyAffectsTheChosenItem_data() {
        QTest::addColumn<QString>("target");
        for (const auto &id : {"A", "B", "C"})
            QTest::newRow(id) << QString(id);
    }
    void cancellationOnlyAffectsTheChosenItem() {
        QFETCH(QString, target);
        Harness h;
        h.installer->automatic = false;
        h.set({row("A"), row("B"), row("C")});
        QTRY_VERIFY(!h.content.busy());
        h.content.updateAll();
        h.content.cancelOperation(taskId(h.content, target));
        if (target == "A")
            QTRY_COMPARE(h.installer->calls.size(), 2);
        QElapsedTimer timeout;
        timeout.start();
        while (h.content.busy() && timeout.elapsed() < 5000) {
            if (h.installer->completion)
                h.installer->finish();
            QTest::qWait(1);
        }
        QVERIFY(!h.content.busy());
        QCOMPARE(h.content.operations()->failedCount(), 0);
        for (const auto &id : {"A", "B", "C"})
            QCOMPARE(byId(h.files->get(h.temp.path()), id).value("version").toString(),
                     target == id ? "1.0" : "2.0");
    }
    void queuedPolicyOrFileChangesOnlyBlockThatItem_data() {
        QTest::addColumn<QString>("change");
        for (const auto &value : {"primary-lock", "secondary-lock", "freeze", "hash", "missing"})
            QTest::newRow(value) << QString(value);
    }
    void queuedPolicyOrFileChangesOnlyBlockThatItem() {
        QFETCH(QString, change);
        Harness h;
        h.installer->automatic = false;
        h.set({row("A"), row("B"), row("C")});
        QTRY_VERIFY(!h.content.busy());
        h.content.updateAll();
        if (change.endsWith("lock"))
            h.preferences->setLocked(h.temp.path(),
                                     change.startsWith("primary") ? "modrinth:B" : "curseforge:cf-B", true);
        if (change == "freeze")
            h.preferences->setFrozen(h.temp.path(), "modrinth:B", true);
        if (change == "hash")
            h.files->mutate(h.temp.path(), "B",
                            [](QVariantMap &r) { r["sha1"] = hash("externally edited"); });
        if (change == "missing")
            h.files->set(h.temp.path(), {row("A"), row("C")});
        h.installer->finish();
        QTRY_COMPARE(h.installer->calls.size(), 2);
        QCOMPARE(h.installer->calls.last().first().release.projectId, QString("C"));
        h.installer->finish();
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.content.operations()->failedCount(), 1);
        QCOMPARE(byId(h.files->get(h.temp.path()), "C").value("version").toString(), QString("2.0"));
    }
    void emptyProtectedAndFilteredStatesDoNotScheduleWork() {
        Harness h;
        h.set({});
        QTRY_VERIFY(!h.content.busy());
        h.content.updateAll();
        h.content.updateSelected();
        QVERIFY(h.installer->calls.isEmpty());
        h.preferences->setLocked(h.temp.path(), "curseforge:cf-A", true);
        h.set({row("A")});
        QTRY_VERIFY(!h.content.busy());
        h.content.updateAll();
        QCOMPARE(h.content.actionableUpdateCount(), 0);
        QCOMPARE(h.content.updateCount(), 1);
        QVERIFY(h.installer->calls.isEmpty());
        QVERIFY(h.content.notice().isEmpty());
        h.preferences->setLocked(h.temp.path(), "curseforge:cf-A", false);
        h.set({row("A"), row("B", "resourcepacks")});
        QTRY_VERIFY(!h.content.busy());
        h.content.setCategory("datapacks");
        QVERIFY(h.content.rows().isEmpty());
        h.content.updateAll();
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.installer->calls.size(), 2); // Update all is instance-wide, including filtered-out rows.
    }
    void duplicateCompletionsDoNotAdvanceTheQueueTwice() {
        Harness h;
        h.installer->duplicateCallback = true;
        h.set({row("A"), row("B"), row("C")});
        QTRY_VERIFY(!h.content.busy());
        h.content.updateAll();
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.installer->calls.size(), 3);
        QCOMPARE(h.installer->peak, 1);
        QCOMPARE(h.content.updateCount(), 0);
        QCOMPARE(h.content.operations()->failedCount(), 0);
    }
    void transientVerificationFailureIsRecoveredBeforeTheNextItem() {
        Harness h;
        h.installer->afterCommit = [&](const QString &id) {
            if (id == "A")
                h.files->readFailures = 1;
        };
        h.set({row("A"), row("B"), row("C")});
        QTRY_VERIFY(!h.content.busy());
        h.content.updateAll();
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.installer->calls.size(), 3);
        QCOMPARE(h.content.operations()->failedCount(), 1);
        for (const auto &id : {"A", "B", "C"})
            QCOMPARE(byId(h.files->get(h.temp.path()), id).value("version").toString(), QString("2.0"));
        const auto count = h.installer->calls.size();
        h.content.retryOperation(taskId(h.content, "A"));
        QCOMPARE(h.installer->calls.size(),
                 count); // A committed; its failed verification is not safe to retry.
    }
    void changingInstancesKeepsCommandsAndResultsWithTheirOriginalPack() {
        Harness h;
        h.installer->automatic = false;
        h.set({row("A"), row("B")});
        QTRY_VERIFY(!h.content.busy());
        h.content.updateAll();
        const auto other = h.temp.path() + "/other";
        h.files->set(other, {row("Z")});
        h.content.setContext({{"path", other}}, {row("Z")});
        QTRY_COMPARE(h.content.rows().size(), 1);
        h.installer->finish();
        QTRY_COMPARE(h.installer->calls.size(), 2);
        h.installer->finish();
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.content.rows().first().toMap().value("projectId").toString(), QString("Z"));
        QCOMPARE(byId(h.files->get(other), "Z").value("version").toString(), QString("1.0"));
        QCOMPARE(byId(h.files->get(h.temp.path()), "B").value("version").toString(), QString("2.0"));
    }
    void bulkAcceptancePublishesOneProjectionAndKeepsTheModelStable() {
        Harness h;
        h.installer->automatic = false;
        QVariantList rows;
        for (int i = 0; i < 300; ++i)
            rows << row(QString::number(i));
        h.set(rows);
        QTRY_VERIFY(!h.content.busy());
        QSignalSpy published(&h.content, &ContentActions::rowsChanged),
            reset(h.content.model(), &QAbstractItemModel::modelReset);
        QElapsedTimer timer;
        timer.start();
        h.content.updateAll();
        qInfo() << "Accept 300 independent updates ms:" << timer.elapsed();
        QCOMPARE(h.content.operations()->activeCount(), 300);
        QCOMPARE(published.size(), 1);
        QCOMPARE(reset.size(), 0);
        for (const auto &entry : h.content.operations()->entries())
            h.content.cancelOperation(entry.toMap().value("id").toULongLong());
        QTRY_VERIFY(!h.content.busy());
        QCOMPARE(h.installer->calls.size(), 1);
    }
};
QTEST_GUILESS_MAIN(ContentUpdateTests)
#include "ContentUpdateTests.moc"
