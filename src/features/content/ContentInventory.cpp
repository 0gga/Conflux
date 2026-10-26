#include "ContentInventory.h"
#include "ConfigAssociation.h"
#include "platform/ContentFileCache.h"
#include "platform/PackFileStore.h"
#include <QDir>
#include <QFile>
#include <functional>
namespace studio {
ContentSnapshot ContentInventory::scan(const QVariantMap &pack) const {
    return collect(pack, true);
}
ContentSnapshot ContentInventory::collect(const QVariantMap &pack, bool includeConfigs) const {
    ContentSnapshot result;
    result.pack = pack;
    const auto canonicalRoot = NativePackFileStore::resolve(pack.value("path").toString(), {});
    if (canonicalRoot.isEmpty()) {
        result.unavailable = true;
        return result;
    }
    const QDir root(canonicalRoot);
    QMap<QString, QVariantMap> pins;
    for (const auto &item : pack.value("mods").toList()) {
        const auto pin = item.toMap();
        pins[pin.value("installPath").toString()] = pin;
    }
    QVariantList mods;
    auto append = [&](const QFileInfo &file, const QString &category) {
        const auto relative = root.relativeFilePath(file.absoluteFilePath());
        // Callers enumerate only a freshly resolved, contained parent. Do not resolve
        // every config's entire ancestor chain again; still reject linked/private leaves.
        if (file.isSymLink() || file.isJunction() ||
            !NativePackFileStore::isAllowedComponent(file.fileName()))
            return;
        QVariantMap row{{"title", file.completeBaseName()},
                        {"fileName", file.fileName()},
                        {"relativePath", relative},
                        {"category", category},
                        {"version", ""},
                        {"status", category == "configs" ? "Local config" : "Installed"},
                        {"icon", ""},
                        {"provider", ""},
                        {"directory", file.isDir()}};
        if (category == "mods") {
            const auto meta = ContentFileCache::instance().metadata(
                file.absoluteFilePath(), "mod:" + QString::number(quintptr(m_mods.get())),
                [&] { return m_mods->read(file.absoluteFilePath()); });
            for (auto it = meta.begin(); it != meta.end(); ++it)
                if (!it.value().toString().isEmpty() || it.value().metaType().id() != QMetaType::QString)
                    row[it.key()] = it.value();
            const bool enabled = !file.fileName().endsWith(".disabled", Qt::CaseInsensitive);
            row["enabled"] = enabled;
            if (!enabled)
                row["status"] = "Disabled";
            if (enabled) {
                ++result.enabledMods;
                if (!row.value("modId").toString().isEmpty())
                    ++result.identifiedMods;
            }
            const auto pin = pins.value(relative);
            if (!pin.isEmpty()) {
                row["title"] = pin.value("projectTitle", row.value("title"));
                row["version"] = pin.value("version");
                row["provider"] = pin.value("provider");
                row["projectId"] = pin.value("projectId");
            }
            if (row.value("version").toString().startsWith("${"))
                row["version"] = "";
            mods.append(row);
        } else if (category == "shaderpacks" || category == "resourcepacks" || category == "datapacks") {
            const auto iconMetadata =
                ContentFileCache::instance().metadata(file.absoluteFilePath(), "pack-icon", [&] {
                    QByteArray icon;
                    if (file.isDir()) {
                        QFile input(NativePackFileStore::resolve(root.path(), relative + "/pack.png"));
                        if (!input.fileName().isEmpty() && input.open(QIODevice::ReadOnly) &&
                            input.size() <= 1024 * 1024)
                            icon = input.readAll();
                    } else
                        icon =
                            m_archives->readEntries(file.absoluteFilePath(), {"pack.png"}).value("pack.png");
                    return QVariantMap{{"icon", rasterIcon(icon)}};
                });
            row["icon"] = iconMetadata.value("icon");
        }
        result.files.append(row);
    };
    for (const auto &category : QStringList{"mods", "shaderpacks", "resourcepacks", "datapacks"}) {
        const auto path = NativePackFileStore::resolve(root.path(), category);
        if (path.isEmpty())
            continue;
        const auto filters = QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot |
                             (category == "mods" ? QDir::Filters{} : QDir::Dirs);
        for (const auto &file : QDir(path).entryInfoList(filters, QDir::Name)) {
            if (category == "mods" && !file.fileName().endsWith(".jar", Qt::CaseInsensitive) &&
                !file.fileName().endsWith(".jar.disabled", Qt::CaseInsensitive))
                continue;
            if (result.files.size() >= 5000) {
                result.limited = true;
                break;
            }
            append(file, category);
        }
    }
    if (!includeConfigs)
        return result;
    const ConfigAssociation associations(mods);
    std::function<void(const QString &, int)> configs = [&](const QString &relative, int depth) {
        if (depth > 5) {
            result.limited = true;
            return;
        }
        const auto path = NativePackFileStore::resolve(root.path(), relative);
        if (path.isEmpty())
            return;
        for (const auto &file : QDir(path).entryInfoList(
                 QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
            if (result.files.size() >= 5000) {
                result.limited = true;
                break;
            }
            if (file.isDir()) {
                configs(root.relativeFilePath(file.absoluteFilePath()), depth + 1);
                continue;
            }
            const auto count = result.files.size();
            append(file, "configs");
            if (count == result.files.size())
                continue;
            auto row = result.files.last().toMap();
            const auto owner = associations.identify(row.value("relativePath").toString());
            for (auto it = owner.begin(); it != owner.end(); ++it)
                row[it.key()] = it.value();
            row["editable"] = !owner.isEmpty() &&
                              QStringList{"json", "toml", "properties"}.contains(file.suffix().toLower());
            result.files.last() = row;
        }
    };
    configs("config", 0);
    configs("defaultconfigs", 0);
    return result;
}
} // namespace studio
