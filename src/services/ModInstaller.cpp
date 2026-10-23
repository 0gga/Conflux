#include "ModInstaller.h"
#include "domain/ContentCompatibility.h"
#include "domain/ReleaseStages.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrlQuery>
#include <QtConcurrent>

namespace studio {
ModInstaller::ModInstaller(QObject *parent, QUrl api)
    : QObject(parent), m_network(this), m_download(this, api.host() == "127.0.0.1"), m_api(std::move(api)) {
    connect(&m_download, &VerifiedDownload::progress, this, &ModInstaller::progress);
}
ModInstaller::~ModInstaller() {
    m_busy = false;
    m_download.cancel();
    ++m_generation;
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
    }
    m_staging.reset();
}
void ModInstaller::fail(const QString &message) {
    if (!m_busy)
        return;
    m_busy = false;
    m_download.cancel();
    m_applying = false;
    ++m_generation;
    auto *reply = m_reply;
    m_reply = nullptr;
    if (reply)
        reply->abort();
    m_staging.reset();
    emit finished({false, message, m_path});
}
void ModInstaller::cancel() {
    if (cancellable() && m_download.busy()) {
        m_download.cancel();
        return;
    }
    if (cancellable())
        fail("Mod installation cancelled. The pack manifest was not changed.");
}
void ModInstaller::get(const QUrl &url, std::function<void(QJsonDocument)> callback) {
    const auto generation = m_generation;
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
    auto *reply = m_network.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, callback = std::move(callback)] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        if (!m_busy || generation != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429
                     ? "Modrinth's rate limit was reached. Wait a moment, then retry."
                     : "Modrinth request failed: " + reply->errorString());
            return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            fail("Modrinth returned invalid JSON.");
            return;
        }
        callback(doc);
    });
}
void ModInstaller::start(const QString &path, const Release &release, const QString &title,
                         const QJsonObject &projectIds) {
    startBatch(path, {{release, title, projectIds}});
}
void ModInstaller::startBatch(const QString &path, const QVector<ContentInstallRequest> &requests,
                              bool allowFabricBridge, bool referenceOnly) {
    if (m_busy)
        return;
    m_busy = true;
    m_applying = false;
    m_referenceOnly = referenceOnly;
    ++m_generation;
    m_path = path;
    m_downloadIndex = 0;
    m_resolved.clear();
    m_shallowRoots.clear();
    for (const auto &request : requests)
        if (!request.resolveDependencies)
            m_shallowRoots.insert(request.release.projectId);
    m_allowedChannels.clear();
    // Every root belongs to one modpack; intersect explicit policies defensively.
    for (const auto &request : requests)
        if (!request.allowedChannels.isEmpty()) {
            if (m_allowedChannels.isEmpty())
                m_allowedChannels = request.allowedChannels;
            else
                for (qsizetype i = m_allowedChannels.size() - 1; i >= 0; --i)
                    if (!request.allowedChannels.contains(m_allowedChannels.at(i)))
                        m_allowedChannels.removeAt(i);
            if (m_allowedChannels.isEmpty()) {
                fail("Conflicting release stages were selected for this install.");
                return;
            }
        }
    m_existing.clear();
    m_disabledReferences.clear();
    m_titles.clear();
    m_tasks.clear();
    m_references = {};
    QFile manifest(QDir(path).filePath("pack.json"));
    if (!manifest.open(QIODevice::ReadOnly)) {
        fail("Could not read this pack's manifest.");
        return;
    }
    m_expected = QJsonDocument::fromJson(manifest.readAll()).object();
    if (m_expected.value("schemaVersion").toInt() != 1) {
        fail("Unsupported pack manifest.");
        return;
    }
    if (requests.isEmpty()) {
        fail("Choose at least one release to install.");
        return;
    }
    for (const auto &request : requests)
        if (request.release.provider != "modrinth") {
            fail("CurseForge downloads will be connected once API access is approved. Modrinth downloads are "
                 "ready now.");
            return;
        }
    m_game = m_expected.value("minecraft").toString();
    m_loader = m_expected.value("loader").toString();
    m_allowedLoaders = {m_loader};
    if (allowFabricBridge && QStringList{"forge", "neoforge"}.contains(m_loader))
        m_allowedLoaders << "fabric";
    m_rootIdentities.clear();
    m_rootPaths.clear();
    for (const auto &request : requests) {
        m_rootIdentities[request.release.projectId] = request.sourceIds;
        m_rootPaths[request.release.projectId] = request.installedPath;
    }
    m_staging = std::make_shared<QTemporaryDir>();
    if (!m_staging->isValid()) {
        fail("Could not create a temporary download folder.");
        return;
    }
    for (const auto &value : m_expected.value("mods").toArray()) {
        const auto existing = value.toObject();
        const auto provider = existing.value("provider").toString();
        const auto id = existing.value("projectId").toString();
        bool replacing = false;
        for (const auto &request : requests)
            if (id == request.sourceIds.value(provider).toString() ||
                (provider == "modrinth" && id == request.release.projectId))
                replacing = true;
        const bool disabled =
            provider == "modrinth" && existing.value("installPath").toString().endsWith(".disabled");
        if (disabled)
            m_disabledReferences[id] = existing;
        if (replacing)
            continue;
        if (disabled) {
            m_references.append(existing);
            continue;
        }
        if (provider != "modrinth") {
            m_references.append(existing);
            continue;
        }
        m_existing.insert(id, existing);
        // Unrelated pinned content is retained without fetching or resolving it.
    }
    for (const auto &request : requests)
        m_titles.insert(request.release.projectId, request.title);
    emit progress(m_shallowRoots.size() == requests.size() ? "Preparing selected versions…"
                                                           : "Resolving required dependencies…");
    for (const auto &request : requests)
        if (!addResolved(request.release))
            return;
    resolveNext();
}
bool ModInstaller::addResolved(const Release &release) {
    if (!releaseStageAllowed(release.channel, m_allowedChannels)) {
        fail("A required version is excluded by the modpack's release stages. No changes were made.");
        return false;
    }
    if (m_disabledReferences.contains(release.projectId) && !m_rootIdentities.contains(release.projectId)) {
        fail("A required dependency is disabled: " +
             m_disabledReferences.value(release.projectId).value("projectTitle").toString(release.projectId) +
             ". Enable it before retrying.");
        return false;
    }
    if (release.projectId.isEmpty() || release.id.isEmpty() || !release.gameVersions.contains(m_game) ||
        !sharesLoader(release.loaders, m_allowedLoaders)) {
        fail("A required release is incompatible with Minecraft " + m_game + " / " + m_loader +
             ". No changes were made.");
        return false;
    }
    if (!QRegularExpression("^[a-fA-F0-9]{40}$").match(release.sha1).hasMatch() ||
        !release.filename.endsWith(".jar", Qt::CaseInsensitive) || !safeDownloadUrl(QUrl(release.url))) {
        fail("A required release has no verifiable downloadable mod file.");
        return false;
    }
    if (m_resolved.contains(release.projectId)) {
        if (m_resolved.value(release.projectId).id != release.id) {
            fail("Dependency conflict: two mods require different releases of project " + release.projectId +
                 ".");
            return false;
        }
        return true;
    }
    m_resolved.insert(release.projectId, release);
    if (m_shallowRoots.contains(release.projectId))
        return true;
    for (const auto &value : release.dependencyRefs) {
        const auto dependency = value.toObject();
        if (dependency.value("dependency_type").toString() != "required")
            continue;
        const auto project = dependency.value("project_id").toString(),
                   version = dependency.value("version_id").toString();
        if (project.isEmpty() && version.isEmpty()) {
            fail("A required dependency has no project or version identifier.");
            return false;
        }
        m_tasks.enqueue({project, version});
    }
    return true;
}
void ModInstaller::resolveNext() {
    if (!m_busy)
        return;
    if (m_tasks.isEmpty()) {
        for (const auto &release : m_resolved)
            for (const auto &value : release.dependencyRefs) {
                if (m_shallowRoots.contains(release.projectId) ||
                    m_disabledReferences.contains(release.projectId))
                    continue;
                const auto dependency = value.toObject();
                if (dependency.value("dependency_type") == "required") {
                    for (const auto &other : m_resolved)
                        if (m_disabledReferences.contains(other.projectId) &&
                            (dependency.value("project_id").toString() == other.projectId ||
                             dependency.value("version_id").toString() == other.id)) {
                            fail("A required dependency is disabled: " + other.projectId +
                                 ". Enable it before installing these updates.");
                            return;
                        }
                }
                if (dependency.value("dependency_type").toString() != "incompatible")
                    continue;
                for (const auto &other : m_resolved) {
                    const auto project = dependency.value("project_id").toString(),
                               version = dependency.value("version_id").toString();
                    if ((!project.isEmpty() && project != other.projectId) ||
                        (!version.isEmpty() && version != other.id) ||
                        (project.isEmpty() && version.isEmpty()))
                        continue;
                    fail("Incompatible mods: " + m_titles.value(release.projectId, release.projectId) +
                         " and " + m_titles.value(other.projectId, other.projectId) + ".");
                    return;
                }
            }
        // Initial installs retain incompatibility guards from exact cached pins,
        // without resolving unrelated installed dependency graphs. Explicit updates
        // intentionally do not reconsider those graphs.
        const auto excludes = [](const QJsonObject &reference, const Release &candidate) {
            for (const auto &value : reference.value("dependencyRefs").toArray()) {
                const auto dependency = value.toObject();
                if (dependency.value("dependency_type") != "incompatible")
                    continue;
                const auto project = dependency.value("project_id").toString(),
                           version = dependency.value("version_id").toString();
                if ((!project.isEmpty() || !version.isEmpty()) &&
                    (project.isEmpty() || project == candidate.projectId) &&
                    (version.isEmpty() || version == candidate.id))
                    return true;
            }
            return false;
        };
        for (const auto &existing : m_existing)
            for (const auto &release : m_resolved) {
                if (m_shallowRoots.contains(release.projectId))
                    continue;
                if (excludes(existing, release) || excludes(QJsonObject::fromVariantMap(release.toVariant()),
                                                            releaseFromVariant(existing.toVariantMap()))) {
                    fail("This new install is incompatible with an installed project. No changes were made.");
                    return;
                }
            }
        for (auto it = m_existing.cbegin(); it != m_existing.cend(); ++it)
            if (!m_resolved.contains(it.key()))
                m_references.append(it.value());
        for (const auto &release : m_resolved) {
            auto reference = QJsonObject::fromVariantMap(release.toVariant());
            reference.insert("projectTitle", m_titles.value(release.projectId, release.projectId));
            if (!m_rootPaths.value(release.projectId).isEmpty())
                reference.insert("confluxReplacePath", m_rootPaths.value(release.projectId));
            reference.insert(
                "projectSourceIds",
                m_rootIdentities.value(release.projectId, QJsonObject{{"modrinth", release.projectId}}));
            bool enabled = true;
            for (const auto &value : m_expected.value("mods").toArray()) {
                const auto old = value.toObject();
                if (old.value("provider") == "modrinth" &&
                    old.value("projectId").toString() == release.projectId)
                    enabled = !old.value("installPath").toString().endsWith(".disabled");
            }
            reference.insert("enabled", enabled);
            reference.insert("installPath",
                             "mods/" + release.sha1.toLower() + ".jar" + (enabled ? "" : ".disabled"));
            m_references.append(reference);
        }
        downloadNext();
        return;
    }
    auto task = m_tasks.dequeue();
    if (m_resolved.contains(task.projectId)) {
        if (!task.versionId.isEmpty() && m_resolved.value(task.projectId).id != task.versionId) {
            fail("Dependency conflict: required version " + task.versionId +
                 " differs from the selected release of " + task.projectId + ".");
            return;
        }
        resolveNext();
        return;
    }
    if (m_existing.contains(task.projectId)) {
        const auto pinned = m_existing.value(task.projectId).value("id").toString();
        if (!task.versionId.isEmpty() && task.versionId != pinned) {
            fail("A required dependency conflicts with a version already pinned in this pack.");
            return;
        }
        task.versionId = pinned;
    }
    QUrl url;
    if (!task.versionId.isEmpty())
        url = m_api.resolved(QUrl("version/" + task.versionId));
    else {
        url = m_api.resolved(QUrl("project/" + task.projectId + "/version"));
        QUrlQuery query;
        query.addQueryItem(
            "game_versions",
            QString::fromUtf8(QJsonDocument(QJsonArray{m_game}).toJson(QJsonDocument::Compact)));
        query.addQueryItem(
            "loaders",
            QString::fromUtf8(
                QJsonDocument(QJsonArray::fromStringList(m_allowedLoaders)).toJson(QJsonDocument::Compact)));
        url.setQuery(query);
    }
    get(url, [this, task](const QJsonDocument &doc) {
        QJsonObject chosen;
        if (!task.versionId.isEmpty())
            chosen = doc.object();
        else {
            // Prefer the newest compatible stable release; use beta/alpha only if none exists.
            for (const auto &value : doc.array()) {
                const auto candidate = value.toObject();
                const auto release = modrinthRelease(candidate);
                if (!releaseStageAllowed(release.channel, m_allowedChannels) ||
                    !release.gameVersions.contains(m_game) ||
                    !sharesLoader(release.loaders, m_allowedLoaders))
                    continue;
                const bool stable = candidate.value("version_type").toString() == "release",
                           oldStable = chosen.value("version_type").toString() == "release";
                if (chosen.isEmpty() || (stable && !oldStable) ||
                    (stable == oldStable && candidate.value("date_published").toString() >
                                                chosen.value("date_published").toString()))
                    chosen = candidate;
            }
        }
        const auto release = modrinthRelease(chosen);
        if ((!task.projectId.isEmpty() && task.projectId != release.projectId) ||
            (!task.versionId.isEmpty() && task.versionId != release.id)) {
            fail("Modrinth returned an unexpected dependency identity.");
            return;
        }
        if (m_existing.contains(release.projectId) &&
            m_existing.value(release.projectId)
                    .value("sha1")
                    .toString()
                    .compare(release.sha1, Qt::CaseInsensitive) != 0) {
            fail("The file for a pinned release changed on Modrinth. Review that mod before retrying.");
            return;
        }
        if (!addResolved(release))
            return;
        const auto id = release.projectId;
        get(m_api.resolved(QUrl("project/" + id)), [this, id](const QJsonDocument &metadata) {
            m_titles.insert(id, metadata.object().value("title").toString(id));
            emit progress("Resolving dependencies · " + QString::number(m_resolved.size()) + " mods");
            resolveNext();
        });
    });
}
bool ModInstaller::safeDownloadUrl(const QUrl &url) const {
    if (url.scheme() == "https" && !url.host().isEmpty())
        return true;
    // Local fixture servers are allowed only when the API itself was injected as loopback.
    return m_api.host() == "127.0.0.1" && url.scheme() == "http" && url.host() == "127.0.0.1";
}
void ModInstaller::downloadNext() {
    if (!m_busy)
        return;
    if (m_downloadIndex >= m_references.size()) {
        apply();
        return;
    }
    const auto ref = m_references.at(m_downloadIndex++).toObject();
    if (m_referenceOnly && ref.contains("confluxRetainedPath")) {
        downloadNext();
        return;
    }
    if (ref.value("provider").toString() != "modrinth") {
        downloadNext();
        return;
    }
    if (m_existing.contains(ref.value("projectId").toString()) &&
        !m_resolved.contains(ref.value("projectId").toString())) {
        downloadNext();
        return;
    }
    const auto sha1 = ref.value("sha1").toString().toLower();
    const auto existingFile = QDir(m_path).filePath(ref.value("installPath").toString());
    if (QFileInfo::exists(existingFile)) {
        const auto generation = m_generation;
        emit progress("Checking installed " + ref.value("projectTitle").toString() + "…");
        auto *watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, generation] {
            const bool valid = watcher->result();
            watcher->deleteLater();
            if (!m_busy || generation != m_generation)
                return;
            if (!valid) {
                fail("An installed mod file was changed outside Conflux. It was left untouched; review it "
                     "before retrying.");
                return;
            }
            downloadNext();
        });
        watcher->setFuture(QtConcurrent::run([existingFile, ref] {
            QFile input(existingFile);
            if (QFileInfo(existingFile).isSymLink() || !input.open(QIODevice::ReadOnly))
                return false;
            const auto size = ref.value("fileSize").toInteger();
            if (size > 0 && input.size() != size)
                return false;
            const bool strong = !ref.value("sha512").toString().isEmpty();
            QCryptographicHash hash(strong ? QCryptographicHash::Sha512 : QCryptographicHash::Sha1);
            return hash.addData(&input) && QString::fromLatin1(hash.result().toHex()) ==
                                               ref.value(strong ? "sha512" : "sha1").toString().toLower();
        }));
        return;
    }
    const auto generation = m_generation;
    emit progress("Downloading " + ref.value("projectTitle").toString() + "…");
    m_download.start(releaseFromVariant(ref.toVariantMap()), m_staging->path(), {},
                     [this, generation](VerifiedDownloadResult result) {
                         if (!m_busy || generation != m_generation)
                             return;
                         if (!result.error.isEmpty()) {
                             fail(result.error);
                             return;
                         }
                         downloadNext();
                     });
}

void ModInstaller::apply() {
    m_applying = true;
    emit progress("Applying verified mod files…");
    auto *watcher = new QFutureWatcher<RepositoryResult>(this);
    const auto path = m_path;
    const auto staging = m_staging;
    const auto manifest = m_expected;
    const auto refs = m_references;
    const auto referenceOnly = m_referenceOnly;
    connect(watcher, &QFutureWatcher<RepositoryResult>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        m_applying = false;
        m_staging.reset();
        emit finished(result);
    });
    watcher->setFuture(QtConcurrent::run([path, staging, manifest, refs, referenceOnly] {
        return PackRepository::applyInstallation(path, manifest, refs, staging->path(), referenceOnly);
    }));
}
} // namespace studio
