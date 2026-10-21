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
bool SettingsContentPreferences::locked(const QString &pack, const QString &identity) const {
    return m_settings->value(QString(key(pack, identity)).replace("/frozen/", "/locked/"), false).toBool();
}
void SettingsContentPreferences::setLocked(const QString &pack, const QString &identity, bool value) {
    m_settings->setValue(QString(key(pack, identity)).replace("/frozen/", "/locked/"), value);
}
qint64 SettingsContentPreferences::updatedAt(const QString &pack, const QString &identity) const {
    return m_settings->value(QString(key(pack, identity)).replace("/frozen/", "/updated/"), 0).toLongLong();
}
void SettingsContentPreferences::setUpdatedAt(const QString &pack, const QString &identity, qint64 value) {
    m_settings->setValue(QString(key(pack, identity)).replace("/frozen/", "/updated/"), value);
}
} // namespace studio
