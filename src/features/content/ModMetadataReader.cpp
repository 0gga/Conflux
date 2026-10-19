#include "ModMetadataReader.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
namespace studio {
QVariantMap ModMetadataReader::read(const QString &path) const {
    const auto entries =
        m_archives->readEntries(path, {"fabric.mod.json", "quilt.mod.json", "META-INF/mods.toml",
                                       "META-INF/neoforge.mods.toml", "META-INF/jarjar/metadata.json"});
    QVariantMap result;
    const auto fabric = QJsonDocument::fromJson(entries.value("fabric.mod.json")).object();
    const auto quilt =
        QJsonDocument::fromJson(entries.value("quilt.mod.json")).object().value("quilt_loader").toObject();
    if (!fabric.isEmpty() || !quilt.isEmpty()) {
        const auto object = fabric.isEmpty() ? quilt : fabric;
        result = {{"modId", object.value("id").toString()},
                  {"title", fabric.isEmpty() ? quilt.value("metadata").toObject().value("name").toString()
                                             : fabric.value("name").toString()},
                  {"version", object.value("version").toString()},
                  {"loader", fabric.isEmpty() ? "quilt" : "fabric"}};
    }
    // Universal JARs can declare more than one loader. Do not flag their Fabric
    // metadata as a mismatch when Forge-family metadata is present too.
    QStringList loaders;
    if (!fabric.isEmpty())
        loaders << "fabric";
    if (!quilt.isEmpty())
        loaders << "quilt";
    for (const auto &entry : QList<QPair<QByteArray, QString>>{{"META-INF/neoforge.mods.toml", "neoforge"},
                                                               {"META-INF/mods.toml", "forge-family"}}) {
        const auto text = QString::fromUtf8(entries.value(QString::fromUtf8(entry.first)));
        auto value = [&](const QString &key) {
            return QRegularExpression("(?m)^\\s*" + key + "\\s*=\\s*[\"']([^\"']+)[\"']")
                .match(text)
                .captured(1);
        };
        if (!value("modId").isEmpty()) {
            loaders << entry.second;
            if (result.isEmpty())
                result = {{"modId", value("modId")},
                          {"title", value("displayName")},
                          {"version", value("version")}};
        }
    }
    result["loaders"] = loaders;
    QString iconPath = fabric.value("icon").toString();
    if (fabric.value("icon").isObject()) {
        const auto sizes = fabric.value("icon").toObject();
        iconPath = sizes.value("64").toString();
        if (iconPath.isEmpty() && !sizes.isEmpty())
            iconPath = sizes.begin().value().toString();
    }
    if (iconPath.isEmpty())
        iconPath = quilt.value("metadata").toObject().value("icon").toString();
    if (iconPath.isEmpty())
        for (const auto &name : QStringList{"META-INF/neoforge.mods.toml", "META-INF/mods.toml"}) {
            const auto match = QRegularExpression("(?m)^\\s*logoFile\\s*=\\s*\"([^\"]+)\"")
                                   .match(QString::fromUtf8(entries.value(name)));
            if (match.hasMatch())
                iconPath = match.captured(1);
        }
    if (!iconPath.isEmpty())
        result["icon"] = rasterIcon(m_archives->readEntries(path, {iconPath}).value(iconPath));
    const auto bundled = QJsonDocument::fromJson(entries.value("META-INF/jarjar/metadata.json"))
                             .object()
                             .value("jars")
                             .toArray();
    for (const auto &item : bundled) {
        const auto identifier = item.toObject().value("identifier").toObject();
        if (identifier.value("group") == "org.sinytra" && identifier.value("artifact") == "connector-mod")
            result["connectorBridge"] = true;
    }
    return result;
}
} // namespace studio
