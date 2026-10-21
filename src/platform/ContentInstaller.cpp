#include "ContentInstaller.h"
#include "ContentAccess.h"
#include "domain/ContentCompatibility.h"
#include "platform/PackFileStore.h"
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QtConcurrent>
#include <filesystem>
namespace studio {
ContentInstaller::ContentInstaller(std::shared_ptr<const NativeContentStore> files, QObject *parent,
                                   bool local)
    : QObject(parent), m_installer(this), m_assets(files, this, local), m_files(std::move(files)) {
    connect(&m_installer, &ModInstaller::progress, this, &ContentInstaller::progress);
    connect(&m_assets, &AssetInstaller::progress, this, &ContentInstaller::progress);
    connect(&m_installer, &ModInstaller::finished, this,
            [this](const RepositoryResult &result) { finishBatch({result.ok, result.error}); });
}
void ContentInstaller::finish(ContentWriteResult result) {
    auto callback = std::move(m_callback);
    m_callback = {};
    m_stage.reset();
    m_rows.clear();
    m_targetRows.clear();
    m_pack.clear();
    m_targetPack.clear();
    m_batches.clear();
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
    const auto current = PackRepository::inspect(m_pack.value("path").toString());
    if (!current.ok) {
        finish({false, current.error});
        return;
    }
    m_pack["mods"] = current.manifest.value("mods").toArray().toVariantList();
    m_pack["assets"] = current.manifest.value("assets").toArray().toVariantList();
    startNextBatch();
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
        const auto current = PackRepository::inspect(path);
        if (!current.ok)
            return ContentWriteResult{false, current.error};
        return files->commitStagedContent(pack, rows, path, current.manifest, shallow);
    }));
}
void ContentInstaller::install(const QVariantMap &pack, const QVariantList &rows,
                               const QVector<ContentInstallRequest> &requests, Callback callback) {
    if (m_callback) {
        callback({false, "Another content operation is in progress."});
        return;
    }
    for (const auto &request : requests)
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
                const auto installed = row.value("installedRelease").toMap().value("loaders").toStringList();
                if (!installed.contains(primary)) {
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
    QVector<ContentInstallRequest> mods;
    for (const auto &request : requests) {
        if (request.contentType == "mod")
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
            const auto source = NativePackFileStore::resolve(pack.value("path").toString(), relative),
                       destination = QDir(result.path).filePath(relative);
            if (source.isEmpty() || !QDir().mkpath(QFileInfo(destination).absolutePath()))
                return ContentFileState{pack, {}, "Could not prepare content staging."};
            std::error_code error;
#ifdef Q_OS_WIN
            std::filesystem::create_hard_link(std::filesystem::path(source.toStdWString()),
                                              std::filesystem::path(destination.toStdWString()), error);
#else
            std::filesystem::create_hard_link(std::filesystem::path(source.toStdString()),
                                              std::filesystem::path(destination.toStdString()), error);
#endif
            if (error && !QFile::copy(source, destination))
                return ContentFileState{pack, {}, "Could not stage " + relative};
            auto ref = QJsonObject::fromVariantMap(row.value("installedRelease").toMap());
            if (!ref.isEmpty()) {
                ref["installPath"] = relative;
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
    if (requests.size() == 1 && requests.first().contentType != "mod") {
        m_assets.install(m_pack, m_rows, requests.first(),
                         [this](ContentWriteResult result) { finishBatch(std::move(result)); });
        return;
    }
    m_installer.startBatch(m_pack.value("path").toString(), requests,
                           hasFabricBridge(m_pack.value("loader").toString(), m_rows));
}
} // namespace studio
