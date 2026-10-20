#include "WorldLaunchAdapters.h"
#include <QDesktopServices>
#include <QRegularExpression>
#include <QSettings>
#include <QUrlQuery>
namespace studio {
bool ModrinthWorldLaunchAdapter::supports(const QVariantMap &pack) const {
    return pack.value("provider") == "modrinth";
}
QString ModrinthWorldLaunchAdapter::unavailableReason(const QVariantMap &pack) const {
    if (pack.value("launcherInstanceId").toString().isEmpty())
        return "This Modrinth installation has no verified launcher instance ID. Refresh the Library in a "
               "current Modrinth App.";
    const auto version = pack.value("minecraft", pack.value("gameVersion")).toString();
    const auto match = QRegularExpression("^1\\.(\\d+)(?:\\.\\d+)?$").match(version);
    if (!match.hasMatch() || match.captured(1).toInt() < 20)
        return "Direct singleplayer world launch requires a known Minecraft version of 1.20 or newer.";
    return {};
}
QUrl ModrinthWorldLaunchAdapter::worldUrl(const QVariantMap &pack, const QString &world) const {
    QUrl url("modrinth://launch/instance/" +
             QString::fromLatin1(QUrl::toPercentEncoding(pack.value("launcherInstanceId").toString())));
    // Modrinth parses the query with application/x-www-form-urlencoded rules.
    if (!world.isEmpty())
        url.setQuery("singleplayer_world=" + QString::fromLatin1(QUrl::toPercentEncoding(world)));
    return url;
}
bool DesktopLaunchUrlOpener::available(const QString &scheme) const {
    if (scheme != "modrinth")
        return false;
#ifdef Q_OS_WIN
    QSettings handler("HKEY_CLASSES_ROOT\\modrinth\\shell\\open\\command", QSettings::NativeFormat);
    return !handler.value(".").toString().isEmpty();
#else
    return true;
#endif
}
bool DesktopLaunchUrlOpener::open(const QUrl &url) {
    return QDesktopServices::openUrl(url);
}
} // namespace studio
