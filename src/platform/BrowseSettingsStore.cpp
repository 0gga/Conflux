#include "BrowseSettingsStore.h"
namespace studio {
BrowseSettingsStore::BrowseSettingsStore(const QString &file)
    : m_settings(file.isEmpty() ? std::make_unique<QSettings>("ModpackStudio", "ModpackStudio")
                                : std::make_unique<QSettings>(file, QSettings::IniFormat)) {}
QVariant BrowseSettingsStore::value(const QString &key, const QVariant &fallback) const {
    return m_settings->value(key, fallback);
}
void BrowseSettingsStore::save(const QString &key, const QVariant &value) {
    m_settings->setValue(key, value);
}
} // namespace studio
