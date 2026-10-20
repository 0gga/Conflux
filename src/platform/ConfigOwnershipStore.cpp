#include "ConfigOwnershipStore.h"
#include <QCryptographicHash>
namespace studio {
SettingsConfigOwnershipStore::SettingsConfigOwnershipStore(const QString &file)
    : m_settings(file.isEmpty() ? std::make_unique<QSettings>()
                                : std::make_unique<QSettings>(file, QSettings::IniFormat)) {}
QString SettingsConfigOwnershipStore::key(const QString &pack, const QString &path) {
    return "configOwners/" +
           QString::fromLatin1(
               QCryptographicHash::hash((pack + '\n' + path).toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString SettingsConfigOwnershipStore::owner(const QString &pack, const QString &path) const {
    return m_settings->value(key(pack, path)).toString();
}
void SettingsConfigOwnershipStore::setOwner(const QString &pack, const QString &path, const QString &id) {
    m_settings->setValue(key(pack, path), id);
}
} // namespace studio
