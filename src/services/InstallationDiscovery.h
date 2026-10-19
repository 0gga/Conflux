#pragma once
#include <QString>
#include <QVariantList>
namespace studio {
struct DiscoveryResult { QVariantList installations; QStringList warnings; };
class InstallationDiscovery {
public:
    static DiscoveryResult scan(const QString& modrinthRoot, const QString& curseforgeRoot);
};
}
