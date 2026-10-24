#include "ContentInstaller.h"
#include "ContentAccess.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include "domain/ReleaseStages.h"
#include "features/content/ContentOperationState.h"
#include "platform/PackFileStore.h"
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QtConcurrent>
#include <filesystem>
namespace studio {
ContentInstaller::ContentInstaller(std::shared_ptr<const NativeContentStore> files, QObject *parent,
                                   bool local, QUrl apiRoot, QUrl curseforgeApi)
    : QObject(parent), m_installer(this, !apiRoot.isEmpty() ? apiRoot
                                         : local            ? QUrl("http://127.0.0.1/")
                                                            : QUrl("https://api.modrinth.com/v2/")),
      m_curseforge(this, curseforgeApi.isEmpty() ? QUrl("https://api.curseforge.com/v1/") : curseforgeApi),
      m_download(this, local), m_files(std::move(files)) {
    connect(&m_download, &VerifiedDownload::progress, this, &ContentInstaller::progress);
    connect(&m_installer, &ModInstaller::progress, this, &ContentInstaller::progress);
    connect(&m_installer, &ModInstaller::finished, this,
            [this](const RepositoryResult &result) { finishBatch({result.ok, result.error}); });
}
void ContentInstaller::finish(ContentWriteResult result) {
    if (result.ok)
        for (const auto &request : m_directRequests) {
            QVariantMap previous;
            for (const auto &value : m_targetRows) {
                const auto row = value.toMap();
                if (!request.installedPath.isEmpty() && row.value("relativePath") == request.installedPath)
                    previous = row;
                if (!request.release.sha1.isEmpty() &&
                    row.value("sha1").toString().compare(request.release.sha1, Qt::CaseInsensitive) == 0)
                    previous = row;
                const auto ids = row.value("sourceIds").toMap();
                for (auto it = request.sourceIds.begin(); it != request.sourceIds.end(); ++it)
                    if (!it.value().toString().isEmpty() && ids.value(it.key()) == it.value().toVariant())
                        previous = row;
            }
            result.installedPredictions << contentInstallPrediction(request, m_targetPack, previous);
        }
    auto callback = std::move(m_callback);
    m_callback = {};
    m_stage.reset();
    m_rows.clear();
    m_targetRows.clear();
    m_pack.clear();
    m_targetPack.clear();
    m_batches.clear();
    m_directRequests.clear();
    m_directMods = {};
    m_directAssets = {};
    m_preparing = false;
    m_committing = false;
    if (callback)
        callback(std::move(result));
}
void ContentInstaller::finishBatch(ContentWriteResult result) {
    if (!result.ok) {
        result.changed = false;
        finish(std::move(result));
        return;
    }
    if (!result.error.isEmpty())
        m_warnings << result.error;
    const auto stage = m_stage;
    const auto path = m_pack.value("path").toString();
    auto *watcher = new QFutureWatcher<RepositoryResult>(this);
    connect(watcher, &QFutureWatcher<RepositoryResult>::finished, this, [this, watcher] {
        const auto current = watcher->result();
        watcher->deleteLater();
        if (!current.ok) {
            finish({false, current.error});
            return;
        }
        m_pack["mods"] = current.manifest.value("mods").toArray().toVariantList();
        m_pack["assets"] = current.manifest.value("assets").toArray().toVariantList();
        startNextBatch();
    });
    watcher->setFuture(QtConcurrent::run([stage, path] { return PackRepository::inspectContent(path); }));
}
void ContentInstaller::startNextBatch() {
    if (m_cancelled) {
        finish({false, "Installation cancelled before changing installed files."});
        return;
    }
    if (!m_batches.isEmpty()) {
        installBatch(m_batches.takeFirst());
        return;
    }
    // Every download and dependency update has succeeded in the isolated workspace.
    // Only this one commit can touch the destination instance.
    m_committing = true;
    emit progress("Applying verified content…");
    const auto pack = m_targetPack;
    const auto rows = m_targetRows;
    const auto stage = m_stage;
    const auto path = m_pack.value("path").toString();
    const auto files = m_files;
    const auto shallow = m_shallowProjects;
    auto *watcher = new QFutureWatcher<ContentWriteResult>(this);
    connect(watcher, &QFutureWatcher<ContentWriteResult>::finished, this, [this, watcher] {
        auto result = watcher->result();
        watcher->deleteLater();
        if (result.ok && !m_warnings.isEmpty())
            result.error = m_warnings.join('\n');
        finish(std::move(result));
    });
    watcher->setFuture(QtConcurrent::run([pack, rows, stage, path, files, shallow] {
        const auto current = PackRepository::inspectContent(path);
        if (!current.ok)
            return ContentWriteResult{false, current.error};
        return files->commitStagedContent(pack, rows, path, current.manifest, shallow, true);
    }));
}
void ContentInstaller::install(const QVariantMap &pack, const QVariantList &rows,
                               const QVector<ContentInstallRequest> &requests, Callback callback) {
    if (m_callback) {
        callback({false, "Another content operation is in progress."});
        return;
    }
    if (requests.isEmpty()) {
        callback({false, "Choose content to install first."});
        return;
    }
    for (const auto &request : requests) {
        if (!releaseStageAllowed(request.release.channel, request.allowedChannels)) {
            callback({false, "This version is excluded by the modpack's release stages."});
            return;
        }
        if (!request.release.gameVersions.contains(pack.value("minecraft").toString())) {
            callback({false, "The selected version does not support this instance's Minecraft version."});
            return;
        }
        if (contentCategory(request.contentType).isEmpty() ||
            !request.release.filename.endsWith(request.contentType == "mod" ? ".jar" : ".zip",
                                               Qt::CaseInsensitive)) {
            callback({false, "Choose a supported content archive."});
            return;
        }
        for (const auto &value : rows) {
            const auto row = value.toMap();
            bool selected =
                !request.installedPath.isEmpty() && row.value("relativePath") == request.installedPath;
            const auto ids = row.value("sourceIds").toMap();
            for (auto it = request.sourceIds.begin(); it != request.sourceIds.end(); ++it)
                selected |= !it.value().toString().isEmpty() &&
                            ids.value(it.key()).toString() == it.value().toString();
            if (selected && (row.value("locked").toBool() || row.value("frozen").toBool())) {
                callback({false, "Unlock this content before changing it."});
                return;
            }
        }
        if (request.dependenciesHandled && request.contentType == "mod" &&
            !sharesLoader(request.release.loaders,
                          compatibleLoaders(pack.value("loader").toString(), rows))) {
            callback({false, "The selected version does not support this instance's loader."});
            return;
        }
        if (!request.resolveDependencies) {
            if (!request.release.gameVersions.contains(pack.value("minecraft").toString())) {
                callback({false, "The selected version does not support this instance's Minecraft version."});
                return;
            }
            if (request.contentType != "mod")
                continue;
            const auto primary = pack.value("loader").toString();
            QStringList permitted{primary};
            for (const auto &value : rows) {
                const auto row = value.toMap();
                const auto ids = row.value("sourceIds").toMap();
                bool same = false;
                for (auto it = request.sourceIds.begin(); it != request.sourceIds.end(); ++it)
                    if (!it.value().toString().isEmpty() &&
                        ids.value(it.key()).toString() == it.value().toString())
                        same = true;
                if (!same || row.value("category") != "mods")
                    continue;
                auto installed = row.value("installedRelease").toMap().value("loaders").toStringList();
                if (installed.isEmpty())
                    installed = row.value("loaders").toStringList();
                if ((primary == "forge" || primary == "neoforge") && installed.contains("forge-family"))
                    installed << primary;
                if (!installed.isEmpty() && !installed.contains(primary)) {
                    permitted.clear();
                    for (const auto &loader : compatibleLoaders(primary, rows))
                        if (installed.contains(loader))
                            permitted << loader;
                }
            }
            if (!sharesLoader(request.release.loaders, permitted)) {
                callback({false, "The selected version does not support this content's installed loader."});
                return;
            }
        }
    }
    m_callback = std::move(callback);
    m_targetPack = pack;
    m_targetRows = rows;
    m_cancelled = false;
    m_warnings.clear();
    m_batches.clear();
    m_shallowProjects.clear();
    for (const auto &request : requests)
        if (!request.resolveDependencies)
            m_shallowProjects.insert(request.release.projectId);
    m_committing = false;
    m_preparing = true;
    m_curseforge.prepare(pack, rows, requests,
                         [this](QVector<ContentInstallRequest> prepared, QString error) {
                             m_preparing = false;
                             if (!error.isEmpty()) {
                                 finish({false, error});
                                 return;
                             }
                             prepareDownloads(prepared);
                         });
}
void ContentInstaller::prepareDownloads(const QVector<ContentInstallRequest> &requests) {
    const auto pack = m_targetPack;
    const auto rows = m_targetRows;
    bool direct = !requests.isEmpty();
    for (const auto &request : requests)
        direct &= request.dependenciesHandled || !request.resolveDependencies || request.contentType != "mod";
    if (direct) {
        m_directRequests = requests;
        m_directIndex = 0;
        m_stage = std::make_shared<QTemporaryDir>();
        if (!m_stage->isValid()) {
            finish({false, "Could not create the download workspace."});
            return;
        }
        downloadDirect();
        return;
    }
    QVector<ContentInstallRequest> mods;
    for (const auto &request : requests) {
        if (request.contentType == "mod" && request.release.provider != "curseforge")
            mods << request;
        else
            m_batches << QVector<ContentInstallRequest>{request};
    }
    if (!mods.isEmpty())
        m_batches << mods;
    m_stage = std::make_shared<QTemporaryDir>();
    if (!m_stage->isValid()) {
        finish({false, "Could not create the content transaction workspace."});
        return;
    }
    m_preparing = true;
    emit progress("Preparing content…");
    const auto stage = m_stage;
    const auto files = m_files;
    auto *watcher = new QFutureWatcher<ContentFileState>(this);
    connect(watcher, &QFutureWatcher<ContentFileState>::finished, this, [this, watcher] {
        auto result = watcher->result();
        watcher->deleteLater();
        m_preparing = false;
        if (!result.error.isEmpty()) {
            finish({false, result.error});
            return;
        }
        m_pack = result.pack;
        m_rows = result.rows;
        startNextBatch();
    });
    watcher->setFuture(QtConcurrent::run([pack, rows, stage, files] {
        ContentAccess access(pack.value("path").toString());
        if (!access.ok())
            return ContentFileState{pack, {}, access.error()};
        auto result =
            PackRepository::create(stage->path(), "Content transaction", pack.value("minecraft").toString(),
                                   pack.value("loader").toString());
        if (!result.ok)
            return ContentFileState{pack, {}, result.error};
        const auto actual = files->snapshot(pack, rows);
        QMap<QString, QString> hashes;
        for (const auto &value : actual) {
            const auto row = value.toMap();
            hashes[row.value("relativePath").toString()] = row.value("sha1").toString();
        }
        QJsonArray pins, assets;
        for (const auto &value : rows) {
            const auto row = value.toMap();
            if (row.value("directory").toBool())
                continue;
            const auto relative = row.value("relativePath").toString();
            if (!QStringList{"mods", "resourcepacks", "shaderpacks", "datapacks"}.contains(
                    row.value("category").toString()))
                continue;
            if (row.value("sha1").toString().isEmpty() ||
                hashes.value(relative) != row.value("sha1").toString())
                return ContentFileState{
                    pack, {}, "Installed content changed before staging. Refresh and retry."};
            auto ref = QJsonObject::fromVariantMap(row.value("installedRelease").toMap());
            auto stagedRelative = relative;
            if (!ref.isEmpty() && row.value("category") == "mods") {
                const auto hash = row.value("sha1").toString().toLower();
                if (ref.value("sha1").toString().compare(hash, Qt::CaseInsensitive) != 0)
                    return ContentFileState{
                        pack, {}, "Installed release metadata differs from its file. Refresh and retry."};
                // The isolated workspace is a managed pack, even for external instances.
                // Retained pins must satisfy its hash-path contract without renaming input files.
                stagedRelative = "mods/" + hash + ".jar" + (row.value("enabled").toBool() ? "" : ".disabled");
                ref["sha1"] = hash;
            }
            if (!ref.isEmpty()) {
                ref["confluxRetainedPath"] = relative;
                ref["installPath"] = stagedRelative;
                ref["projectTitle"] = row.value("title").toString();
                ref["projectSourceIds"] = QJsonObject::fromVariantMap(row.value("sourceIds").toMap());
                ref["enabled"] = row.value("enabled").toBool();
                if (row.value("category") == "mods")
                    pins << ref;
                else {
                    ref["category"] = row.value("category").toString();
                    assets << ref;
                }
            }
        }
        const auto saved =
            PackRepository::updateContentReferences(result.path, result.manifest, pins, assets);
        if (!saved.ok)
            return ContentFileState{pack, {}, saved.error};
        auto working = pack;
        working["path"] = result.path;
        working["managed"] = true;
        working["provider"] = "conflux";
        working["sourceContent"] = QVariantList{};
        working["mods"] = pins.toVariantList();
        working["assets"] = assets.toVariantList();
        return ContentFileState{working, rows, {}};
    }));
}
void ContentInstaller::installBatch(const QVector<ContentInstallRequest> &requests) {
    if (requests.size() == 1 &&
        (requests.first().contentType != "mod" || requests.first().release.provider == "curseforge")) {
        const auto request = requests.first();
        const auto category = contentCategory(request.contentType);
        if (request.resolveDependencies && !request.release.requiredDependencies.isEmpty()) {
            finish({false, "This ZIP requires unsupported dependencies."});
            return;
        }
        const auto stage = m_stage;
        const auto path = m_pack.value("path").toString();
        const auto directory = QDir(path).filePath(category);
        if (!QDir().mkpath(directory)) {
            finish({false, "Could not create the download folder."});
            return;
        }
        m_download.start(
            request.release, directory, request.contentType,
            [this, stage, path, request, category](VerifiedDownloadResult download) {
                if (!download.error.isEmpty()) {
                    finish({false, download.error});
                    return;
                }
                auto ref = QJsonObject::fromVariantMap(request.release.toVariant());
                ref["projectTitle"] = request.title;
                ref["projectSourceIds"] = request.sourceIds;
                ref["category"] = category;
                ref["installPath"] = category + '/' + QFileInfo(download.path).fileName();
                if (!request.installedPath.isEmpty())
                    ref["confluxReplacePath"] = request.installedPath;
                auto *watcher = new QFutureWatcher<RepositoryResult>(this);
                connect(watcher, &QFutureWatcher<RepositoryResult>::finished, this, [this, watcher] {
                    auto result = watcher->result();
                    watcher->deleteLater();
                    finishBatch({result.ok, result.error});
                });
                watcher->setFuture(QtConcurrent::run([stage, path, request, ref] {
                    auto current = PackRepository::inspectContent(path);
                    if (!current.ok)
                        return current;
                    const auto key = request.contentType == "mod" ? QString("mods") : QString("assets");
                    QJsonArray refs;
                    for (const auto &v : current.manifest.value(key).toArray()) {
                        const auto old = v.toObject();
                        if (old.value("provider") != request.release.provider ||
                            old.value("projectId") != request.release.projectId)
                            refs << old;
                    }
                    refs << ref;
                    return PackRepository::updateContentReferences(
                        path, current.manifest,
                        key == "mods" ? refs : current.manifest.value("mods").toArray(),
                        key == "assets" ? refs : current.manifest.value("assets").toArray());
                }));
            });
        return;
    }
    m_installer.startBatch(m_pack.value("path").toString(), requests,
                           hasFabricBridge(m_pack.value("loader").toString(), m_rows), true);
}
void ContentInstaller::downloadDirect() {
    if (m_cancelled) {
        finish({false, "Installation cancelled."});
        return;
    }
    if (m_directIndex == m_directRequests.size()) {
        commitDirect();
        return;
    }
    const auto request = m_directRequests[m_directIndex++];
    if (request.contentType != "mod" && request.resolveDependencies && !request.dependenciesHandled &&
        !request.release.requiredDependencies.isEmpty()) {
        finish({false, "This pack requires dependencies that cannot be installed automatically."});
        return;
    }
    emit progress("Downloading " + request.title + "…");
    const auto category = contentCategory(request.contentType);
    const auto directory = m_stage->path() + '/' + category;
    if (!QDir().mkpath(directory)) {
        finish({false, "Could not create the download folder."});
        return;
    }
    QString reuse;
    for (const auto &value : m_targetRows) {
        const auto row = value.toMap();
        if (row.value("category").toString() == category &&
            row.value("sha1").toString().compare(request.release.sha1, Qt::CaseInsensitive) == 0) {
            reuse = NativePackFileStore::resolve(m_targetPack.value("path").toString(),
                                                 row.value("relativePath").toString());
            if (!reuse.isEmpty())
                break;
        }
    }
    m_download.start(
        request.release, directory, request.contentType,
        [this, request, category](VerifiedDownloadResult result) {
            if (!result.error.isEmpty()) {
                finish({false, result.error});
                return;
            }
            auto ref = QJsonObject::fromVariantMap(request.release.toVariant());
            ref["projectTitle"] = request.title;
            ref["projectSourceIds"] = request.sourceIds;
            ref["installPath"] = category + '/' + QFileInfo(result.path).fileName();
            if (!request.installedPath.isEmpty())
                ref["confluxReplacePath"] = request.installedPath;
            if (request.contentType == "mod")
                m_directMods << ref;
            else {
                ref["category"] = contentCategory(request.contentType);
                m_directAssets << ref;
            }
            downloadDirect();
        },
        reuse);
}
void ContentInstaller::commitDirect() {
    if (m_cancelled) {
        finish({false, "Installation cancelled."});
        return;
    }
    m_committing = true;
    emit progress("Applying verified content…");
    const auto stage = m_stage;
    const auto files = m_files;
    const auto pack = m_targetPack;
    const auto rows = m_targetRows;
    const QJsonObject manifest{{"mods", m_directMods}, {"assets", m_directAssets}};
    const auto shallow = m_shallowProjects;
    auto *watcher = new QFutureWatcher<ContentWriteResult>(this);
    connect(watcher, &QFutureWatcher<ContentWriteResult>::finished, this, [this, watcher] {
        auto result = watcher->result();
        watcher->deleteLater();
        finish(result);
    });
    watcher->setFuture(QtConcurrent::run([stage, files, pack, rows, manifest, shallow] {
        return files->commitStagedContent(pack, rows, stage->path(), manifest, shallow, true);
    }));
}
} // namespace studio
