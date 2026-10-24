#include "ModMetadataReader.h"
#include "domain/LicensePolicy.h"
#include "toml.hpp"
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
    QMap<QString, bool> dependencies;
    QStringList modIds, provides;
    const auto addDependency = [&](const QString &id, bool required) {
        if (!id.isEmpty())
            dependencies[id] = dependencies.value(id) || required;
    };
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
    if (!fabric.isEmpty() || !quilt.isEmpty()) {
        const auto metadata = fabric.isEmpty() ? quilt.value("metadata").toObject() : fabric;
        QStringList authors;
        for (const auto &author : metadata.value("authors").toArray())
            authors << (author.isString() ? author.toString() : author.toObject().value("name").toString());
        if (!authors.isEmpty())
            result["author"] = authors.join(", ");
        const auto environment = fabric.value("environment").toString("*");
        result["environment"] = environment == "*" ? "both" : environment;
        const auto license = metadata.value("license");
        result["openSource"] = license.isString() ? isKnownOpenSourceLicense(license.toString()) : false;
        if (license.isArray())
            for (const auto &value : license.toArray())
                if (isKnownOpenSourceLicense(value.toString()))
                    result["openSource"] = true;
    }
    if (!fabric.isEmpty())
        loaders << "fabric";
    for (const auto &key : {"depends", "recommends", "suggests"}) {
        const auto refs = fabric.value(key).toObject();
        for (auto it = refs.constBegin(); it != refs.constEnd(); ++it)
            addDependency(it.key(), QString(key) == "depends");
    }
    for (const auto &value : fabric.value("provides").toArray())
        if (value.isString())
            provides << value.toString();
    for (const auto &value : quilt.value("depends").toArray()) {
        const auto ref = value.toObject();
        addDependency(ref.value("id").toString(), !ref.value("optional").toBool());
    }
    for (const auto &value : quilt.value("provides").toArray())
        if (value.isString())
            provides << value.toString();
        else if (value.isObject())
            provides << value.toObject().value("id").toString();
    if (!quilt.isEmpty())
        loaders << "quilt";
    for (const auto &entry : QList<QPair<QByteArray, QString>>{{"META-INF/neoforge.mods.toml", "neoforge"},
                                                               {"META-INF/mods.toml", "forge-family"}}) {
        const auto text = QString::fromUtf8(entries.value(QString::fromUtf8(entry.first)));
        QVariantMap declaration;
        QString license;
        try {
            const auto table = toml::parse(text.toStdString());
            license = QString::fromStdString(table["license"].value_or(std::string{}));
            if (const auto *mods = table["mods"].as_array(); mods && !mods->empty()) {
                if (const auto *mod = mods->get(0)->as_table())
                    for (const auto *key : {"modId", "displayName", "version", "authors"})
                        declaration[key] = QString::fromStdString((*mod)[key].value_or(std::string{}));
                for (const auto &value : *mods)
                    if (const auto *mod = value.as_table()) {
                        const auto id = QString::fromStdString((*mod)["modId"].value_or(std::string{}));
                        if (!id.isEmpty())
                            modIds << id;
                        if (const auto *refs = table["dependencies"][id.toStdString()].as_array())
                            for (const auto &value : *refs)
                                if (const auto *ref = value.as_table()) {
                                    const auto kind =
                                        QString::fromStdString((*ref)["type"].value_or(std::string{}));
                                    if (!kind.isEmpty() && kind != "required" && kind != "optional")
                                        continue;
                                    addDependency(
                                        QString::fromStdString((*ref)["modId"].value_or(std::string{})),
                                        kind == "required" ||
                                            (kind.isEmpty() && (*ref)["mandatory"].value_or(true)));
                                }
                    }
            }
        } catch (const toml::parse_error &) {
            // Malformed metadata stays unidentified; dependency IDs are never owners.
        }
        const auto value = [&](const QString &key) { return declaration.value(key).toString(); };
        if (!value("modId").isEmpty()) {
            if (result.value("author").toString().isEmpty())
                result["author"] = value("authors");
            if (!result.contains("openSource"))
                result["openSource"] = isKnownOpenSourceLicense(license);
            loaders << entry.second;
            if (result.value("modId").toString().isEmpty()) {
                result["modId"] = value("modId");
                result["title"] = value("displayName");
                result["version"] = value("version");
            }
        }
    }
    result["loaders"] = loaders;
    modIds << result.value("modId").toString();
    modIds.removeAll("");
    modIds.removeDuplicates();
    provides.removeAll("");
    provides.removeDuplicates();
    result["modIds"] = modIds;
    result["provides"] = provides;
    QVariantList references;
    for (auto it = dependencies.cbegin(); it != dependencies.cend(); ++it)
        references << QVariantMap{{"id", it.key()}, {"required", it.value()}};
    result["runtimeDependencies"] = references;
    const auto runtimeVersion = result.value("version").toString();
    if (!runtimeVersion.startsWith("${"))
        result["runtimeVersion"] = runtimeVersion;
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
