#include "ContentInstaller.h"
#include "platform/PackFileStore.h"
#include "services/PackContent.h"
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QtConcurrent>
#include <filesystem>
namespace studio {
ContentInstaller::ContentInstaller(std::shared_ptr<const NativeContentStore> files, QObject *parent)
    : QObject(parent), m_installer(this), m_files(std::move(files)) {
    connect(&m_installer, &ModInstaller::progress, this, &ContentInstaller::progress);
    connect(&m_installer, &ModInstaller::finished, this, [this](const RepositoryResult &result) {
        if (!result.ok) {
            finish({false, result.error});
            return;
        }
        if (!m_stage) {
            finish({true, result.error});
            return;
        }
        emit progress("Applying the backed-up content transaction…");
        const auto pack = m_pack;
        const auto rows = m_rows;
        const auto stage = m_stage;
        const auto files = m_files;
        auto *watcher = new QFutureWatcher<ContentWriteResult>(this);
        connect(watcher, &QFutureWatcher<ContentWriteResult>::finished, this, [this, watcher] {
            const auto result = watcher->result();
            watcher->deleteLater();
            finish(result);
        });
        watcher->setFuture(QtConcurrent::run([pack, rows, stage, files, result] {
            QSet<QString> disabledProjects, disabledVersions;
            for (const auto &value : rows) {
                const auto row = value.toMap();
                if (row.value("enabled").toBool())
                    continue;
                const auto id = row.value("sourceIds").toMap().value("modrinth").toString();
                if (!id.isEmpty())
                    disabledProjects.insert(id);
                const auto version = row.value("installedRelease").toMap().value("id").toString();
                if (!version.isEmpty())
                    disabledVersions.insert(version);
            }
            for (const auto &value : result.manifest.value("mods").toArray()) {
                const auto ref = value.toObject();
                if (disabledProjects.contains(ref.value("projectId").toString()))
                    continue;
                for (const auto &dependency : ref.value("dependencyRefs").toArray()) {
                    const auto dep = dependency.toObject();
                    if (dep.value("dependency_type") == "required" &&
                        (disabledProjects.contains(dep.value("project_id").toString()) ||
                         disabledVersions.contains(dep.value("version_id").toString())))
                        return ContentWriteResult{false,
                                                  "A required dependency is disabled. Enable it before "
                                                  "installing this update. No installed files were changed."};
                }
            }
            // The staging inventory includes unrecognized local files too, so a new
            // dependency cannot silently introduce a second enabled mod ID.
            const auto staged = PackContent::scan({{"path", result.path}, {"loader", pack.value("loader")}});
            QMap<QString, int> before, after;
            for (const auto &value : rows) {
                const auto row = value.toMap();
                if (row.value("enabled").toBool() && !row.value("modId").toString().isEmpty())
                    ++before[row.value("modId").toString()];
            }
            for (const auto &value : staged.files) {
                const auto row = value.toMap();
                if (row.value("enabled").toBool() && !row.value("modId").toString().isEmpty())
                    ++after[row.value("modId").toString()];
            }
            for (auto it = after.begin(); it != after.end(); ++it)
                if (it.value() > qMax(1, before.value(it.key())))
                    return ContentWriteResult{false, "This install would add a duplicate enabled mod ID: " +
                                                         it.key() + ". No installed files were changed."};
            return files->applyStaged(pack, rows, result.path, result.manifest);
        }));
    });
}
void ContentInstaller::finish(ContentWriteResult result) {
    auto callback = std::move(m_callback);
    m_callback = {};
    m_stage.reset();
    m_rows.clear();
    m_pack.clear();
    m_preparing = false;
    if (callback)
        callback(std::move(result));
}
void ContentInstaller::install(const QVariantMap &pack, const QVariantList &rows,
                               const QVector<ContentInstallRequest> &requests, Callback callback) {
    if (m_callback || m_installer.busy() || m_preparing) {
        callback({false, "Another content operation is in progress."});
        return;
    }
    m_pack = pack;
    m_rows = rows;
    m_callback = std::move(callback);
    m_cancelled = false;
    bool untracked = false;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        if (row.value("category") != "mods")
            continue;
        bool tracked = false;
        for (const auto &pin : pack.value("mods").toList())
            if (pin.toMap().value("installPath") == row.value("relativePath"))
                tracked = true;
        if (!tracked)
            untracked = true;
    }
    if (pack.value("managed").toBool() && !untracked) {
        m_installer.startBatch(pack.value("path").toString(), requests);
        return;
    }
    m_stage = std::make_shared<QTemporaryDir>();
    if (!m_stage->isValid()) {
        finish({false, "Could not create a temporary install workspace."});
        return;
    }
    m_preparing = true;
    emit progress("Preparing an isolated dependency transaction…");
    const auto stage = m_stage;
    auto *watcher = new QFutureWatcher<RepositoryResult>(this);
    connect(watcher, &QFutureWatcher<RepositoryResult>::finished, this, [this, watcher, requests] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_preparing = false;
        if (m_cancelled) {
            finish({false, "Installation cancelled before changing installed files."});
            return;
        }
        if (!result.ok) {
            finish({false, result.error});
            return;
        }
        m_installer.startBatch(result.path, requests);
    });
    watcher->setFuture(QtConcurrent::run([pack, rows, stage] {
        auto result =
            PackRepository::create(stage->path(), "Content staging", pack.value("minecraft").toString(),
                                   pack.value("loader").toString());
        if (!result.ok)
            return result;
        QDir().mkpath(result.path + "/mods");
        QJsonArray pins;
        for (const auto &value : rows) {
            const auto row = value.toMap();
            if (row.value("category") != "mods")
                continue;
            const auto relative = row.value("relativePath").toString();
            const auto source = NativePackFileStore::resolve(pack.value("path").toString(), relative);
            if (source.isEmpty())
                return RepositoryResult{false, "An installed file moved while preparing the update."};
            auto ref = QJsonObject::fromVariantMap(row.value("installedRelease").toMap());
            QString target = relative;
            if (!ref.isEmpty() && row.value("enabled").toBool()) {
                target = "mods/" + row.value("sha1").toString() + ".jar";
                ref["installPath"] = target;
                ref["projectTitle"] = row.value("title").toString();
                ref["projectSourceIds"] = QJsonObject::fromVariantMap(row.value("sourceIds").toMap());
                pins << ref;
            }
            const auto destination = QDir(result.path).filePath(target);
            std::error_code error;
#ifdef Q_OS_WIN
            std::filesystem::create_hard_link(std::filesystem::path(source.toStdWString()),
                                              std::filesystem::path(destination.toStdWString()), error);
#else
            std::filesystem::create_hard_link(std::filesystem::path(source.toStdString()),
                                              std::filesystem::path(destination.toStdString()), error);
#endif
            if (error && !QFile::copy(source, destination))
                return RepositoryResult{false, "Could not prepare an isolated copy of " + relative};
        }
        return PackRepository::updateContentPins(result.path, result.manifest, pins);
    }));
}
} // namespace studio
