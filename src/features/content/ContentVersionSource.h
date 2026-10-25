#pragma once
#include "domain/Catalogue.h"
#include <functional>
namespace studio {
// Version reads have their own cancellation scope, separate from identification,
// update checks and mutations. Already-delivered callbacks may still arrive.
class IContentVersionSource {
  public:
    using VersionsCallback = std::function<void(QVector<Release>, QString)>;
    virtual ~IContentVersionSource() = default;
    virtual void versions(const QVariantMap &, const QString &game, const QString &loaders,
                          VersionsCallback) = 0;
    virtual void cancelVersions() {}
};
} // namespace studio
