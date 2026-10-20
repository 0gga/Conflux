#include "InstalledModIdentity.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
namespace studio {
InstalledModIdentity NativeInstalledModIdentity::inspect(const QVariantMap &pack,
                                                         const QVariantMap &row) const {
    InstalledModIdentity identity;
    const auto relative = row.value("relativePath").toString();
    if (row.value("category") != "mods" || !relative.startsWith("mods/") ||
        !(relative.endsWith(".jar") || relative.endsWith(".jar.disabled"))) {
        identity.error = "This content is not an installed mod JAR.";
        return identity;
    }
    const auto root = pack.value("path").toString();
    const auto file = m_files->read(root, relative, 128 * 1024 * 1024);
    if (!file.error.isEmpty() || file.truncated || file.bytes.isEmpty()) {
        identity.error = file.error.isEmpty()
                             ? "This file is empty or exceeds the 128 MiB identification limit."
                             : file.error;
        return identity;
    }
    identity.sha1 =
        QString::fromLatin1(QCryptographicHash::hash(file.bytes, QCryptographicHash::Sha1).toHex());
    for (const auto &value : pack.value("mods").toList()) {
        const auto pin = value.toMap();
        if (pin.value("installPath") != relative ||
            pin.value("sha1").toString().compare(identity.sha1, Qt::CaseInsensitive) != 0)
            continue;
        const auto provider = pin.value("provider").toString();
        if (provider == "modrinth" || provider == "curseforge")
            identity.sourceIds[provider] = pin.value("projectId").toString();
        const auto ids = pin.value("projectSourceIds").toMap();
        for (auto it = ids.begin(); it != ids.end(); ++it)
            if (it.key() == "modrinth" || it.key() == "curseforge")
                identity.sourceIds[it.key()] = it.value().toString();
    }
    if (pack.value("provider") == "curseforge") {
        const auto metadata = m_files->read(root, "minecraftinstance.json", 16 * 1024 * 1024);
        if (!metadata.truncated && metadata.error.isEmpty()) {
            for (const auto &value :
                 QJsonDocument::fromJson(metadata.bytes).object().value("installedAddons").toArray()) {
                const auto addon = value.toObject(), installed = addon.value("installedFile").toObject();
                auto name = relative.mid(5);
                if (name.endsWith(".disabled"))
                    name.chop(9);
                if (addon.value("fileNameOnDisk").toString(installed.value("fileName").toString()) != name)
                    continue;
                for (const auto &hash : installed.value("hashes").toArray()) {
                    const auto h = hash.toObject();
                    if (h.value("type").toInt() == 1 &&
                        h.value("value").toString().compare(identity.sha1, Qt::CaseInsensitive) == 0) {
                        const auto id = addon.value("addonID").toInteger();
                        if (id > 0) {
                            identity.sourceIds["curseforge"] = QString::number(id);
                            const auto url = addon.value("webSiteURL").toString();
                            if (url.startsWith("https://www.curseforge.com/") ||
                                url.startsWith("https://curseforge.com/"))
                                identity.projectUrl = url;
                        }
                    }
                }
            }
        }
    }
    return identity;
}
} // namespace studio
