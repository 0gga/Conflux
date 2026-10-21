#include "PackContent.h"
#include "platform/ContentAccess.h"
#include <stdexcept>
namespace studio {
PackContentInspector::PackContentInspector(std::shared_ptr<const IContentInventory> inventory,
                                           std::vector<std::unique_ptr<IDiagnosticRule>> rules)
    : m_inventory(std::move(inventory)), m_rules(std::move(rules)) {}
PackContentResult PackContentInspector::inspect(const QVariantMap &pack) const {
    ContentAccess access(pack.value("path").toString());
    if (!access.ok())
        throw std::runtime_error(access.error().toStdString());
    const auto snapshot = m_inventory->scan(pack);
    PackContentResult result;
    result.files = snapshot.files;
    for (const auto &rule : m_rules)
        result.issues.append(rule->evaluate(snapshot));
    for (const auto &issue : result.issues)
        result.diagnostics << issue.toMap().value("message").toString();
    for (auto &item : result.files) {
        auto row = item.toMap();
        for (const auto &entry : result.issues) {
            const auto issue = entry.toMap();
            if (issue.value("relativePath") == row.value("relativePath"))
                row["status"] = issue.value("category") == "loader"      ? "Compatibility review"
                                : issue.value("category") == "duplicate" ? "Duplicate ID"
                                                                         : "Review";
        }
        if (row.value("category") == "mods" && row.value("enabled").toBool() &&
            row.value("loaders").toStringList().contains("fabric") &&
            (pack.value("loader") == "forge" || pack.value("loader") == "neoforge"))
            for (const auto &issue : result.issues)
                if (issue.toMap().value("category") == "bridge")
                    row["status"] = "Uses Connector";
        item = row;
    }
    result.coverage = QString::number(snapshot.identifiedMods) + " / " +
                      QString::number(snapshot.enabledMods) +
                      " enabled mod archives identified. Runtime conflicts and full loader dependency ranges "
                      "require a game launch. Config ownership is a suggestion.";
    return result;
}
std::shared_ptr<const IPackContentInspector> defaultPackInspector() {
    auto archives = std::make_shared<ZipArchiveReader>();
    auto mods = std::make_shared<ModMetadataReader>(archives);
    return std::make_shared<PackContentInspector>(std::make_shared<ContentInventory>(mods, archives),
                                                  defaultDiagnosticRules());
}
PackContentResult PackContent::scan(const QVariantMap &pack) {
    return defaultPackInspector()->inspect(pack);
}
} // namespace studio
