#include "ContentPreferences.h"
#include <QCryptographicHash>
namespace studio {
SettingsContentPreferences::SettingsContentPreferences(const QString &path)
    : m_settings(path.isEmpty() ? std::make_unique<QSettings>("ModpackStudio", "ModpackStudio")
                                : std::make_unique<QSettings>(path, QSettings::IniFormat)) {}
QString SettingsContentPreferences::key(const QString &pack, const QString &identity, Field field) const {
    const auto select = [field](const Keys &keys) {
        switch (field) {
        case Field::Frozen:
            return keys.frozen;
        case Field::Locked:
            return keys.locked;
        case Field::Updated:
            return keys.updated;
        }
        return QString{};
    };
    const QPair<QString, QString> lookup{pack, identity};
    if (const auto *cached = m_keys.object(lookup))
        return select(*cached);
    const auto digest = QString::fromLatin1(
        QCryptographicHash::hash((pack + '\n' + identity).toUtf8(), QCryptographicHash::Sha256).toHex());
    Keys keys{"content/frozen/" + digest, "content/locked/" + digest, "content/updated/" + digest};
    const auto result = select(keys);
    const auto cost =
        128 + sizeof(Keys) +
        2 * (pack.size() + identity.size() + keys.frozen.size() + keys.locked.size() + keys.updated.size());
    if (cost <= size_t(m_keys.maxCost()))
        m_keys.insert(lookup, new Keys(std::move(keys)), cost);
    return result;
}
bool SettingsContentPreferences::frozen(const QString &pack, const QString &identity) const {
    return m_settings->value(key(pack, identity, Field::Frozen), false).toBool();
}
bool SettingsContentPreferences::hasFrozenPreference(const QString &pack, const QString &identity) const {
    return m_settings->contains(key(pack, identity, Field::Frozen));
}
void SettingsContentPreferences::setFrozen(const QString &pack, const QString &identity, bool value) {
    m_settings->setValue(key(pack, identity, Field::Frozen), value);
}
bool SettingsContentPreferences::locked(const QString &pack, const QString &identity) const {
    return m_settings->value(key(pack, identity, Field::Locked), false).toBool();
}
void SettingsContentPreferences::setLocked(const QString &pack, const QString &identity, bool value) {
    m_settings->setValue(key(pack, identity, Field::Locked), value);
}
qint64 SettingsContentPreferences::updatedAt(const QString &pack, const QString &identity) const {
    return m_settings->value(key(pack, identity, Field::Updated), 0).toLongLong();
}
void SettingsContentPreferences::setUpdatedAt(const QString &pack, const QString &identity, qint64 value) {
    m_settings->setValue(key(pack, identity, Field::Updated), value);
}
} // namespace studio
