#pragma once
#include <QVariantList>
#include <QVariantMap>
namespace studio {
// Reads public content identity/lock fields; explicit lock writes use the separate lock port.
class LauncherContentState final {
  public:
    // Worker-side identity check for existing instance directories, including filesystem aliases.
    static bool sameInstanceDirectory(const QString &, const QString &);
    static QString databasePath(const QVariantMap &pack);
    static QVariantList read(const QVariantMap &pack, QString *error = nullptr);
};
} // namespace studio
