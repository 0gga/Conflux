#include "AssetInstaller.h"
#include "miniz.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtConcurrent>
namespace studio {
AssetInstaller::AssetInstaller(std::shared_ptr<const NativeContentStore> files, QObject *parent, bool local)
    : QObject(parent), m_network(this), m_files(std::move(files)), m_allowLocal(local) {}
void AssetInstaller::cancel() {
    if (m_reply)
        m_reply->abort();
}
void AssetInstaller::install(const QVariantMap &pack, const QVariantList &rows,
                             const ContentInstallRequest &request, IContentInstaller::Callback callback) {
    const auto &release = request.release;
    const QUrl url(release.url);
    if (m_active) {
        callback({false, "Another content download is in progress."});
        return;
    }
    if (release.provider != "modrinth" || !release.filename.endsWith(".zip", Qt::CaseInsensitive) ||
        release.sha1.size() != 40 || release.fileSize <= 0 || release.fileSize > 512LL * 1024 * 1024 ||
        (url.scheme() != "https" && !(m_allowLocal && url.scheme() == "http" && url.host() == "127.0.0.1"))) {
        callback({false, "This pack has no supported verified Modrinth download."});
        return;
    }
    if (pack.value("minecraft").toString().isEmpty() ||
        !release.gameVersions.contains(pack.value("minecraft").toString())) {
        callback({false, "Choose a version compatible with this instance's Minecraft version."});
        return;
    }
    if (!release.requiredDependencies.isEmpty()) {
        callback({false, "This ZIP pack declares required dependencies. Dependency planning for ZIP packs is "
                         "not available yet; review its project page before installing."});
        return;
    }
    auto stage = std::make_shared<QTemporaryDir>();
    auto file = std::make_shared<QSaveFile>(stage->path() + "/download.zip");
    if (!stage->isValid() || !file->open(QIODevice::WriteOnly)) {
        callback({false, "Could not create a temporary pack download."});
        return;
    }
    m_active = true;
    QNetworkRequest http(url);
    http.setHeader(QNetworkRequest::UserAgentHeader, "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
    http.setTransferTimeout(30000);
    http.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_network.get(http);
    m_reply = reply;
    auto hash1 = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha1);
    auto hash512 = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha512);
    auto count = std::make_shared<qint64>(0);
    auto consume = [reply, file, hash1, hash512, count, limit = release.fileSize] {
        const auto bytes = reply->readAll();
        *count += bytes.size();
        if (*count > limit || file->write(bytes) != bytes.size()) {
            reply->abort();
            return;
        }
        hash1->addData(bytes);
        hash512->addData(bytes);
    };
    connect(reply, &QNetworkReply::readyRead, this, consume);
    emit progress("Downloading " + request.title + "…");
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, stage, consume, hash1, hash512, count, pack, rows, request,
             callback = std::move(callback)]() mutable {
                consume();
                m_reply.clear();
                const auto &release = request.release;
                const bool verified = reply->error() == QNetworkReply::NoError &&
                                      *count == release.fileSize &&
                                      QString::fromLatin1(hash1->result().toHex()) == release.sha1 &&
                                      (release.sha512.isEmpty() ||
                                       QString::fromLatin1(hash512->result().toHex()) == release.sha512);
                const auto error = reply->error() == QNetworkReply::OperationCanceledError
                                       ? "Pack download cancelled."
                                       : reply->errorString();
                reply->deleteLater();
                if (!verified || !file->commit()) {
                    m_active = false;
                    callback({false, verified ? "Could not stage the pack."
                                              : "Pack download failed verification: " + error});
                    return;
                }
                mz_zip_archive archive{};
                const auto path = file->fileName().toUtf8();
                bool valid = mz_zip_reader_init_file(&archive, path.constData(), 0) != 0;
                if (valid) {
                    valid = mz_zip_reader_get_num_files(&archive) <= 100000;
                    if (request.contentType == "shader") {
                        bool shaders = false;
                        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&archive) && i < 100000; ++i) {
                            mz_zip_archive_file_stat entry{};
                            if (mz_zip_reader_file_stat(&archive, i, &entry) &&
                                QByteArray(entry.m_filename).startsWith("shaders/")) {
                                shaders = true;
                                break;
                            }
                        }
                        valid = valid && shaders;
                    } else
                        valid = valid && mz_zip_reader_locate_file(&archive, "pack.mcmeta", nullptr, 0) >= 0;
                    mz_zip_reader_end(&archive);
                }
                if (!valid) {
                    m_active = false;
                    callback({false, "This ZIP does not contain a valid root pack structure."});
                    return;
                }
                emit progress("Applying the verified pack…");
                const auto files = m_files;
                auto *watcher = new QFutureWatcher<ContentWriteResult>(this);
                connect(watcher, &QFutureWatcher<ContentWriteResult>::finished, this,
                        [this, watcher, callback = std::move(callback)] {
                            const auto result = watcher->result();
                            watcher->deleteLater();
                            m_active = false;
                            callback(result);
                        });
                watcher->setFuture(
                    QtConcurrent::run([files, pack, rows, request, stage, path = file->fileName()] {
                        return files->installAsset(pack, rows, request, path);
                    }));
            });
}
} // namespace studio
