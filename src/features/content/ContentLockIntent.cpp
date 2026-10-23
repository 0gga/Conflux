#include "ContentLockIntent.h"
namespace studio {
ContentLockIntent planContentLockIntent(const QVariantMap &pack, const QVariantList &rows, bool locked,
                                        bool sourceOnly) {
    ContentLockIntent intent;
    if (rows.isEmpty())
        return intent;
    for (const auto &value : rows)
        if (value.toMap().value("pending").toBool()) {
            intent.route = ContentLockIntent::Reject;
            intent.error = "This content already has an active or queued operation. Wait for it to finish.";
            return intent;
        }
    if (pack.value("provider") == "modrinth" || pack.value("provider") == "curseforge") {
        intent.route = ContentLockIntent::Source;
        auto &operation = intent.operation;
        operation.kind = ContentOperation::Lock;
        operation.packPath = pack.value("path").toString();
        operation.locked = locked;
        if (sourceOnly)
            operation.sourceLock = rows.first().toMap();
        else
            for (const auto &value : rows)
                operation.targets << contentRowKey(value.toMap());
        return intent;
    }
    for (const auto &value : rows)
        if (value.toMap().value("launcherLocked").toBool()) {
            intent.route = ContentLockIntent::Reject;
            intent.error = "The source launcher for this lock is unavailable.";
            return intent;
        }
    intent.route = ContentLockIntent::Local;
    return intent;
}
} // namespace studio
