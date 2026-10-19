#include "PackContent.h"
#include "miniz.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <functional>

namespace studio {
// Read only bounded metadata entries, never extract archive paths to disk.
static QByteArray metadata(mz_zip_archive& zip, const char* name) {
    const int index = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
    mz_zip_archive_file_stat stat{};
    if (index < 0 || !mz_zip_reader_file_stat(&zip, index, &stat) || stat.m_uncomp_size > 1024 * 1024) return {};
    QByteArray bytes(int(stat.m_uncomp_size), Qt::Uninitialized);
    return mz_zip_reader_extract_to_mem(&zip, index, bytes.data(), size_t(bytes.size()), 0) ? bytes : QByteArray{};
}
static QVariantMap modMetadata(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    mz_zip_archive zip{};
    zip.m_pRead = [](void* opaque, mz_uint64 offset, void* buffer, size_t length) -> size_t {
        auto* input = static_cast<QFile*>(opaque);
        if (!input->seek(qint64(offset))) return 0;
        const auto count = input->read(static_cast<char*>(buffer), qint64(length));
        return count < 0 ? 0 : size_t(count);
    };
    zip.m_pIO_opaque = &file;
    if (!mz_zip_reader_init(&zip, mz_uint64(file.size()), 0)) return {};
    QVariantMap result;
    const auto fabric = QJsonDocument::fromJson(metadata(zip, "fabric.mod.json")).object();
    const auto quilt = QJsonDocument::fromJson(metadata(zip, "quilt.mod.json")).object().value("quilt_loader").toObject();
    if (!fabric.isEmpty() || !quilt.isEmpty()) {
        const auto object = fabric.isEmpty() ? quilt : fabric;
        result = {{"modId", object.value("id").toString()}, {"title", fabric.isEmpty() ? quilt.value("metadata").toObject().value("name").toString() : fabric.value("name").toString()},
            {"version", object.value("version").toString()}, {"loader", fabric.isEmpty() ? "quilt" : "fabric"}};
    }
    // Universal JARs can declare more than one loader. Do not flag their Fabric
    // metadata as a mismatch when Forge-family metadata is present too.
    QStringList loaders;
    if (!fabric.isEmpty()) loaders << "fabric";
    if (!quilt.isEmpty()) loaders << "quilt";
    for (const auto& entry : QList<QPair<QByteArray, QString>>{{"META-INF/neoforge.mods.toml", "neoforge"}, {"META-INF/mods.toml", "forge-family"}}) {
        const auto text = QString::fromUtf8(metadata(zip, entry.first.constData()));
        auto value = [&](const QString& key) {
            return QRegularExpression("(?m)^\\s*" + key + "\\s*=\\s*[\"']([^\"']+)[\"']").match(text).captured(1);
        };
        if (!value("modId").isEmpty()) {
            loaders << entry.second;
            if (result.isEmpty()) result = {{"modId", value("modId")}, {"title", value("displayName")}, {"version", value("version")}};
        }
    }
    result["loaders"] = loaders;
    const auto bundled = QJsonDocument::fromJson(metadata(zip, "META-INF/jarjar/metadata.json")).object().value("jars").toArray();
    for (const auto& item : bundled) {
        const auto identifier = item.toObject().value("identifier").toObject();
        if (identifier.value("group") == "org.sinytra" && identifier.value("artifact") == "connector-mod") result["connectorBridge"] = true;
    }
    mz_zip_reader_end(&zip);
    return result;
}
static QString normalized(QString value) {
    return value.toLower().remove(QRegularExpression("[^a-z0-9]"));
}
PackContentResult PackContent::scan(const QVariantMap& pack) {
    PackContentResult result;
    const QDir root(pack.value("path").toString());
    const auto canonical = QFileInfo(root.absolutePath()).canonicalFilePath();
    if (canonical.isEmpty()) { result.diagnostics << "The modpack folder is no longer available."; return result; }
    const auto safe = [&](const QFileInfo& file) {
        return !file.isSymLink() && file.canonicalFilePath().startsWith(canonical + '/');
    };
    QMap<QString, QVariantMap> pins;
    QSet<QString> projects, names, seenIds;
    for (const auto& item : pack.value("mods").toList()) {
        auto mod = item.toMap(); pins.insert(mod.value("installPath").toString(), mod);
        projects.insert(mod.value("projectId").toString());
    }
    int mods = 0, parsed = 0;
    bool connectorBridge = false;
    QVariantList mismatches;
    const auto loader = pack.value("loader").toString().toLower();
    QStringList orphanConfigs;
    auto append = [&](const QFileInfo& file, const QString& kind) {
        if (!safe(file)) return;
        const auto relative = root.relativeFilePath(file.absoluteFilePath());
        QVariantMap row{{"fileName", file.fileName()}, {"relativePath", relative}, {"category", kind},
            {"title", file.completeBaseName()}, {"version", ""}, {"status", file.fileName().endsWith(".disabled") ? "Disabled" : "Installed"}, {"provider", ""}};
        if (kind == "mods") {
            const bool enabled = !file.fileName().endsWith(".disabled");
            const auto meta = modMetadata(file.absoluteFilePath());
            const auto pin = pins.value(relative);
            if (enabled) { ++mods; connectorBridge = connectorBridge || meta.value("connectorBridge").toBool(); }
            const auto id = meta.value("modId").toString();
            if (!id.isEmpty()) {
                if (enabled) {
                    ++parsed;
                    if (seenIds.contains(id)) result.diagnostics << "Duplicate enabled mod ID: " + id + ". Check " + file.fileName() + ".";
                    seenIds.insert(id);
                }
                names.insert(normalized(id));
                names.insert(normalized(meta.value("title").toString()));
                if (!meta.value("title").toString().isEmpty()) row["title"] = meta.value("title");
                row["version"] = meta.value("version");
                const auto required = meta.value("loaders").toStringList();
                const bool knownLoader = QStringList{"fabric", "quilt", "forge", "neoforge", "vanilla"}.contains(loader);
                const bool compatible = required.contains(loader) || (loader == "quilt" && required.contains("fabric")) || (required.contains("forge-family") && (loader == "forge" || loader == "neoforge"));
                if (enabled && knownLoader && !compatible) {
                    row["status"] = "Loader mismatch";
                    mismatches.append(QVariantMap{{"index",result.files.size()},{"file",file.fileName()},{"loaders",required}});
                }
            }
            if (!pin.isEmpty()) {
                row["title"] = pin.value("projectTitle", row.value("title"));
                row["version"] = pin.value("version"); row["provider"] = pin.value("provider"); row["projectId"] = pin.value("projectId");
                const auto games = pin.value("gameVersions").toStringList();
                const auto loaders = pin.value("loaders").toStringList();
                if ((!games.isEmpty() && !games.contains(pack.value("minecraft").toString())) || (!loaders.isEmpty() && !loaders.contains(loader))) {
                    row["status"] = "Incompatible pin"; result.diagnostics << file.fileName() + " is pinned for a different Minecraft version or loader.";
                }
            }
            if (row.value("version").toString().startsWith("${")) row["version"] = "";
        }
        if (kind == "configs") row["status"] = "Local config";
        result.files.append(row);
    };
    for (const auto& kind : QStringList{"mods", "shaderpacks", "resourcepacks"}) {
        const QDir dir(root.filePath(kind));
        if (QFileInfo(dir.absolutePath()).isSymLink() || (!QFileInfo(dir.absolutePath()).canonicalFilePath().isEmpty() && !QFileInfo(dir.absolutePath()).canonicalFilePath().startsWith(canonical + '/'))) continue;
        for (const auto& file : dir.entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot | (kind == "mods" ? QDir::Filters{} : QDir::Dirs), QDir::Name)) {
            if (kind == "mods" && !file.fileName().endsWith(".jar") && !file.fileName().endsWith(".jar.disabled")) continue;
            if (result.files.size() >= 5000) break;
            append(file, kind);
        }
    }
    connectorBridge = connectorBridge || seenIds.contains("connector");
    int bridged = 0;
    for (const auto& item : mismatches) {
        const auto mismatch = item.toMap(); const auto required = mismatch.value("loaders").toStringList();
        if (connectorBridge && (loader == "forge" || loader == "neoforge") && required.contains("fabric")) {
            auto row = result.files.at(mismatch.value("index").toInt()).toMap(); row["status"] = "Uses Connector";
            result.files[mismatch.value("index").toInt()] = row; ++bridged;
        } else result.diagnostics << mismatch.value("file").toString() + " declares " + required.join(" / ") + " metadata; this pack uses " + loader + ".";
    }
    if (bridged) result.diagnostics << QString::number(bridged) + " Fabric mods use Connector. The bridge is present; individual runtime compatibility is not verified.";
    for (const auto& pin : pins) {
        const auto path = pin.value("installPath").toString();
        if (path.isEmpty() || !QFileInfo::exists(root.filePath(path))) result.diagnostics << pin.value("projectTitle", pin.value("projectId")).toString() + ": pinned mod file is missing.";
        for (const auto& dep : pin.value("dependencyRefs").toList()) {
            const auto dependency = dep.toMap(); const auto id = dependency.value("project_id").toString();
            if (dependency.value("dependency_type") == "required" && !id.isEmpty() && !projects.contains(id)) result.diagnostics << pin.value("projectTitle").toString() + ": required pinned dependency " + id + " is missing.";
        }
    }
    // Config ownership is heuristic. List candidates for review; never delete files.
    std::function<void(const QDir&, int)> scanConfigs = [&](const QDir& dir, int depth) {
        if (depth > 5 || result.files.size() >= 5000) return;
        for (const auto& item : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
            if (!safe(item)) continue;
            if (item.isDir()) { scanConfigs(QDir(item.absoluteFilePath()), depth + 1); continue; }
            append(item, "configs");
            const auto relative = QDir(root.filePath("config")).relativeFilePath(item.absoluteFilePath());
            const auto owner = normalized(relative.section('/', 0, 0).section('.', 0, 0));
            bool matched = owner.size() < 3;
            for (const auto& name : names) if (!name.isEmpty() && (owner.startsWith(name) || name.startsWith(owner))) matched = true;
            if (!matched && parsed > 0 && QStringList{"json", "json5", "toml", "yaml", "yml", "properties", "cfg", "conf", "ini"}.contains(item.suffix().toLower())) { orphanConfigs << relative; auto row = result.files.last().toMap(); row["status"] = "Review ownership"; result.files.last() = row; }
            if (result.files.size() >= 5000) break;
        }
    };
    for (const auto& folder : QStringList{"config", "defaultconfigs"}) {
        const QFileInfo config(root.filePath(folder));
        if (config.exists() && safe(config)) scanConfigs(QDir(config.absoluteFilePath()), 0);
    }
    if (!orphanConfigs.isEmpty()) result.diagnostics << QString::number(orphanConfigs.size()) + " config files may belong to removed mods (name-based suggestion): " + orphanConfigs.mid(0, 4).join(", ") + ". Review before deleting; ownership is uncertain.";
    if (result.files.size() >= 5000) result.diagnostics << "Content scan reached its 5,000-file limit; some files were omitted.";
    result.diagnostics.removeDuplicates();
    result.coverage = QString::number(parsed) + " / " + QString::number(mods) + " enabled mod archives identified. Checks cover loader metadata, duplicate IDs and managed pins. Runtime conflicts and full loader dependency ranges require a game launch. Config ownership is a suggestion.";
    return result;
}
}
