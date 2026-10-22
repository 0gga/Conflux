#include "AssetInstaller.h"
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QtConcurrent>
namespace studio {
AssetInstaller::AssetInstaller(std::shared_ptr<const NativeContentStore> files, QObject *parent, bool local)
    : QObject(parent), m_download(this, local), m_files(std::move(files)) {
    connect(&m_download, &VerifiedDownload::progress, this, &AssetInstaller::progress);
}
void AssetInstaller::cancel() {
    m_download.cancel();
}
void AssetInstaller::install(const QVariantMap &pack, const QVariantList &rows,
                             const ContentInstallRequest &request, IContentInstaller::Callback callback) {
    if (m_active) {
        callback({false, "Another content download is in progress."});
        return;
    }
    if (!request.release.filename.endsWith(".zip", Qt::CaseInsensitive) || request.release.fileSize <= 0 ||
        !request.release.gameVersions.contains(pack.value("minecraft").toString())) {
        callback({false, "Choose a verified ZIP compatible with this instance."});
        return;
    }
    if (request.resolveDependencies && !request.release.requiredDependencies.isEmpty()) {
        callback({false, "This ZIP requires dependencies that cannot be installed automatically."});
        return;
    }
    auto stage = std::make_shared<QTemporaryDir>();
    if (!stage->isValid()) {
        callback({false, "Could not create the download workspace."});
        return;
    }
    m_active = true;
    emit progress("Downloading " + request.title + "…");
    m_download.start(request.release, stage->path(), request.contentType,
                     [this, stage, pack, rows, request, callback](VerifiedDownloadResult download) {
                         if (!download.error.isEmpty()) {
                             m_active = false;
                             callback({false, download.error});
                             return;
                         }
                         emit progress("Applying verified content…");
                         const auto files = m_files;
                         auto *watcher = new QFutureWatcher<ContentWriteResult>(this);
                         connect(watcher, &QFutureWatcher<ContentWriteResult>::finished, this,
                                 [this, watcher, callback] {
                                     const auto result = watcher->result();
                                     watcher->deleteLater();
                                     m_active = false;
                                     callback(result);
                                 });
                         watcher->setFuture(QtConcurrent::run([files, stage, pack, rows, request, download] {
                             return files->installAsset(pack, rows, request, download.path);
                         }));
                     });
}
} // namespace studio
