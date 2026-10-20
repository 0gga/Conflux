#include "BrowseFilterPreferences.h"
#include <QCryptographicHash>
namespace studio {
BrowseFilterPreferences::BrowseFilterPreferences(std::shared_ptr<IBrowseSettingsStore> store)
    : m_store(std::move(store)) {}
QString BrowseFilterPreferences::key(const QString &instance, const QString &type) {
    return "browse/compatibility/" +
           QString::fromLatin1(
               QCryptographicHash::hash(instance.toUtf8(), QCryptographicHash::Sha256).toHex()) +
           '/' + type;
}
std::optional<BrowseCompatibility> BrowseFilterPreferences::load(const QString &instance,
                                                                 const QString &type) const {
    const auto value = m_store->value(key(instance, type)).toMap();
    if (value.value("schema").toInt() != 1)
        return std::nullopt;
    return BrowseCompatibility{value.value("versions").toStringList(), value.value("loaders").toStringList(),
                               value.value("unlocked").toBool(),
                               value.value("defaultLoaders", true).toBool()};
}
void BrowseFilterPreferences::save(const QString &instance, const QString &type,
                                   const BrowseCompatibility &value) {
    m_store->save(key(instance, type), QVariantMap{{"schema", 1},
                                                   {"versions", value.versions},
                                                   {"loaders", value.loaders},
                                                   {"unlocked", value.versionUnlocked},
                                                   {"defaultLoaders", value.defaultLoaders}});
}
} // namespace studio
