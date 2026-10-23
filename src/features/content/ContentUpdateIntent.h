#pragma once
#include "ContentOperationState.h"
#include <optional>
namespace studio {
bool contentUpdateEligible(const QVariantMap &row);
ContentInstallRequest contentUpdateRequest(const QVariantMap &row);
// Rows already carry indexed compatibility and policy state; execution revalidates queued intent.
std::optional<ContentOperation> planContentUpdate(const QVariantMap &pack, const QVariantMap &row);
} // namespace studio
