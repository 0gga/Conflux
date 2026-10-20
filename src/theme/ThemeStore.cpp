#include "ThemeStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
namespace studio {
static QString resource(const QString &name) {
    return ":/qt/qml/Conflux/Style/themes/" + name;
}
NativeThemeStore::NativeThemeStore(QString root)
    : m_root(root.isEmpty() ? qEnvironmentVariable("CONFLUX_STYLE_SETTINGS_DIR") : std::move(root)) {
    m_settings = m_root.isEmpty()
                     ? std::make_unique<QSettings>("ModpackStudio", "ModpackStudio")
                     : std::make_unique<QSettings>(m_root + "/appearance.ini", QSettings::IniFormat);
    QDir().mkpath(folder());
    for (const auto &name : {QString("oled.json"), QString("dark-grey.json")}) {
        const auto path = QDir(folder()).filePath(name);
        if (!QFile::exists(path) && QFile::copy(resource(name), path))
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                            QFileDevice::ReadUser | QFileDevice::WriteUser);
    }
    if (!m_settings->value("appearance/oledDefaultApplied", false).toBool()) {
        if (m_settings->value("appearance/theme") == builtInGrey)
            select(builtInOled);
        m_settings->setValue("appearance/oledDefaultApplied", true);
    }
}
QString NativeThemeStore::folder() const {
    return QDir(m_root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                                 : m_root)
        .filePath("themes");
}
QStringList NativeThemeStore::available() const {
    return QStringList{builtInOled, builtInGrey} +
           QDir(folder()).entryList({"*.json"}, QDir::Files, QDir::Name);
}
QString NativeThemeStore::selected() const {
    return m_settings->value("appearance/theme").toString();
}
void NativeThemeStore::select(const QString &file) {
    m_settings->setValue("appearance/theme", file);
}
ThemeRead NativeThemeStore::read(const QString &name) const {
    const bool built = name == builtInOled || name == builtInGrey;
    QFile file(built ? resource(name == builtInOled ? "oled.json" : "dark-grey.json")
                     : QDir(folder()).filePath(name));
    if ((!built && (QFileInfo(name).fileName() != name || !name.endsWith(".json"))) ||
        !file.open(QIODevice::ReadOnly) || file.size() > 65536)
        return {{}, "Could not open theme. The current theme was kept."};
    return {file.readAll(), {}};
}
ThemeWrite NativeThemeStore::save(const QString &name, const QByteArray &bytes) {
    if (name.trimmed().isEmpty() || name.size() > 80 || bytes.size() > 65536)
        return {{}, "Use a theme name between 1 and 80 characters."};
    auto slug = name.toLower().trimmed();
    slug.replace(QRegularExpression("[^a-z0-9]+"), "-");
    slug = slug.left(40);
    const auto hash = QString::fromLatin1(
        QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex().left(8));
    const auto filename = "custom-" + slug + '-' + hash + ".json";
    const auto path = QDir(folder()).filePath(filename);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size() || !file.commit())
        return {{}, "Could not save the custom theme. The current draft was kept."};
    return {filename, {}};
}
} // namespace studio
