#pragma once
#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>
namespace studio {
// Pure adapter for CurseForge's existing per-addon metadata. No filesystem writes.
class CurseforgeContentState final {
  public:
    static QVariantList read(const QByteArray &, const QVariantMap &pack, QString *error = nullptr);
    static QByteArray setLocked(const QByteArray &, const QVariantMap &pack, const QVariantList &targets,
                                bool locked, QString *error);
};
} // namespace studio
