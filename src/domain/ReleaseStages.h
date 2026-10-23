#pragma once
#include <QStringList>
namespace studio {
// Provider publication status and Minecraft snapshots are not content release stages.
inline QStringList releaseStages() {
    return {"release", "beta", "alpha"};
}
inline QStringList normalizedReleaseStages(const QStringList &input) {
    QStringList result;
    for (const auto &stage : releaseStages())
        for (const auto &value : input)
            if (value.trimmed().toLower() == stage && !result.contains(stage))
                result << stage;
    return result;
}
// Missing and legacy empty selections default to Release for every install/update path.
inline QStringList effectiveReleaseStages(const QStringList &selection = {}) {
    return selection.isEmpty() ? QStringList{"release"} : normalizedReleaseStages(selection);
}
inline bool releaseStageAllowed(const QString &stage, const QStringList &selection) {
    // Provider stages are canonical; a user selection may still need normalization.
    if (stage != QLatin1StringView("release") && stage != QLatin1StringView("beta") &&
        stage != QLatin1StringView("alpha"))
        return false;
    if (selection.isEmpty())
        return stage == QLatin1StringView("release");
    if (selection.contains(stage))
        return true;
    for (const auto &value : selection)
        if (value.trimmed().toLower() == stage)
            return true;
    return false;
}
} // namespace studio
