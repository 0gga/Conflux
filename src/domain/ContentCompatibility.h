#pragma once
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
namespace studio {
inline bool hasFabricBridge(const QString &primary, const QVariantList &inventory) {
    if (primary != "forge" && primary != "neoforge")
        return false;
    for (const auto &value : inventory) {
        const auto row = value.toMap();
        if (row.value("enabled").toBool() &&
            (row.value("connectorBridge").toBool() || row.value("modId") == "connector"))
            return true;
    }
    return false;
}
inline QStringList compatibleLoaders(const QString &primary, const QVariantList &inventory) {
    QStringList loaders;
    if (!primary.isEmpty())
        loaders << primary;
    if (hasFabricBridge(primary, inventory))
        loaders << "fabric";
    return loaders;
}
inline bool sharesLoader(const QStringList &candidate, const QStringList &permitted) {
    for (const auto &loader : candidate)
        if (permitted.contains(loader))
            return true;
    return false;
}
inline bool hasDataPackFormatConflict(const QString &targetCategory, const QVariantMap &ids,
                                      const QVariantList &inventory) {
    if (!QStringList{"mods", "datapacks"}.contains(targetCategory))
        return false;
    for (const auto &value : inventory) {
        const auto row = value.toMap();
        const auto category = row.value("category").toString();
        if (!QStringList{"mods", "datapacks"}.contains(category) || category == targetCategory)
            continue;
        const auto installed = row.value("sourceIds").toMap();
        for (auto it = ids.cbegin(); it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty() && installed.value(it.key()) == it.value())
                return true;
    }
    return false;
}
} // namespace studio
