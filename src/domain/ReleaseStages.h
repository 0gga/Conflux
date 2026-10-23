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
// An empty selection preserves the automatic policy rather than enabling all stages.
inline QStringList updateReleaseStages(const QString &installed, const QStringList &selection = {}) {
    if (!selection.isEmpty())
        return normalizedReleaseStages(selection);
    if (installed == "alpha")
        return releaseStages();
    if (installed == "beta")
        return {"release", "beta"};
    return {"release"};
}
inline bool releaseStageAllowed(const QString &stage, const QStringList &selection) {
    return releaseStages().contains(stage) && (selection.isEmpty() || selection.contains(stage));
}
} // namespace studio
