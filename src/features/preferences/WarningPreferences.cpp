#include "WarningPreferences.h"
namespace studio {
SettingsWarningPreferenceStore::SettingsWarningPreferenceStore(QString path)
    : m_settings(path.isEmpty() ? std::make_unique<QSettings>("ModpackStudio", "ModpackStudio")
                                : std::make_unique<QSettings>(path, QSettings::IniFormat)) {}
bool SettingsWarningPreferenceStore::enabled(const QString &category) const {
    return m_settings->value("warnings/" + category, true).toBool();
}
void SettingsWarningPreferenceStore::setEnabled(const QString &category, bool enabled) {
    m_settings->setValue("warnings/" + category, enabled);
}
static const QList<QStringList> categories{
    {"duplicate", "Duplicate mods", "Multiple enabled files declare the same mod ID."},
    {"loader", "Loader compatibility", "A mod declares a different loader or incompatible managed pin."},
    {"missing", "Missing files and dependencies",
     "Pinned files or required managed dependencies are absent."},
    {"orphan", "Possible stale configs", "Config ownership could not be matched. These are suggestions."},
    {"bridge", "Compatibility bridges",
     "Fabric mods depend on Connector; runtime compatibility is unverified."},
    {"scan", "Scan limitations", "Unavailable folders and content scan limits."}};
WarningPreferences::WarningPreferences(std::shared_ptr<IWarningPreferenceStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)) {}
QVariantList WarningPreferences::options() const {
    QVariantList result;
    for (const auto &row : categories)
        result.append(QVariantMap{{"id", row[0]},
                                  {"title", row[1]},
                                  {"description", row[2]},
                                  {"enabled", m_store->enabled(row[0])}});
    return result;
}
QStringList WarningPreferences::enabledCategories() const {
    QStringList result;
    for (const auto &row : categories)
        if (m_store->enabled(row[0]))
            result << row[0];
    return result;
}
void WarningPreferences::setEnabled(const QString &category, bool enabled) {
    for (const auto &row : categories)
        if (row[0] == category) {
            m_store->setEnabled(category, enabled);
            emit changed();
            return;
        }
}
void WarningPreferences::reset() {
    for (const auto &row : categories)
        m_store->setEnabled(row[0], true);
    emit changed();
}
} // namespace studio
