#pragma once
#include "features/content/ContentInventory.h"
#include "features/diagnostics/DiagnosticRule.h"
namespace studio {
struct PackContentResult {
    QVariantList files, issues;
    QStringList diagnostics;
    QString coverage;
};
class IPackContentInspector {
  public:
    virtual ~IPackContentInspector() = default;
    virtual PackContentResult inspect(const QVariantMap &pack) const = 0;
};
class PackContentInspector final : public IPackContentInspector {
  public:
    PackContentInspector(std::shared_ptr<const IContentInventory> inventory,
                         std::vector<std::unique_ptr<IDiagnosticRule>> rules);
    PackContentResult inspect(const QVariantMap &) const override;

  private:
    std::shared_ptr<const IContentInventory> m_inventory;
    std::vector<std::unique_ptr<IDiagnosticRule>> m_rules;
};
std::shared_ptr<const IPackContentInspector> defaultPackInspector();
// Compatibility entry point for headless domain callers.
class PackContent {
  public:
    static PackContentResult scan(const QVariantMap &pack);
    static QVariantList contentFiles(const QVariantMap &pack);
};
} // namespace studio
