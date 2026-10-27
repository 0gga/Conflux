#include "DiagnosticRule.h"
#include "domain/ContentTypes.h"
#include "features/content/ContentDependencyGraph.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
namespace studio {
static QVariantMap issue(const QString &category, const QString &message, const QString &path = {}) {
    return {{"category", category}, {"message", message}, {"relativePath", path}};
}
static QVariantList fixes(const ContentSnapshot &snapshot, const QVariantMap &row, const QStringList &kinds) {
    static const QRegularExpression hashPattern("^[a-fA-F0-9]{40}$");
    if (row.value("relativePath").toString().isEmpty() || row.value("directory").toBool() ||
        !hashPattern.match(row.value("sha1").toString()).hasMatch())
        return {};
    QVariantList result;
    for (const auto &kind : kinds)
        result << QVariantMap{{"kind", kind},
                              {"label", kind == "enable"    ? "Enable"
                                        : kind == "disable" ? "Disable"
                                                            : "Remove"},
                              {"pack", QVariantMap{{"path", snapshot.pack.value("path")},
                                                   {"minecraft", snapshot.pack.value("minecraft")},
                                                   {"loader", snapshot.pack.value("loader")}}},
                              {"relativePath", row.value("relativePath")},
                              {"sha1", row.value("sha1")}};
    return result;
}
class DisabledRequiredContentRule final : public IDiagnosticRule {
    QVariantList evaluate(const ContentSnapshot &snapshot) const override {
        QVariantList result;
        for (const auto &value : ContentDependencyGraph(snapshot.files).disabledRequirements()) {
            const auto relation = value.toMap();
            const auto row = relation.value("dependency").toMap();
            auto entry =
                issue("missing",
                      row.value("title", row.value("fileName")).toString() + " is disabled. Required by " +
                          relation.value("requiredBy").toStringList().join(", ") + ".",
                      row.value("relativePath").toString());
            entry["code"] = "disabled-required-content";
            entry["fixes"] = fixes(snapshot, row, {"enable"});
            result << entry;
        }
        return result;
    }
};
class DuplicateModRule final : public IDiagnosticRule {
    QVariantList evaluate(const ContentSnapshot &snapshot) const override {
        QVariantList result;
        QMap<QString, QStringList> paths;
        for (const auto &item : snapshot.files) {
            const auto row = item.toMap();
            const auto id = row.value("modId").toString();
            if (row.value("category") != "mods" || !row.value("enabled").toBool() || id.isEmpty())
                continue;
            paths[id] << row.value("relativePath").toString();
        }
        for (auto it = paths.cbegin(); it != paths.cend(); ++it)
            if (it.value().size() > 1) {
                auto entry = issue(
                    "duplicate",
                    "Multiple enabled files declare mod ID " + it.key() + ":\n" + it.value().join("\n") +
                        "\nKeep the intended version enabled and disable or remove the other file.",
                    it.value().first());
                entry["paths"] = it.value();
                result << entry;
            }
        return result;
    }
};
class LoaderCompatibilityRule final : public IDiagnosticRule {
    QVariantList evaluate(const ContentSnapshot &snapshot) const override {
        QVariantList result;
        const auto loader = snapshot.pack.value("loader").toString().toLower();
        if (!QStringList{"fabric", "quilt", "forge", "neoforge", "vanilla"}.contains(loader))
            return result;
        bool bridge = false;
        int bridged = 0;
        for (const auto &item : snapshot.files) {
            const auto row = item.toMap();
            if (row.value("enabled").toBool() &&
                (row.value("connectorBridge").toBool() || row.value("modId") == "connector"))
                bridge = true;
        }
        for (const auto &item : snapshot.files) {
            const auto row = item.toMap();
            const auto required = row.value("loaders").toStringList();
            if (!row.value("enabled").toBool() || required.isEmpty())
                continue;
            if (required.contains(loader) || (loader == "quilt" && required.contains("fabric")) ||
                (required.contains("forge-family") && (loader == "forge" || loader == "neoforge")))
                continue;
            if (bridge && (loader == "forge" || loader == "neoforge") && required.contains("fabric")) {
                ++bridged;
                continue;
            }
            auto entry = issue("loader",
                               row.value("fileName").toString() + " declares " + required.join(" / ") +
                                   " metadata; this pack uses " + contentLabel(loader) + ".",
                               row.value("relativePath").toString());
            entry["code"] = "incompatible-loader";
            entry["fixes"] = fixes(snapshot, row, {"disable", "remove"});
            result << entry;
        }
        if (bridged)
            result.append(issue("bridge", QString::number(bridged) +
                                              " Fabric mods use Connector. The bridge is present; individual "
                                              "runtime compatibility is not verified."));
        return result;
    }
};
class ManagedPinRule final : public IDiagnosticRule {
    QVariantList evaluate(const ContentSnapshot &snapshot) const override {
        QVariantList result;
        QSet<QString> projects;
        const auto mods = snapshot.pack.value("mods").toList();
        for (const auto &item : mods)
            projects.insert(item.toMap().value("projectId").toString());
        for (const auto &item : mods) {
            const auto pin = item.toMap();
            const auto path = pin.value("installPath").toString();
            const auto title = pin.value("projectTitle", pin.value("projectId")).toString();
            if (path.isEmpty() ||
                !QFileInfo::exists(QDir(snapshot.pack.value("path").toString()).filePath(path)))
                result.append(issue("missing", title + ": pinned mod file is missing.", path));
            const auto games = pin.value("gameVersions").toStringList(),
                       loaders = pin.value("loaders").toStringList();
            if ((!games.isEmpty() && !games.contains(snapshot.pack.value("minecraft").toString())) ||
                (!loaders.isEmpty() && !loaders.contains(snapshot.pack.value("loader").toString())))
                result.append(
                    issue("loader", title + " is pinned for a different Minecraft version or loader.", path));
            for (const auto &dep : pin.value("dependencyRefs").toList()) {
                const auto dependency = dep.toMap();
                const auto id = dependency.value("project_id").toString();
                if (dependency.value("dependency_type") == "required" && !id.isEmpty() &&
                    !projects.contains(id))
                    result.append(issue("missing",
                                        title + ": required pinned dependency " + id + " is missing.", path));
            }
        }
        return result;
    }
};
class OrphanConfigRule final : public IDiagnosticRule {
    QVariantList evaluate(const ContentSnapshot &snapshot) const override {
        QStringList candidates;
        if (!snapshot.identifiedMods)
            return {};
        for (const auto &item : snapshot.files) {
            const auto row = item.toMap();
            if (row.value("category") != "configs" || !row.value("relatedModId").toString().isEmpty())
                continue;
            if (QStringList{"json", "json5", "toml", "yaml", "yml", "properties", "cfg", "conf", "ini"}
                    .contains(QFileInfo(row.value("fileName").toString()).suffix().toLower()))
                candidates << row.value("relativePath").toString();
        }
        return candidates.isEmpty()
                   ? QVariantList{}
                   : QVariantList{
                         issue("orphan",
                               QString::number(candidates.size()) +
                                   " config files have no identified owner and may belong to removed mods: " +
                                   candidates.mid(0, 4).join(", ") +
                                   ". Review before deleting; ownership is uncertain.")};
    }
};
class ScanCoverageRule final : public IDiagnosticRule {
    QVariantList evaluate(const ContentSnapshot &snapshot) const override {
        QVariantList result;
        if (snapshot.unavailable)
            result.append(issue("scan", "The modpack folder is no longer available."));
        if (snapshot.limited)
            result.append(issue(
                "scan",
                "Content scan reached its 5,000-file or directory depth limit; some files were omitted."));
        return result;
    }
};
std::vector<std::unique_ptr<IDiagnosticRule>> defaultDiagnosticRules() {
    std::vector<std::unique_ptr<IDiagnosticRule>> result;
    result.emplace_back(std::make_unique<DuplicateModRule>());
    result.emplace_back(std::make_unique<LoaderCompatibilityRule>());
    result.emplace_back(std::make_unique<DisabledRequiredContentRule>());
    result.emplace_back(std::make_unique<ManagedPinRule>());
    result.emplace_back(std::make_unique<OrphanConfigRule>());
    result.emplace_back(std::make_unique<ScanCoverageRule>());
    return result;
}
} // namespace studio
