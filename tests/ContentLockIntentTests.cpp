#include "features/content/ContentLockIntent.h"
#include <QtTest>
using namespace studio;
class ContentLockIntentTests final : public QObject {
    Q_OBJECT
    static QVariantMap row() {
        return {{"relativePath", "mods/exact.jar.disabled"},
                {"category", "mods"},
                {"sourceIds", QVariantMap{{"modrinth", "exact"}, {"curseforge", "42"}}},
                {"sha1", QString(40, 'a')},
                {"opaque", QVariantMap{{"retain", true}}}};
    }
  private slots:
    void recognizedSourcesKeepExactLockAndUnlockIntent_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<bool>("locked");
        QTest::addColumn<bool>("sourceOnly");
        for (const auto &provider : {"modrinth", "curseforge"})
            for (bool locked : {false, true})
                for (bool sourceOnly : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(provider).arg(locked).arg(sourceOnly)))
                        << QString(provider) << locked << sourceOnly;
    }
    void recognizedSourcesKeepExactLockAndUnlockIntent() {
        QFETCH(QString, provider);
        QFETCH(bool, locked);
        QFETCH(bool, sourceOnly);
        const QVariantMap pack{{"provider", provider}, {"path", "C:/Instances/original"}};
        auto original = row();
        original["launcherLocked"] = true; // A protected source entry can still be explicitly unlocked.
        const QVariantList rows{original};
        const auto intent = planContentLockIntent(pack, rows, locked, sourceOnly);
        QCOMPARE(intent.route, ContentLockIntent::Source);
        QVERIFY(intent.error.isEmpty());
        QCOMPARE(intent.operation.kind, ContentOperation::Lock);
        QCOMPARE(intent.operation.packPath, pack.value("path").toString());
        QCOMPARE(intent.operation.locked, locked);
        QVERIFY(intent.operation.expectedHashes.isEmpty()); // Execution owns verification.
        if (sourceOnly) {
            QCOMPARE(intent.operation.sourceLock, original);
            QVERIFY(intent.operation.targets.isEmpty());
        } else {
            QCOMPARE(intent.operation.targets, QStringList{contentRowKey(original)});
            QVERIFY(intent.operation.sourceLock.isEmpty());
        }
        QCOMPARE(rows, QVariantList{original});
    }
    void localPreferenceIntentDoesNotCreateAFileOperation_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<bool>("locked");
        for (const auto &provider : {"conflux", ""})
            for (bool locked : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(provider).arg(locked)))
                    << QString(provider) << locked;
    }
    void localPreferenceIntentDoesNotCreateAFileOperation() {
        QFETCH(QString, provider);
        QFETCH(bool, locked);
        const auto intent =
            planContentLockIntent({{"provider", provider}, {"path", "original"}}, {row()}, locked);
        QCOMPARE(intent.route, ContentLockIntent::Local);
        QVERIFY(intent.error.isEmpty());
        QVERIFY(intent.operation.packPath.isEmpty());
        QVERIFY(intent.operation.targets.isEmpty());
        QVERIFY(intent.operation.sourceLock.isEmpty());
    }
    void emptySelectionDoesNothingForEveryRoute() {
        for (const auto &provider : {"modrinth", "curseforge", "conflux", ""})
            for (bool locked : {false, true}) {
                const auto intent = planContentLockIntent({{"provider", provider}}, {}, locked, true);
                QCOMPARE(intent.route, ContentLockIntent::Ignore);
                QVERIFY(intent.error.isEmpty());
                QVERIFY(intent.operation.targets.isEmpty());
            }
    }
    void anyPendingRowRejectsTheWholeSelectionBeforePlanning() {
        auto pending = row();
        pending["pending"] = true;
        pending["launcherLocked"] = true;
        for (const auto &provider : {"modrinth", "curseforge", "conflux", ""})
            for (bool locked : {false, true}) {
                const auto intent = planContentLockIntent({{"provider", provider}}, {row(), pending}, locked);
                QCOMPARE(intent.route, ContentLockIntent::Reject);
                QVERIFY(intent.error.contains("active or queued"));
                QVERIFY(intent.operation.targets.isEmpty());
                QVERIFY(intent.operation.sourceLock.isEmpty());
            }
    }
    void unavailableSourceProtectionRejectsLocalLockAndUnlock() {
        auto protectedRow = row();
        protectedRow["launcherLocked"] = true;
        for (const auto &provider : {"conflux", "", "unrecognized"})
            for (bool locked : {false, true}) {
                const auto intent =
                    planContentLockIntent({{"provider", provider}}, {row(), protectedRow}, locked);
                QCOMPARE(intent.route, ContentLockIntent::Reject);
                QVERIFY(intent.error.contains("unavailable"));
                QVERIFY(intent.operation.targets.isEmpty());
            }
    }
    void sourceBatchUsesStableContentKeysInSelectionOrder() {
        auto first = row(), second = row();
        first["contentKey"] = "stable:first";
        second["relativePath"] = "resourcepacks/second.zip";
        second["category"] = "resourcepacks";
        second["sourceIds"] = QVariantMap{{"curseforge", "99"}};
        const auto intent =
            planContentLockIntent({{"provider", "curseforge"}, {"path", "original"}}, {first, second}, true);
        QCOMPARE(intent.route, ContentLockIntent::Source);
        QCOMPARE(intent.operation.targets, (QStringList{"stable:first", contentRowKey(second)}));
        QVERIFY(intent.operation.requests.isEmpty());
        QVERIFY(intent.operation.predictions.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ContentLockIntentTests)
#include "ContentLockIntentTests.moc"
