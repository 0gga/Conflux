#pragma once
#include "features/content/ContentPorts.h"
namespace studio {
// Explicit content-lock toggles are the only launcher metadata writes authorized here.
class LauncherContentLocks final : public ILauncherContentLocks {
  public:
    explicit LauncherContentLocks(QString backupRoot = {}) : m_backupRoot(std::move(backupRoot)) {}
    ContentWriteResult setLocked(const QVariantMap &, const QVariantList &, bool) const override;

  private:
    QString m_backupRoot;
};
} // namespace studio
