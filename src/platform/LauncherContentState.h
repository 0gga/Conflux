#pragma once
#include <QVariantList>
#include <QVariantMap>
namespace studio {
// Reads only public content identity/lock fields; launcher databases are never written.
class LauncherContentState final {
  public:
    static QVariantList read(const QVariantMap &pack, QString *error = nullptr);
};
} // namespace studio
