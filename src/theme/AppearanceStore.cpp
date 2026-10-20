#include "AppearanceStore.h"
#include <QDir>
namespace studio {
NativeAppearanceStore::NativeAppearanceStore(QString root) {
    if (root.isEmpty())
        root = qEnvironmentVariable("CONFLUX_STYLE_SETTINGS_DIR");
    if (!root.isEmpty())
        QDir().mkpath(root);
    m_settings = root.isEmpty() ? std::make_unique<QSettings>("ModpackStudio", "ModpackStudio")
                                : std::make_unique<QSettings>(root + "/appearance.ini", QSettings::IniFormat);
}
QVariantMap NativeAppearanceStore::load() const {
    return m_settings->value("appearance/layout").toMap();
}
void NativeAppearanceStore::save(const QVariantMap &values) {
    m_settings->setValue("appearance/layout", values);
}
} // namespace studio
