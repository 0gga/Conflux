#include "InstalledModIdentity.h"
#include "ContentFileCache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
namespace studio {
InstalledModIdentity NativeInstalledModIdentity::inspect(const QVariantMap &pack,
                                                         const QVariantMap &row) const {
    InstalledModIdentity identity;
    const auto relative = row.value("relativePath").toString();
    if (row.value("category") != "mods" || !relative.startsWith("mods/") ||
        !(relative.endsWith(".jar", Qt::CaseInsensitive) ||
          relative.endsWith(".jar.disabled", Qt::CaseInsensitive))) {
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
    return inspectVerified(pack, row, identity.sha1);
}
InstalledModIdentity NativeInstalledModIdentity::inspectVerified(const QVariantMap &pack,
                                                                 const QVariantMap &row,
                                                                 const QString &verifiedHash) const {
    InstalledModIdentity identity;
    identity.sha1 = verifiedHash;
    const auto root = pack.value("path").toString(), relative = row.value("relativePath").toString();
    {
        QMutexLocker guard(&m_mutex);
        const auto pins = pack.value("mods").toList();
        if (pins != m_pins) {
            m_pins = pins;
            m_pinIds.clear();
            for (const auto &value : pins) {
                const auto pin = value.toMap();
                QMap<QString, QString> ids;
                const auto provider = pin.value("provider").toString();
                if (provider == "modrinth" || provider == "curseforge")
                    ids[provider] = pin.value("projectId").toString();
                const auto sources = pin.value("projectSourceIds").toMap();
                for (auto it = sources.begin(); it != sources.end(); ++it)
                    if (it.key() == "modrinth" || it.key() == "curseforge")
                        ids[it.key()] = it.value().toString();
                m_pinIds[pin.value("installPath").toString() + '\n' +
                         pin.value("sha1").toString().toLower()] = ids;
            }
        }
        identity.sourceIds = m_pinIds.value(relative + '\n' + identity.sha1.toLower());
    }
    if (pack.value("provider") == "curseforge") {
        const auto index = ContentFileCache::instance().metadata(
            QDir(root).filePath("minecraftinstance.json"),
            "curseforge:" + QString::number(quintptr(m_files.get())), [&] {
                QVariantMap entries;
                const auto metadata = m_files->read(root, "minecraftinstance.json", 16 * 1024 * 1024);
                if (metadata.truncated || !metadata.error.isEmpty())
                    return entries;
                for (const auto &value :
                     QJsonDocument::fromJson(metadata.bytes).object().value("installedAddons").toArray()) {
                    const auto addon = value.toObject(), installed = addon.value("installedFile").toObject();
                    const auto id = addon.value("addonID").toInteger();
                    if (id <= 0)
                        continue;
                    const auto name =
                        addon.value("fileNameOnDisk").toString(installed.value("fileName").toString());
                    for (const auto &hash : installed.value("hashes").toArray()) {
                        const auto h = hash.toObject();
                        if (h.value("type").toInt() == 1)
                            entries[name + '\n' + h.value("value").toString().toLower()] = QVariantMap{
                                {"id", QString::number(id)}, {"url", addon.value("webSiteURL").toString()}};
                    }
                }
                return entries;
            });
        auto name = relative.mid(5);
        if (name.endsWith(".disabled", Qt::CaseInsensitive))
            name.chop(9);
        const auto match = index.value(name + '\n' + identity.sha1.toLower()).toMap();
        if (!match.isEmpty()) {
            identity.sourceIds["curseforge"] = match.value("id").toString();
            const auto url = match.value("url").toString();
            if (url.startsWith("https://www.curseforge.com/") || url.startsWith("https://curseforge.com/"))
                identity.projectUrl = url;
        }
    }
    return identity;
}
} // namespace studio
