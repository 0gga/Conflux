#pragma once
#include "features/showcase/ProjectShowcaseSource.h"
#include "platform/PackFileStore.h"
#include <QMutex>
#include <memory>
namespace studio {
class NativeInstalledModIdentity final : public IInstalledModIdentity {
  public:
    explicit NativeInstalledModIdentity(std::shared_ptr<const IPackFileReader> files)
        : m_files(std::move(files)) {}
    InstalledModIdentity inspect(const QVariantMap &, const QVariantMap &) const override;
    InstalledModIdentity inspectVerified(const QVariantMap &, const QVariantMap &,
                                         const QString &) const override;

  private:
    std::shared_ptr<const IPackFileReader> m_files;
    mutable QMutex m_mutex;
    mutable QVariantList m_pins;
    mutable QHash<QString, QMap<QString, QString>> m_pinIds;
};
} // namespace studio
