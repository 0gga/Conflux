#include "ContentTime.h"
#include <QDateTime>
#include <QLocale>
#include <QtTest>
using namespace studio;
class ContentTimeTests final : public QObject {
    Q_OBJECT
    static qint64 reference() {
        return QDateTime::fromString("2026-10-22T12:00:00Z", Qt::ISODate).toMSecsSinceEpoch();
    }
  private slots:
    void elapsedHoursAndCoarseOlderAges_data() {
        QTest::addColumn<qint64>("elapsed");
        QTest::addColumn<QString>("expected");
        const qint64 hour = 3600000, day = 24 * hour;
        const QList<QPair<qint64, QString>> rows{
            {0, "<1 hour ago"},           {hour - 1, "<1 hour ago"},  {hour, "1 hour ago"},
            {2 * hour - 1, "1 hour ago"}, {2 * hour, "2 hours ago"},  {day - 1, "23 hours ago"},
            {day, "24 hours ago"},        {day + 1, "1 day ago"},     {2 * day, "2 days ago"},
            {59 * day, "59 days ago"},    {60 * day, "2 months ago"}, {729 * day, "24 months ago"},
            {730 * day, "2 years ago"}};
        for (const auto &row : rows)
            QTest::newRow(qPrintable(QString::number(row.first))) << row.first << row.second;
    }
    void elapsedHoursAndCoarseOlderAges() {
        QFETCH(qint64, elapsed);
        QFETCH(QString, expected);
        ContentTime time;
        const auto value =
            QDateTime::fromMSecsSinceEpoch(reference() - elapsed, Qt::UTC).toString(Qt::ISODateWithMs);
        QCOMPARE(time.age(value, reference()), expected);
        const auto label = time.dateLabel(value, reference());
        if (elapsed <= 86400000) {
            QVERIFY(label.endsWith(" · " + expected));
            QVERIFY(time.updatedHint(value, reference()).endsWith(" · " + expected));
        } else {
            QVERIFY(!label.contains("ago"));
            QVERIFY(!time.updatedHint(value, reference()).contains("ago"));
        }
    }
    void equivalentOffsetsAndSubsecondsKeepExactElapsedAge() {
        ContentTime time;
        for (const auto &value : {"2026-10-22T09:00:00Z", "2026-10-22T11:00:00+02:00",
                                  "2026-10-22T04:00:00-05:00", "2026-10-22T09:00:00.000000+00:00"}) {
            QCOMPARE(time.age(value, reference()), QString("3 hours ago"));
            QVERIFY(time.dateLabel(value, reference()).endsWith(" · 3 hours ago"));
        }
    }
    void midnightAndDstDoNotReplaceElapsedHoursWithCalendarDays() {
        ContentTime time;
        const auto afterMidnight =
            QDateTime::fromString("2026-10-23T00:30:00Z", Qt::ISODate).toMSecsSinceEpoch();
        QCOMPARE(time.age("2026-10-22T23:30:00Z", afterMidnight), QString("1 hour ago"));
        const auto afterDst = QDateTime::fromString("2026-10-25T12:00:00Z", Qt::ISODate).toMSecsSinceEpoch();
        QCOMPARE(time.age("2026-10-24T13:00:00Z", afterDst), QString("23 hours ago"));
    }
    void missingPrecisionFutureAndInvalidValuesNeverInventAnHourlyAge() {
        ContentTime time;
        for (const auto &value : {"", "invalid", "2026-99-99T12:00:00Z"}) {
            QVERIFY(time.age(value, reference()).isEmpty());
            QVERIFY(time.dateLabel(value, reference()).isEmpty());
            QVERIFY(time.updatedHint(value, reference()).isEmpty());
        }
        for (const auto &value : {"2026-10-22", "2026-10-22T13:00:00Z"}) {
            const auto date = QDateTime::fromString(value, Qt::ISODateWithMs);
            QVERIFY(date.isValid());
            const auto expected = QLocale().toString(date.toLocalTime(), "d. MMM yyyy");
            QCOMPARE(time.age(value, reference()), expected);
            QCOMPARE(time.dateLabel(value, reference()), expected);
            QVERIFY(!time.updatedHint(value, reference()).contains("ago"));
        }
        QCOMPARE(time.updatedHint("2026-10-22", reference()),
                 "Updated " + time.dateLabel("2026-10-22", reference()));
        const auto old = QDateTime::fromString("2026-09-17T05:16:33Z", Qt::ISODate);
        QCOMPARE(time.updatedHint("2026-09-17T05:16:33Z", reference()),
                 "Updated " + QLocale().toString(old.toLocalTime(), "d. MMM yyyy - h:mm AP"));
    }
    void sharedClockTicksCoarselyAndNotifiesWithoutPerCardTimers() {
        ContentTime time;
        const auto before = QDateTime::currentMSecsSinceEpoch();
        const auto now = time.now();
        QVERIFY(now >= before && now <= QDateTime::currentMSecsSinceEpoch());
        const auto timers = time.findChildren<QTimer *>();
        QCOMPARE(timers.size(), 1);
        QCOMPARE(timers.first()->interval(), 60000);
        QVERIFY(timers.first()->isActive());
        QSignalSpy changed(&time, &ContentTime::changed);
        QVERIFY(QMetaObject::invokeMethod(timers.first(), "timeout"));
        QCOMPARE(changed.size(), 1);
    }
};
QTEST_GUILESS_MAIN(ContentTimeTests)
#include "ContentTimeTests.moc"
