#include "features/content/ContentOperationQueue.h"
#include "features/status/OperationHistory.h"
#include "features/status/RunningInstances.h"
#include "platform/InstanceProcessSource.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace studio;
class StatusTests : public QObject {
    Q_OBJECT
  private slots:
    void queueDiscardPreservesActiveAndOtherInstanceTasks() {
        ContentOperationQueue queue;
        ContentOperationQueue::Completion finish;
        QStringList ran;
        queue.enqueue(
            [&](auto done) {
                ran << "active";
                finish = done;
            },
            "a");
        queue.enqueue(
            [&](auto done) {
                ran << "obsolete";
                done();
            },
            "a");
        queue.enqueue(
            [&](auto done) {
                ran << "other";
                done();
            },
            "b");
        queue.discardQueued("a");
        QVERIFY(queue.busy());
        finish();
        finish();
        QTRY_VERIFY(!queue.busy());
        QCOMPARE(ran, (QStringList{"active", "other"}));
    }
    void activeTasksStayAboveQueuedAndFinishedWithoutResettingTheModel() {
        OperationHistory history;
        QSignalSpy resets(&history, &QAbstractItemModel::modelReset);
        history.queued(1, {}, "First", "Update");
        QPersistentModelIndex first(history.index(0));
        history.start(1);
        history.queued(2, {}, "Second", "Update");
        history.queued(3, {}, "Third", "Update");
        QCOMPARE(history.entries().first().toMap().value("id").toULongLong(), quint64(1));
        history.finish(1, false, "Failure");
        history.start(2);
        QCOMPARE(history.entries()[0].toMap().value("id").toULongLong(), quint64(2));
        QCOMPARE(history.entries()[1].toMap().value("id").toULongLong(), quint64(3));
        QCOMPARE(history.entries()[2].toMap().value("id").toULongLong(), quint64(1));
        QCOMPARE(first.row(), 2);
        history.queued(4, {}, "Fourth", "Update");
        QCOMPARE(history.entries().first().toMap().value("id").toULongLong(), quint64(2));
        history.progress(2, "Downloading");
        QCOMPARE(history.entries().first().toMap().value("message").toString(), QString("Downloading"));
        QCOMPARE(resets.size(), 0);
    }
    void clearingFinishedPreservesAllActiveWorkAndRetiresFailurePayloads() {
        OperationHistory history;
        for (quint64 id = 1; id <= 4; ++id)
            history.queued(id, {}, "Fixture", "Update");
        history.start(3);
        history.finish(1, false, "Failure");
        history.finish(2, true, {});
        QSignalSpy retired(&history, &OperationHistory::dismissed),
            changes(&history, &OperationHistory::changed);
        history.clearFinished();
        QCOMPARE(history.rowCount(), 2);
        QCOMPARE(history.activeCount(), 2);
        QCOMPARE(history.failedCount(), 0);
        QCOMPARE(retired.size(), 2);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(history.entries().first().toMap().value("id").toULongLong(), quint64(3));
        history.clearFinished();
        QCOMPARE(changes.size(), 1);
        history.finish(3, false, "Again");
        history.clearFinished();
        QCOMPARE(history.rowCount(), 1);
        QCOMPARE(history.entries().first().toMap().value("id").toULongLong(), quint64(4));
    }
    void largeQueuesKeepActiveTasksAndOnlyTheLatestHundredFailures() {
        OperationHistory history;
        QSignalSpy retired(&history, &OperationHistory::dismissed),
            resets(&history, &QAbstractItemModel::modelReset);
        for (quint64 id = 1; id <= 350; ++id)
            history.queued(id, {}, QString::number(id), "Update");
        history.start(350);
        for (quint64 id = 1; id <= 300; ++id)
            history.finish(id, false, QString("Failure %1").arg(id));
        QCOMPARE(history.activeCount(), 50);
        QCOMPARE(history.failedCount(), 100);
        QCOMPARE(history.rowCount(), 150);
        QCOMPARE(retired.size(), 200);
        QCOMPARE(resets.size(), 0);
        QVERIFY(history.details(1).isEmpty());
        QVERIFY(history.details(201).contains("Failure 201"));
        QVERIFY(!history.details(350).isEmpty());
        history.dismiss(350);
        QCOMPARE(history.activeCount(), 50);
    }
    void taskCompletionPublishesStateAndMessageTogether() {
        OperationHistory history;
        history.queued(1, {}, "Fixture", "Update");
        QSignalSpy changes(&history, &OperationHistory::changed);
        connect(&history, &OperationHistory::changed, &history, [&] {
            const auto row = history.entries().first().toMap();
            if (row.value("state") == "failed") {
                QCOMPARE(row.value("message").toString(), QString("Failure"));
                QVERIFY(row.value("retryable").toBool());
            }
        });
        history.finish(1, false, "Failure");
        QCOMPARE(changes.size(), 1);
    }
    void taskFailuresKeepDetailsAndRetryIntentUntilDismissed() {
        OperationHistory history;
        QSignalSpy retry(&history, &OperationHistory::retryRequested);
        history.queued(1, {{"title", "Instance"}, {"path", "instance"}}, "Trident", "Update");
        QCOMPARE(history.activeCount(), 1);
        QSignalSpy resets(&history, &QAbstractItemModel::modelReset);
        QPersistentModelIndex stable(history.index(0));
        history.start(1);
        history.progress(1, "Downloading");
        QCOMPARE(history.entries().first().toMap().value("message").toString(), QString("Downloading"));
        QVERIFY(stable.isValid());
        QCOMPARE(resets.count(), 0);
        history.finish(1, false, "Fixture failure");
        QCOMPARE(history.activeCount(), 0);
        QCOMPARE(history.failedCount(), 1);
        QVERIFY(history.details(1).contains("Fixture failure"));
        history.retry(1);
        QCOMPARE(retry.count(), 1);
        history.dismiss(1);
        QVERIFY(history.entries().isEmpty());
    }
    void runningStatusRefreshesInWorkersAndPreservesLastKnownStateOnErrors() {
        class Source : public IInstanceProcessSource {
          public:
            mutable int calls = 0;
            RunningInstanceResult scan(const QVariantList &packs) const override {
                ++calls;
                if (calls == 1)
                    return {packs, {}};
                return {{}, "Status unavailable"};
            }
        };
        auto source = std::make_shared<Source>();
        RunningInstances running(source);
        running.setPacks({QVariantMap{{"path", "fixture"}, {"title", "Fixture"}}});
        QTRY_COMPARE(running.instances().size(), 1);
        running.refresh();
        QTRY_VERIFY(!running.error().isEmpty());
        QCOMPARE(running.instances().size(), 1);
    }
    void nativeDetectionMatchesTheActualGameDirectoryAndNeverReturnsArguments() {
#ifndef Q_OS_WIN
        QSKIP("Windows process adapter");
#else
        QTemporaryDir temp;
        QDir().mkpath(temp.path() + "/instance with spaces");
        const auto program = temp.path() + "/java.exe";
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), program));
        QProcess process;
        process.start(program, {"--process-probe", "--gameDir", temp.path() + "/instance with spaces",
                                "--accessToken", "fixture-not-for-output"});
        QVERIFY(process.waitForStarted(5000));
        InstanceProcessSource source;
        QElapsedTimer first;
        first.start();
        const auto result =
            source.scan({QVariantMap{{"path", temp.path() + "/instance with spaces"}, {"title", "Fixture"}}});
        const auto firstMs = first.elapsed();
        QElapsedTimer cached;
        cached.start();
        const auto repeated =
            source.scan({QVariantMap{{"path", temp.path() + "/instance with spaces"}, {"title", "Fixture"}}});
        qInfo() << "Native status probe ms first/cached:" << firstMs << cached.elapsed();
        QCOMPARE(repeated.instances, result.instances);
        const auto expectedPid = process.processId();
        process.terminate();
        if (!process.waitForFinished(2000)) {
            process.kill();
            process.waitForFinished(1000);
        }
        const auto stopped = source.scan({QVariantMap{{"path", temp.path() + "/instance with spaces"}}});
        QVERIFY(stopped.instances.isEmpty());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.instances.size(), 1);
        QCOMPARE(result.instances.first().toMap().value("pid").toLongLong(), qint64(expectedPid));
        QVERIFY(!result.instances.first().toMap().contains("arguments"));
        QVERIFY(!result.instances.first().toMap().contains("accessToken"));
#endif
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--process-probe")) {
        QTimer::singleShot(15000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    StatusTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "StatusTests.moc"
