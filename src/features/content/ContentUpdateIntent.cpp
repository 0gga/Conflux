#include "ContentUpdateIntent.h"
#include "domain/ContentTypes.h"
namespace studio {
bool contentUpdateEligible(const QVariantMap &row) {
    return row.value("updateAvailable").toBool() && !row.value("locked").toBool() &&
           !row.value("frozen").toBool() && !row.value("pending").toBool();
}
ContentInstallRequest contentUpdateRequest(const QVariantMap &row) {
    return {releaseFromVariant(row.value("updateRelease").toMap()),
            row.value("title").toString(),
            QJsonObject::fromVariantMap(row.value("sourceIds").toMap()),
            categoryType(row.value("category").toString()),
            false,
            row.value("relativePath").toString()};
}
std::optional<ContentOperation> planContentUpdate(const QVariantMap &pack, const QVariantMap &row) {
    if (!contentUpdateEligible(row))
        return std::nullopt;
    ContentOperation operation;
    operation.kind = ContentOperation::Install;
    operation.packPath = pack.value("path").toString();
    operation.targets = {contentRowKey(row)};
    operation.requests = {contentUpdateRequest(row)};
    operation.predictions = {contentInstallPrediction(operation.requests.first(), pack, row)};
    return operation;
}
} // namespace studio
