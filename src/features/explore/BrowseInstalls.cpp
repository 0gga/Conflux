#include "BrowseInstalls.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include "domain/ReleaseStages.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <algorithm>
namespace studio {
static QString lookupScope(const QString &path) {
    auto result = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}
static QVariantMap verifiedRowProjects(const QVariantMap &row) {
    auto ids = row.value("sourceIds").toMap();
    const auto release = row.value("installedRelease").toMap();
    const auto hash = row.value("sha1").toString();
    const auto pinnedHash = release.value("sha1").toString();
    if (!hash.isEmpty() && !pinnedHash.isEmpty() && hash.compare(pinnedHash, Qt::CaseInsensitive) != 0)
        return ids;
    for (const auto &source : {row, release}) {
        const auto mapped = source.value("projectSourceIds").toMap();
        for (auto it = mapped.cbegin(); it != mapped.cend(); ++it)
            if (!it.value().toString().isEmpty())
                ids.insert(it.key(), it.value());
        const auto provider = source.value("provider").toString(),
                   project = source.value("projectId").toString();
        if ((provider == "modrinth" || provider == "curseforge") && !project.isEmpty())
            ids.insert(provider, project);
    }
    return ids;
}
static bool installedInRows(const QVariantMap &release, const QString &type, const QVariantList &rows) {
    const auto category =
        type == "datapack" && release.value("filename").toString().endsWith(".jar", Qt::CaseInsensitive)
            ? QString("mods")
            : contentCategory(type);
    for (const auto &value : rows) {
        const auto row = value.toMap();
        if ((!type.isEmpty() && row.value("category") != category) ||
            (row.value("category") == "datapacks" &&
             !row.value("relativePath").toString().startsWith("datapacks/")))
            continue;
        const auto wanted = release.value("sha1").toString(), actual = row.value("sha1").toString();
        if (!wanted.isEmpty() && !actual.isEmpty()) {
            if (wanted.compare(actual, Qt::CaseInsensitive) == 0)
                return true;
            continue;
        }
        const auto pin = row.value("installedRelease").toMap();
        if (!pin.value("id").toString().isEmpty() && pin.value("id") == release.value("id") &&
            pin.value("provider") == release.value("provider") &&
            pin.value("projectId") == release.value("projectId"))
            return true;
    }
    return false;
}
BrowseInstalls::BrowseInstalls(std::shared_ptr<IContentCatalogue> source, QObject *parent)
    : QObject(parent), m_catalogue(std::move(source)) {}
void BrowseInstalls::setReleasePolicy(std::shared_ptr<PackReleasePolicy> policy) {
    if (m_releasePolicy)
        disconnect(m_releasePolicy.get(), nullptr, this, nullptr);
    m_releasePolicy = std::move(policy);
    if (m_releasePolicy)
        connect(m_releasePolicy.get(), &PackReleasePolicy::policyChanged, this,
                [this](const QString &path) { cancelPendingForPack(path); });
}
bool BrowseInstalls::releaseAllowed(const QVariantMap &release) const {
    return releaseStageAllowed(release.value("channel", "release").toString(),
                               m_releasePolicy ? m_releasePolicy->channels(m_pack.value("path").toString())
                                               : QStringList{});
}
void BrowseInstalls::setContext(const QVariantMap &pack, const QVariantList &rows, bool busy) {
    if (!m_identitiesChanged && pack == m_pack && rows == m_inputRows && busy == m_indexing)
        return;
    m_identitiesChanged = false;
    m_inputRows = rows;
    m_projectRows.clear();
    if (m_pack.value("path") != pack.value("path")) {
        m_notice.clear();
    }
    m_pack = pack;
    m_rows = rows;
    int rowIndex = 0;
    for (auto &value : m_rows) {
        auto row = value.toMap();
        auto ids = verifiedRowProjects(row);
        const auto learned = m_verifiedIdentities.value(row.value("sha1").toString().toLower());
        for (auto it = learned.cbegin(); it != learned.cend(); ++it)
            if (!ids.contains(it.key()) || ids.value(it.key()) == it.value())
                ids.insert(it.key(), it.value());
        if (!ids.isEmpty())
            row["sourceIds"] = ids;
        const auto category = row.value("category").toString();
        const bool destination =
            category != "datapacks" || row.value("relativePath").toString().startsWith("datapacks/");
        if (destination)
            for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
                const auto key = category + QChar(0x1f) + it.key() + ':' + it.value().toString();
                if (!it.value().toString().isEmpty() && !m_projectRows.contains(key))
                    m_projectRows.insert(key, rowIndex);
            }
        if (destination) {
            const auto proof = category + QChar(0x1f) + row.value("sha1").toString().toLower();
            for (const auto &project : m_verifiedFileProjects.value(proof)) {
                const auto key = category + QChar(0x1f) + project;
                if (!m_projectRows.contains(key))
                    m_projectRows.insert(key, rowIndex);
            }
        }
        value = row;
        ++rowIndex;
    }
    m_indexing = busy;
    ++m_revision;
    emit changed();
}
void BrowseInstalls::setTargetPath(const QString &path) {
    if (m_target == path)
        return;
    m_target = path;
    m_notice.clear();
    ++m_revision;
    emit changed();
}
void BrowseInstalls::rememberReleases(const QVariantMap &project, const QVariantList &groups) {
    if (!ready() || groups.isEmpty())
        return;
    QHash<QString, QVariantMap> present;
    QHash<QString, QSet<QString>> categories;
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        if (categoryType(row.value("category").toString()) != project.value("type", "mod").toString() &&
            !(project.value("type") == "datapack" && row.value("category") == "mods"))
            continue;
        const auto hash = row.value("sha1").toString().toLower();
        if (!hash.isEmpty()) {
            present.insert(hash, verifiedRowProjects(row));
            categories[hash].insert(row.value("category").toString());
        }
    }
    bool changed = false;
    for (const auto &value : groups)
        for (const auto &variant : value.toMap().value("variants").toList()) {
            const auto release = variant.toMap();
            const auto hash = release.value("sha1").toString().toLower();
            if (hash.isEmpty() || !present.contains(hash))
                continue;
            auto ids = project.value("sourceIds").toMap();
            if (ids.isEmpty())
                ids.insert(project.value("provider").toString(), project.value("id"));
            const auto provider = release.value("provider").toString(),
                       id = release.value("projectId").toString();
            if (!provider.isEmpty() && !id.isEmpty())
                ids.insert(provider, id);
            // File presence proves this project's release is installed without
            // changing another authoritative same-provider project identity.
            for (const auto &category : categories.value(hash)) {
                const auto key = category + QChar(0x1f) + hash;
                if (!m_verifiedFileProjects.contains(key) && m_verifiedFileProjects.size() >= 2048)
                    m_verifiedFileProjects.clear();
                auto &proven = m_verifiedFileProjects[key];
                for (auto it = ids.cbegin(); it != ids.cend(); ++it)
                    if (!it.value().toString().isEmpty()) {
                        const auto projectKey = it.key() + ':' + it.value().toString();
                        if (!proven.contains(projectKey)) {
                            proven.insert(projectKey);
                            changed = true;
                        }
                    }
            }
            auto known = present.value(hash);
            const auto learned = m_verifiedIdentities.value(hash);
            for (auto it = learned.cbegin(); it != learned.cend(); ++it)
                known.insert(it.key(), it.value());
            bool ambiguous = false;
            for (auto it = ids.cbegin(); it != ids.cend(); ++it)
                if (known.contains(it.key()) && known.value(it.key()) != it.value())
                    ambiguous = true;
            if (ambiguous)
                continue;
            for (auto it = ids.cbegin(); it != ids.cend(); ++it)
                if (!it.value().toString().isEmpty())
                    known.insert(it.key(), it.value());
            if (learned != known) {
                if (m_verifiedIdentities.size() >= 2048)
                    m_verifiedIdentities.clear();
                m_verifiedIdentities.insert(hash, known);
                changed = true;
            }
        }
    if (changed) {
        m_identitiesChanged = true;
        const auto rows = m_rows;
        setContext(m_pack, rows, m_indexing);
    }
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
    int index = -1;
    const QStringList categories =
        type == "datapack" ? QStringList{"datapacks", "mods"} : QStringList{contentCategory(type)};
    for (const auto &category : categories)
        for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
            const auto found =
                m_projectRows.constFind(category + QChar(0x1f) + it.key() + ':' + it.value().toString());
            if (found != m_projectRows.cend() && (index < 0 || *found < index))
                index = *found;
        }
    if (index >= 0) {
        const auto row = m_rows.at(index).toMap();
        out["locked"] = row.value("locked").toBool();
        out["installed"] = true;
        out["canInstall"] = false;
        out["label"] = row.value("enabled", true).toBool() ? "Installed" : "Installed \u00b7 disabled";
        return out;
    }
    if (ids.value("modrinth").toString().isEmpty() && !m_curseforgeEnabled) {
        out["canInstall"] = false;
        out["label"] = "Connect CurseForge";
    }
    return out;
}
bool BrowseInstalls::installed(const QVariantMap &release, const QString &type) const {
    return ready() && installedInRows(release, type, m_rows);
}
void BrowseInstalls::installVersion(const QVariantMap &release, const QVariantMap &project) {
    if (!releaseAllowed(release)) {
        m_notice = "This version is excluded by the modpack's release stages.";
        ++m_revision;
        emit changed();
        emit operationFailed(m_notice);
        return;
    }
    if (release.value("provider") == "curseforge" && !m_curseforgeEnabled) {
        m_notice = "Connect an approved CurseForge API key in Settings.";
        ++m_revision;
        emit changed();
        emit operationFailed(m_notice);
        return;
    }
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
        ++m_revision;
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
    ++m_revision;
    emit changed();
    emit installRequested(release, target);
}
QString BrowseInstalls::lookupKey(const QVariantMap &project) const {
    auto ids = project.value("sourceIds").toMap();
    if (ids.isEmpty())
        ids[project.value("provider").toString()] = project.value("id");
    return lookupScope(m_pack.value("path").toString()) + '|' + project.value("type", "mod").toString() +
           '|' +
           QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(ids)).toJson(QJsonDocument::Compact));
}
void BrowseInstalls::cancelPending() {
    cancelPendingForPack({});
}
void BrowseInstalls::cancelPendingForPack(const QString &path) {
    const auto scope = path.isEmpty() ? QString{} : lookupScope(path);
    if (scope.isEmpty()) {
        ++m_lookupGeneration;
        m_pendingLookups.clear();
        m_lookupPacks.clear();
        m_packLookupGenerations.clear();
    } else {
        ++m_packLookupGenerations[scope];
        for (auto it = m_lookupPacks.begin(); it != m_lookupPacks.end();) {
            if (it.value() == scope) {
                m_pendingLookups.remove(it.key());
                it = m_lookupPacks.erase(it);
            } else
                ++it;
        }
    }
    m_lookups.discardQueued(scope);
    if (scope.isEmpty() || m_activeLookupPack == scope) {
        auto done = std::move(m_activeLookup);
        m_activeLookupPack.clear();
        m_catalogue->cancelVersions();
        if (done)
            done();
    }
    ++m_revision;
    emit changed();
}
void BrowseInstalls::install(const QVariantMap &project) {
    if (!state(project).value("canInstall").toBool())
        return;
    const auto key = lookupKey(project);
    const auto pack = m_pack;
    const auto rows = m_rows;
    const auto loaders = versionLoaders();
    const auto channels =
        m_releasePolicy ? m_releasePolicy->channels(pack.value("path").toString()) : QStringList{};
    const auto permittedChannels = channels.isEmpty() ? QStringList{"release"} : channels;
    const int generation = m_lookupGeneration;
    const auto packPath = lookupScope(pack.value("path").toString());
    const int packGeneration = m_packLookupGenerations.value(packPath);
    m_pendingLookups.insert(key);
    m_lookupPacks.insert(key, packPath);
    m_notice.clear();
    ++m_revision;
    emit changed();
    m_lookups.enqueue(
        [this, project, key, pack, rows, loaders, permittedChannels, generation, packGeneration,
         packPath](ContentOperationQueue::Completion done) {
            if (generation != m_lookupGeneration ||
                packGeneration != m_packLookupGenerations.value(packPath)) {
                done();
                return;
            }
            m_activeLookup = done;
            m_activeLookupPack = packPath;
            auto row = project;
            row["category"] = contentCategory(project.value("type", "mod").toString());
            const QPointer<BrowseInstalls> guard(this);
            m_catalogue->versions(
                row, pack.value("minecraft").toString(), loaders,
                [guard, project, key, pack, rows, generation, packGeneration, packPath, done,
                 permittedChannels](QVector<Release> releases, QString error) {
                    if (!guard)
                        return;
                    if (generation != guard->m_lookupGeneration ||
                        packGeneration != guard->m_packLookupGenerations.value(packPath)) {
                        done();
                        return;
                    }
                    guard->m_activeLookup = {};
                    guard->m_activeLookupPack.clear();
                    if (project.value("type") == "datapack")
                        std::stable_sort(releases.begin(), releases.end(),
                                         [](const Release &a, const Release &b) {
                                             return a.filename.endsWith(".jar", Qt::CaseInsensitive) &&
                                                    !b.filename.endsWith(".jar", Qt::CaseInsensitive);
                                         });
                    bool installed = false;
                    for (const auto &release : releases)
                        if (permittedChannels.contains(release.channel) &&
                            (release.provider == "modrinth" ||
                             (release.provider == "curseforge" && guard->m_curseforgeEnabled)) &&
                            !release.url.isEmpty() && !release.sha1.isEmpty() &&
                            release.gameVersions.contains(pack.value("minecraft").toString())) {
                            const auto &inventory =
                                lookupScope(guard->m_pack.value("path").toString()) == packPath
                                    ? guard->m_rows
                                    : rows;
                            if (installedInRows(release.toVariant(), project.value("type", "mod").toString(),
                                                inventory)) {
                                if (lookupScope(guard->m_pack.value("path").toString()) == packPath)
                                    guard->rememberReleases(
                                        project,
                                        {QVariantMap{{"variants", QVariantList{release.toVariant()}}}});
                                installed = true;
                                break;
                            }
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
                    if (!guard || generation != guard->m_lookupGeneration ||
                        packGeneration != guard->m_packLookupGenerations.value(packPath)) {
                        done();
                        return;
                    }
                    guard->m_pendingLookups.remove(key);
                    guard->m_lookupPacks.remove(key);
                    if (!installed) {
                        const auto failure =
                            error.isEmpty()
                                ? QString("No compatible download matches the modpack's release stages. Open "
                                          "Versions to review alternatives.")
                                : error;
                        guard->m_notice = failure;
                        emit guard->operationFailed(failure);
                    }
                    ++guard->m_revision;
                    emit guard->changed();
                    done();
                });
        },
        packPath);
}
bool BrowseInstalls::connectorAvailable() const {
    return ready() && hasFabricBridge(m_pack.value("loader").toString(), m_rows);
}
QString BrowseInstalls::versionLoaders() const {
    return compatibleLoaders(m_pack.value("loader").toString(), m_rows).join('|');
}
} // namespace studio
