#pragma once
#include "features/content/ContentInventory.h"
#include <memory>
#include <vector>
namespace studio {
class IDiagnosticRule {
  public:
    virtual ~IDiagnosticRule() = default;
    virtual QVariantList evaluate(const ContentSnapshot &) const = 0;
};
std::vector<std::unique_ptr<IDiagnosticRule>> defaultDiagnosticRules();
} // namespace studio
