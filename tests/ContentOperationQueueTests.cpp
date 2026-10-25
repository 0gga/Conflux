#include "features/content/ContentOperationQueue.h"
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>
#include <memory>
using namespace studio;
class ContentOperationQueueTests final : public QObject {
    Q_OBJECT
  private slots:
    void fifoAndCompletionAreIndependentOfDiscardedGroups() {
        ContentOperationQueue queue;
        QSignalSpy changes(&queue, &ContentOperationQueue::changed);
        QVERIFY(!queue.busy());
        ContentOperationQueue::Completion first, second;
        QStringList started;
        queue.enqueue(
            [&](auto done) {
                started << "first";
                first = done;
            },
            "keep");
        queue.enqueue([&](auto) { started << "discarded"; }, "discard");
        queue.enqueue(
            [&](auto done) {
                started << "second";
                second = done;
            },
            "keep");
        QVERIFY(queue.busy());
        queue.discardQueued("absent");
        QCOMPARE(changes.size(), 1);
        queue.discardQueued("discard");
        QCOMPARE(changes.size(), 2);
        QCOMPARE(started, QStringList{"first"});
        first();
        first();
        QTRY_COMPARE(started, (QStringList{"first", "second"}));
        first(); // A late duplicate must not advance past a different active task.
        QTest::qWait(5);
        QVERIFY(queue.busy());
        second();
        second();
        QTRY_VERIFY(!queue.busy());
        QCOMPARE(started.size(), 2);
    }
    void discardAllDoesNotCancelTheActiveTask() {
        ContentOperationQueue queue;
        ContentOperationQueue::Completion finish;
        int calls = 0;
        queue.enqueue([&](auto done) {
            ++calls;
            finish = done;
        });
        queue.enqueue([&](auto) { ++calls; });
        queue.discardQueued();
        QCOMPARE(calls, 1);
        QVERIFY(queue.busy());
        finish();
        QTRY_VERIFY(!queue.busy());
        QCOMPARE(calls, 1);
    }
    void destructionDuringDispatchNotificationDoesNotStartWork() {
        QPointer<ContentOperationQueue> queue = new ContentOperationQueue;
        bool started = false;
        connect(queue, &ContentOperationQueue::changed, this, [&] { delete queue.data(); });
        queue->enqueue([&](auto) { started = true; });
        QVERIFY(queue.isNull());
        QVERIFY(!started);
    }
    void completionAfterDestructionIsHarmless() {
        auto queue = std::make_unique<ContentOperationQueue>();
        ContentOperationQueue::Completion finish;
        queue->enqueue([&](auto done) { finish = done; });
        queue.reset();
        finish();
        finish();
        QCoreApplication::processEvents();
    }
    void destructionAfterCompletionDropsScheduledDispatch() {
        auto queue = std::make_unique<ContentOperationQueue>();
        bool next = false;
        queue->enqueue([](auto done) { done(); });
        queue->enqueue([&](auto) { next = true; });
        queue.reset();
        QCoreApplication::processEvents();
        QVERIFY(!next);
    }
    void reentrantNotificationsMayEnqueueAndDiscardWaitingWork() {
        ContentOperationQueue queue;
        QStringList started;
        bool observed = false;
        connect(&queue, &ContentOperationQueue::changed, this, [&] {
            if (observed || !queue.busy())
                return;
            observed = true;
            queue.enqueue([&](auto) { started << "discarded"; }, "discard");
            queue.enqueue(
                [&](auto done) {
                    started << "second";
                    done();
                },
                "keep");
            queue.discardQueued("discard");
        });
        queue.enqueue([&](auto done) {
            started << "first";
            done();
        });
        QTRY_VERIFY(!queue.busy());
        QCOMPARE(started, (QStringList{"first", "second"}));
    }
    void synchronousCompletionsYieldAndNeverRecursivelyDispatch() {
        ContentOperationQueue queue;
        ContentOperationQueue::Completion first;
        int calls = 0, depth = 0, maximumDepth = 0;
        queue.enqueue([&](auto done) { first = done; });
        for (int i = 0; i < 2000; ++i)
            queue.enqueue([&](auto done) {
                maximumDepth = qMax(maximumDepth, ++depth);
                ++calls;
                done();
                --depth;
            });
        first();
        QCOMPARE(calls, 0); // Completion always yields before the next accepted task.
        QTRY_VERIFY_WITH_TIMEOUT(!queue.busy(), 10000);
        QCOMPARE(calls, 2000);
        QCOMPARE(maximumDepth, 1);
    }
};
QTEST_GUILESS_MAIN(ContentOperationQueueTests)
#include "ContentOperationQueueTests.moc"
