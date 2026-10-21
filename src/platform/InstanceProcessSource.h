#pragma once
#include "features/status/RunningInstances.h"
#include <QJsonArray>
#include <QMutex>
namespace studio {
class InstanceProcessSource final : public IInstanceProcessSource {
  public:
    RunningInstanceResult scan(const QVariantList &) const override;

  private:
    mutable QMutex m_scanMutex;
    mutable QStringList m_processFingerprint;
    mutable QJsonArray m_processDirectories;
};
} // namespace studio
