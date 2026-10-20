#pragma once
#include "domain/Catalogue.h"
#include <functional>
namespace studio {
struct InstalledModIdentity {
    QString sha1, error;
    QMap<QString, QString> sourceIds;
    QString projectUrl;
};
class IInstalledModIdentity {
  public:
    virtual ~IInstalledModIdentity() = default;
    virtual InstalledModIdentity inspect(const QVariantMap &pack, const QVariantMap &file) const = 0;
};
class IProjectShowcaseSource {
  public:
    using ResolveCallback = std::function<void(Project, QString)>;
    using DetailCallback = std::function<void(ProjectDetail)>;
    virtual ~IProjectShowcaseSource() = default;
    virtual void resolve(const InstalledModIdentity &, ResolveCallback) = 0;
    virtual void load(const Project &, const QString &game, const QString &loader, DetailCallback) = 0;
    virtual void setCurseforgeKey(const QByteArray &) = 0;
};
} // namespace studio
