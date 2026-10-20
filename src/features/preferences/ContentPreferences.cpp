#include "ContentPreferences.h"
#include <QCryptographicHash>
namespace studio {
SettingsContentPreferences::SettingsContentPreferences(const QString &path)
    : m_settings(path.isEmpty() ? std::make_unique<QSettings>("ModpackStudio", "ModpackStudio")
                                : std::make_unique<QSettings>(path, QSettings::IniFormat)) {}
QString SettingsContentPreferences::key(const QString &pack, const QString &identity) {
    return "content/frozen/" + QString::fromLatin1(QCryptographicHash::hash((pack + '\n' + identity).toUtf8(),
                                                                            QCryptographicHash::Sha256)
                                                       .toHex());
}
bool SettingsContentPreferences::frozen(const QString &pack, const QString &identity) const {
    return m_settings->value(key(pack, identity), false).toBool();
}
bool SettingsContentPreferences::hasFrozenPreference(const QString &pack, const QString &identity) const {
    return m_settings->contains(key(pack, identity));
}
void SettingsContentPreferences::setFrozen(const QString &pack, const QString &identity, bool value) {
    m_settings->setValue(key(pack, identity), value);
}
} // namespace studio
