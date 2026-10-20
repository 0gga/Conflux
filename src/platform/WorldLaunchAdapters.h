#pragma once
#include "features/launch/WorldLaunch.h"
namespace studio {
class ModrinthWorldLaunchAdapter final : public IWorldLaunchAdapter {
  public:
    bool supports(const QVariantMap &) const override;
    QString unavailableReason(const QVariantMap &) const override;
    QUrl worldUrl(const QVariantMap &, const QString &) const override;
};
class DesktopLaunchUrlOpener final : public ILaunchUrlOpener {
  public:
    bool available(const QString &) const override;
    bool open(const QUrl &) override;
};
} // namespace studio
