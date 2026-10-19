#include "Theme.h"
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace studio {
static const QString builtIn = ":/qt/qml/Conflux/Style/themes/dark-grey.json";
Theme::Theme(QObject* parent, QString storageOverride) : QObject(parent), m_storageOverride(std::move(storageOverride)),
    m_settings(m_storageOverride.isEmpty() ? std::make_unique<QSettings>("ModpackStudio", "ModpackStudio") : std::make_unique<QSettings>(m_storageOverride + "/appearance.ini", QSettings::IniFormat)) {
    QDir().mkpath(folder());
    const auto example = QDir(folder()).filePath("dark-grey.json");
    if (!QFile::exists(example) && QFile::copy(builtIn, example))
        QFile::setPermissions(example, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser);
    const auto saved = m_settings->value("appearance/theme").toString();
    select("Built-in dark grey");
    if (!saved.isEmpty()) select(saved);
    m_initializing = false;
}
QString Theme::folder() const { return QDir(m_storageOverride.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : m_storageOverride).filePath("themes"); }
QStringList Theme::available() const { return QStringList{"Built-in dark grey"} + QDir(folder()).entryList({"*.json"}, QDir::Files, QDir::Name); }
bool Theme::select(const QString& file) {
    const bool built = file == "Built-in dark grey";
    QFile input(built ? builtIn : QDir(folder()).filePath(file));
    if ((!built && (QFileInfo(file).fileName() != file || !file.endsWith(".json"))) || !input.open(QIODevice::ReadOnly) || input.size() > 65536) {
        m_error = "Could not open theme. The current theme was kept."; emit changed(); return false;
    }
    const auto document = QJsonDocument::fromJson(input.readAll()).object();
    QVariantMap colors;
    const QMap<QString, QStringList> groups{
        {"surfaces", {"background", "panel", "card", "input", "hover", "selection", "border"}},
        {"text", {"text", "muted"}}, {"actions", {"accent", "accentText"}},
        {"status", {"warning", "warningSurface", "danger"}}};
    bool valid = document.value("schemaVersion").toInt() == 1 && !document.value("name").toString().trimmed().isEmpty();
    for (auto group = groups.begin(); group != groups.end(); ++group) {
        const auto values = document.value(group.key()).toObject();
        for (const auto& key : group.value()) {
            const auto value = values.value(key).toString();
            valid = valid && QColor::isValidColorName(value);
            colors.insert(key, value);
        }
    }
    if (!valid) { m_error = "Invalid theme: use schemaVersion 1 and valid colors for every token. The current theme was kept."; emit changed(); return false; }
    m_colors = colors; m_name = document.value("name").toString(); m_file = file; m_error.clear();
    if (!m_initializing) m_settings->setValue("appearance/theme", file);
    emit changed(); return true;
}
void Theme::reload() { select(m_file); }
void Theme::openFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(folder())); }
}
