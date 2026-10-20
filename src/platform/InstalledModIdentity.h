#pragma once
#include "features/showcase/ProjectShowcaseSource.h"
#include "platform/PackFileStore.h"
#include <memory>
namespace studio {
class NativeInstalledModIdentity final : public IInstalledModIdentity {
  public:
    explicit NativeInstalledModIdentity(std::shared_ptr<const IPackFileReader> files)
        : m_files(std::move(files)) {}
    InstalledModIdentity inspect(const QVariantMap &, const QVariantMap &) const override;

  private:
    std::shared_ptr<const IPackFileReader> m_files;
};
} // namespace studio
