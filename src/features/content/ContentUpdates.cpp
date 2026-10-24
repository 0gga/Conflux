#include "ContentActions.h"
#include "ContentUpdateIntent.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include "domain/ReleaseStages.h"
#include <QPointer>
namespace studio {
int ContentActions::updateCount() const {
    int count = 0;
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        if (row.value("updateAvailable").toBool())
            ++count;
    }
    return count;
}
int ContentActions::actionableUpdateCount() const {
    int count = 0;
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        if (contentUpdateEligible(row))
            ++count;
    }
    return count;
}
void ContentActions::update(const QString &path) {
    const auto row = find(path);
    if (row.value("pending").toBool()) {
        reject("This content already has an active or queued operation. Wait for it to finish.");
        return;
    }
    if (!row.value("updateAvailable").toBool() || row.value("frozen").toBool() ||
        row.value("locked").toBool()) {
        if (row.value("locked").toBool())
            reject("Unlock this content before updating it.", row.value("title").toString());
        return;
    }
    install({contentUpdateRequest(row)});
}
void ContentActions::updateAll() {
    queueUpdates(m_rows);
}
void ContentActions::queueUpdates(const QVariantList &rows) {
    if (m_batchingUpdates)
        return;
    // Capture eligible intent once; publishing predictions must not invalidate this input.
    const auto pack = m_pack;
    const auto candidates = withPreferences(pack, rows);
    m_batchingUpdates = true;
    bool accepted = false;
    for (const auto &value : candidates)
        if (auto operation = planContentUpdate(pack, value.toMap()))
            accepted = enqueue(std::move(*operation)) || accepted;
    m_batchingUpdates = false;
    if (accepted)
        publishProjection();
}
void ContentActions::updateSelected() {
    queueUpdates(selection());
}

void ContentActions::openVersions(const QString &path) {
    m_versionTarget = find(path);
    m_versions.clear();
    m_versionError.clear();
    if (m_versionTarget.isEmpty())
        return;
    const int generation = ++m_versionGeneration;
    m_versionsBusy = true;
    emit changed();
    const QPointer<ContentActions> guard(this);
    m_catalogue->versions(m_versionTarget, m_pack.value("minecraft").toString(),
                          compatibleLoaders(m_pack.value("loader").toString(), m_rows).join('|'),
                          [guard, generation](QVector<Release> releases, QString error) {
                              if (!guard || generation != guard->m_versionGeneration)
                                  return;
                              guard->m_versions.clear();
                              for (const auto &release : releases)
                                  guard->m_versions << release.toVariant();
                              guard->m_versionError = error;
                              guard->m_versionsBusy = false;
                              emit guard->changed();
                          });
}
void ContentActions::installVersion(int index) {
    if (index < 0 || index >= m_versions.size() || m_versionTarget.value("frozen").toBool() ||
        find(m_versionTarget.value("relativePath").toString()).value("locked").toBool())
        return;
    auto project = m_versionTarget;
    project["type"] = categoryType(m_versionTarget.value("category").toString());
    installRelease(m_versions.at(index).toMap(), project);
}
bool ContentActions::releaseAllowed(const QVariantMap &release) const {
    return releaseStageAllowed(release.value("channel", "release").toString(),
                               m_releasePolicy ? m_releasePolicy->channels(m_pack.value("path").toString())
                                               : QStringList{});
}
bool ContentActions::installRelease(const QVariantMap &release, const QVariantMap &project) {
    auto type = project.value("type", "mod").toString();
    if (type == "datapack" && release.value("filename").toString().endsWith(".jar", Qt::CaseInsensitive))
        type = "mod";
    const auto target = project.value("targetPack", m_pack).toMap();
    const auto path = target.value("path").toString();
    const auto rows = path == m_pack.value("path").toString() ? m_rows : m_states.value(path).rows;
    auto ids = project.value("sourceIds").toMap();
    if (ids.isEmpty())
        ids[release.value("provider").toString()] = release.value("projectId");
    if (hasDataPackFormatConflict(contentCategory(type), ids, rows)) {
        m_notice = "This project is installed in another format. Remove that copy before switching formats.";
        emit operationFailed(m_notice);
        emit changed();
        return false;
    }
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto installed = row.value("sourceIds").toMap();
        bool match = false;
        for (auto it = ids.begin(); it != ids.end(); ++it)
            if (!it.value().toString().isEmpty() && installed.value(it.key()) == it.value())
                match = true;
        if (match && (row.value("locked").toBool() || row.value("frozen").toBool())) {
            m_notice = "Unlock this content or unfreeze its version before changing it.";
            emit operationFailed(m_notice);
            emit changed();
            return false;
        }
    }
    auto destination = project;
    destination["targetPack"] = target;
    return install({{releaseFromVariant(release), project.value("title").toString(),
                     QJsonObject::fromVariantMap(ids), type}},
                   destination);
}
} // namespace studio
