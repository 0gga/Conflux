#include "ContentTime.h"
#include <QDateTime>
#include <QLocale>
namespace studio {
namespace {
constexpr qint64 hour = 3600000, day = 24 * hour;
QString recentAge(const QString &value, const QDateTime &date, qint64 nowMs) {
    // A calendar date alone cannot establish an elapsed-hour age.
    if (!value.contains('T', Qt::CaseInsensitive))
        return {};
    const auto elapsed = nowMs - date.toMSecsSinceEpoch();
    if (elapsed < 0 || elapsed > day)
        return {};
    if (elapsed < hour)
        return "<1 hour ago";
    const auto hours = elapsed / hour;
    return QString::number(hours) + (hours == 1 ? " hour ago" : " hours ago");
}
QString calendarDate(const QDateTime &date) {
    return QLocale().toString(date.toLocalTime(), "d. MMM yyyy");
}
} // namespace
ContentTime::ContentTime(QObject *parent) : QObject(parent), m_timer(this) {
    m_timer.setInterval(60000);
    connect(&m_timer, &QTimer::timeout, this, &ContentTime::changed);
    m_timer.start();
}
qint64 ContentTime::now() const {
    return QDateTime::currentMSecsSinceEpoch();
}
QString ContentTime::age(const QString &value, qint64 nowMs) const {
    const auto date = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!date.isValid())
        return {};
    const auto recent = recentAge(value, date, nowMs);
    if (!recent.isEmpty())
        return recent;
    if (!value.contains('T', Qt::CaseInsensitive) || nowMs < date.toMSecsSinceEpoch())
        return calendarDate(date);
    const auto days = (nowMs - date.toMSecsSinceEpoch()) / day;
    if (days == 1)
        return "1 day ago";
    if (days < 60)
        return QString::number(days) + " days ago";
    if (days < 730)
        return QString::number(days / 30) + " months ago";
    return QString::number(days / 365) + " years ago";
}
QString ContentTime::dateLabel(const QString &value, qint64 nowMs) const {
    const auto date = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!date.isValid())
        return {};
    const auto recent = recentAge(value, date, nowMs);
    return calendarDate(date) + (recent.isEmpty() ? QString{} : " · " + recent);
}
QString ContentTime::updatedHint(const QString &value, qint64 nowMs) const {
    const auto date = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!date.isValid())
        return {};
    if (!recentAge(value, date, nowMs).isEmpty() || !value.contains('T', Qt::CaseInsensitive))
        return "Updated " + dateLabel(value, nowMs);
    return "Updated " + QLocale().toString(date.toLocalTime(), "d. MMM yyyy - h:mm AP");
}
} // namespace studio
