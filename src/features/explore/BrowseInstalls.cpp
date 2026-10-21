#include "BrowseInstalls.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <algorithm>
namespace studio {
BrowseInstalls::BrowseInstalls(std::shared_ptr<IContentCatalogue> source, QObject *parent)
    : QObject(parent), m_catalogue(std::move(source)) {}
void BrowseInstalls::setContext(const QVariantMap &pack, const QVariantList &rows, bool busy) {
    if (m_pack.value("path") != pack.value("path")) {
        m_notice.clear();
    }
    m_pack = pack;
    m_rows = rows;
    m_indexing = busy;
    emit changed();
}
void BrowseInstalls::setTargetPath(const QString &path) {
    if (m_target == path)
        return;
    m_target = path;
    m_notice.clear();
    emit changed();
}
QVariantMap BrowseInstalls::state(const QVariantMap &project) const {
    QVariantMap out{{"installed", false},
                    {"canInstall", ready() && !busy()},
                    {"label", ready() ? "Install" : "Choose instance"}};
    if (m_pendingLookups.contains(lookupKey(project))) {
        out["installed"] = true;
        out["canInstall"] = false;
        out["label"] = "Installed";
        return out;
    }
    const auto type = project.value("type", "mod").toString();
    if (!ready())
        return out;
    auto ids = project.value("sourceIds").toMap();
    if (ids.isEmpty())
        ids[project.value("provider").toString()] = project.value("id");
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        if (categoryType(row.value("category").toString()) != type &&
            !(type == "datapack" && row.value("category") == "mods"))
            continue;
        if (type == "datapack" && row.value("category") == "datapacks" &&
            !row.value("relativePath").toString().startsWith("datapacks/"))
            continue;
        const auto known = row.value("sourceIds").toMap();
        for (auto it = ids.cbegin(); it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty() && known.value(it.key()) == it.value()) {
                out["locked"] = row.value("locked").toBool();
                out["installed"] = true;
                out["canInstall"] = false;
                out["label"] = row.value("enabled", true).toBool() ? "Installed" : "Installed · disabled";
                return out;
            }
    }
    if (!ids.contains("modrinth")) {
        out["canInstall"] = false;
        out["label"] = "CurseForge pending";
    }
    return out;
}
bool BrowseInstalls::installed(const QVariantMap &release, const QString &type) const {
    if (!ready())
        return false;
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        const auto category =
            type == "datapack" && release.value("filename").toString().endsWith(".jar", Qt::CaseInsensitive)
                ? QString("mods")
                : contentCategory(type);
        if (!type.isEmpty() && row.value("category") != category)
            continue;
        if (row.value("category") == "datapacks" &&
            !row.value("relativePath").toString().startsWith("datapacks/"))
            continue;
        if (!release.value("sha1").toString().isEmpty() && !row.value("sha1").toString().isEmpty()) {
            if (row.value("sha1") == release.value("sha1"))
                return true;
            continue;
        }
        const auto version = row.value("installedRelease").toMap();
        if (!version.value("id").toString().isEmpty() &&
            version.value("provider") == release.value("provider") &&
            version.value("projectId") == release.value("projectId") &&
            version.value("id") == release.value("id"))
            return true;
    }
    return false;
}
void BrowseInstalls::installVersion(const QVariantMap &release, const QVariantMap &project) {
    if (!ready() || busy() || installed(release, project.value("type", "mod").toString()))
        return;
    const auto targetType = project.value("type", "mod").toString();
    const auto targetCategory =
        targetType == "datapack" && release.value("filename").toString().endsWith(".jar", Qt::CaseInsensitive)
            ? QString("mods")
            : contentCategory(targetType);
    if (hasDataPackFormatConflict(targetCategory, project.value("sourceIds").toMap(), m_rows)) {
        m_notice = "This project is installed in another format. Remove that copy before switching between "
                   "ZIP and mod variants.";
        emit changed();
        return;
    }
    auto target = project;
    target["targetPack"] = m_pack;
    target["targetRows"] = m_rows;
    if (project.value("type") == "datapack" &&
        release.value("filename").toString().endsWith(".jar", Qt::CaseInsensitive))
        target["type"] = "mod";
    m_notice.clear();
    emit changed();
    emit installRequested(release, target);
}
QString BrowseInstalls::lookupKey(const QVariantMap &project) const {
    auto ids = project.value("sourceIds").toMap();
    if (ids.isEmpty())
        ids[project.value("provider").toString()] = project.value("id");
    return m_pack.value("path").toString() + '|' + project.value("type", "mod").toString() + '|' +
           QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(ids)).toJson(QJsonDocument::Compact));
}
void BrowseInstalls::cancelPending() {
    ++m_lookupGeneration;
    m_pendingLookups.clear();
    emit changed();
}
void BrowseInstalls::install(const QVariantMap &project) {
    if (!state(project).value("canInstall").toBool())
        return;
    const auto key = lookupKey(project);
    const auto pack = m_pack;
    const auto rows = m_rows;
    const auto loaders = versionLoaders();
    const int generation = m_lookupGeneration;
    m_pendingLookups.insert(key);
    m_notice.clear();
    emit changed();
    m_lookups.enqueue([this, project, key, pack, rows, loaders,
                       generation](ContentOperationQueue::Completion done) {
        if (generation != m_lookupGeneration) {
            done();
            return;
        }
        auto row = project;
        row["category"] = contentCategory(project.value("type", "mod").toString());
        const QPointer<BrowseInstalls> guard(this);
        m_catalogue->versions(
            row, pack.value("minecraft").toString(), loaders,
            [guard, project, key, pack, rows, generation, done](QVector<Release> releases, QString error) {
                if (!guard)
                    return;
                if (generation != guard->m_lookupGeneration) {
                    done();
                    return;
                }
                if (project.value("type") == "datapack")
                    std::stable_sort(releases.begin(), releases.end(),
                                     [](const Release &a, const Release &b) {
                                         return a.filename.endsWith(".jar", Qt::CaseInsensitive) &&
                                                !b.filename.endsWith(".jar", Qt::CaseInsensitive);
                                     });
                bool installed = false;
                for (const auto &release : releases)
                    if (release.channel == "release" && release.provider == "modrinth" &&
                        !release.url.isEmpty() && !release.sha1.isEmpty() &&
                        release.gameVersions.contains(pack.value("minecraft").toString())) {
                        auto target = project;
                        target["targetPack"] = pack;
                        target["targetRows"] = rows;
                        if (project.value("type") == "datapack" &&
                            release.filename.endsWith(".jar", Qt::CaseInsensitive))
                            target["type"] = "mod";
                        emit guard->installRequested(release.toVariant(), target);
                        installed = true;
                        break;
                    }
                guard->m_pendingLookups.remove(key);
                if (!installed) {
                    const auto failure = error.isEmpty()
                                             ? QString("No compatible stable download is available. Open "
                                                       "Versions to review alternatives.")
                                             : error;
                    guard->m_notice = failure;
                    emit guard->operationFailed(failure);
                }
                emit guard->changed();
                done();
            });
    });
}
bool BrowseInstalls::connectorAvailable() const {
    return ready() && hasFabricBridge(m_pack.value("loader").toString(), m_rows);
}
QString BrowseInstalls::versionLoaders() const {
    return compatibleLoaders(m_pack.value("loader").toString(), m_rows).join('|');
}
} // namespace studio
