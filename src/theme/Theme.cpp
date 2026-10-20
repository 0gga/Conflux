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
static const QString builtInOled = "Built-in OLED";
static const QString builtInGrey = "Built-in dark grey";
static QString themeResource(const QString &filename) {
    return ":/qt/qml/Conflux/Style/themes/" + filename;
}
Theme::Theme(QObject *parent, QString storageOverride)
    : QObject(parent), m_storageOverride(std::move(storageOverride)),
      m_settings(
          m_storageOverride.isEmpty()
              ? std::make_unique<QSettings>("ModpackStudio", "ModpackStudio")
              : std::make_unique<QSettings>(m_storageOverride + "/appearance.ini", QSettings::IniFormat)) {
    QDir().mkpath(folder());
    for (const auto &filename : {QString("oled.json"), QString("dark-grey.json")}) {
        const auto example = QDir(folder()).filePath(filename);
        if (!QFile::exists(example) && QFile::copy(themeResource(filename), example))
            QFile::setPermissions(example, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                               QFileDevice::ReadUser | QFileDevice::WriteUser);
    }
    const auto saved = m_settings->value("appearance/theme").toString();
    const bool migrateDefault = !m_settings->value("appearance/oledDefaultApplied", false).toBool();
    select(builtInOled);
    if (!saved.isEmpty() && !(migrateDefault && saved == builtInGrey))
        select(saved);
    m_initializing = false;
    if (migrateDefault) {
        m_settings->setValue("appearance/oledDefaultApplied", true);
        if (saved == builtInGrey)
            m_settings->setValue("appearance/theme", builtInOled);
    }
}
QString Theme::folder() const {
    return QDir(m_storageOverride.isEmpty()
                    ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                    : m_storageOverride)
        .filePath("themes");
}
QStringList Theme::available() const {
    return QStringList{builtInOled, builtInGrey} +
           QDir(folder()).entryList({"*.json"}, QDir::Files, QDir::Name);
}
bool Theme::select(const QString &file) {
    const bool built = file == builtInOled || file == builtInGrey;
    QFile input(built ? themeResource(file == builtInOled ? "oled.json" : "dark-grey.json")
                      : QDir(folder()).filePath(file));
    if ((!built && (QFileInfo(file).fileName() != file || !file.endsWith(".json"))) ||
        !input.open(QIODevice::ReadOnly) || input.size() > 65536) {
        m_error = "Could not open theme. The current theme was kept.";
        emit changed();
        return false;
    }
    const auto document = QJsonDocument::fromJson(input.readAll()).object();
    QVariantMap colors;
    const QMap<QString, QStringList> groups{
        {"surfaces", {"background", "panel", "card", "input", "hover", "selection", "border"}},
        {"text", {"text", "muted"}},
        {"actions", {"accent", "accentText"}},
        {"status", {"warning", "warningSurface", "danger"}}};
    bool valid = document.value("schemaVersion").toInt() == 1 &&
                 !document.value("name").toString().trimmed().isEmpty();
    for (auto group = groups.begin(); group != groups.end(); ++group) {
        const auto values = document.value(group.key()).toObject();
        for (const auto &key : group.value()) {
            const auto value = values.value(key).toString();
            valid = valid && QColor::isValidColorName(value);
            colors.insert(key, value);
        }
    }
    if (!valid) {
        m_error = "Invalid theme: use schemaVersion 1 and valid colors for every token. The current theme "
                  "was kept.";
        emit changed();
        return false;
    }
    m_colors = colors;
    m_name = document.value("name").toString();
    m_file = file;
    m_error.clear();
    if (!m_initializing)
        m_settings->setValue("appearance/theme", file);
    emit changed();
    return true;
}
void Theme::reload() {
    select(m_file);
}
void Theme::openFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder()));
}
} // namespace studio
