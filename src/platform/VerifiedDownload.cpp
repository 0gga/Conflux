#include "VerifiedDownload.h"
#include "miniz.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QtConcurrent>
namespace studio {
void VerifiedDownload::start(const Release &release, const QString &directory, const QString &type,
                             std::function<void(VerifiedDownloadResult)> callback,
                             const QString &existingFile) {
    if (busy()) {
        callback({{}, "Another download is still finishing."});
        return;
    }
    const QUrl url(release.url);
    static const QRegularExpression sha1("^[a-fA-F0-9]{40}$"), sha512("^[a-fA-F0-9]{128}$");
    if (!sha1.match(release.sha1).hasMatch() ||
        (!release.sha512.isEmpty() && !sha512.match(release.sha512).hasMatch()) || release.fileSize < 0 ||
        release.fileSize > 512LL * 1024 * 1024 || url.host().isEmpty() ||
        (url.scheme() != "https" && !(m_local && url.scheme() == "http" && url.host() == "127.0.0.1"))) {
        callback({{}, "This content has no supported verified download."});
        return;
    }
    const auto cancelled = m_cancelled = std::make_shared<std::atomic_bool>(false);
    const QPointer<VerifiedDownload> guard(this);
    auto *watcher = new QFutureWatcher<VerifiedDownloadResult>(this);
    connect(watcher, &QFutureWatcher<VerifiedDownloadResult>::finished, this, [this, watcher, callback] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_cancelled.reset();
        callback(result);
    });
    watcher->setFuture(QtConcurrent::run([guard, release, directory, type, cancelled, url, existingFile] {
        const auto path = directory + '/' + release.sha1.toLower() + ".jar";
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly))
            return VerifiedDownloadResult{{}, "Could not stage the download."};
        QCryptographicHash hash(QCryptographicHash::Sha1), strong(QCryptographicHash::Sha512);
        qint64 count = 0;
        QString error;
        if (existingFile.isEmpty()) {
            QNetworkAccessManager network;
            QNetworkRequest request(url);
            request.setTransferTimeout(30000);
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
            request.setMaximumRedirectsAllowed(5);
            auto *reply = network.get(request);
            reply->setReadBufferSize(256 * 1024);
            QEventLoop loop;
            QTimer cancellation;
            cancellation.setInterval(25);
            QObject::connect(&cancellation, &QTimer::timeout, reply, [reply, cancelled] {
                if (*cancelled)
                    reply->abort();
            });
            cancellation.start();
            QElapsedTimer progress;
            progress.start();
            auto consume = [&] {
                while (reply->bytesAvailable()) {
                    const auto bytes = reply->read(128 * 1024);
                    if (bytes.isEmpty())
                        break;
                    count += bytes.size();
                    if (count > (release.fileSize > 0 ? release.fileSize : 512LL * 1024 * 1024) ||
                        file.write(bytes) != bytes.size()) {
                        error = "The download exceeded its expected size or could not be saved.";
                        reply->abort();
                        return;
                    }
                    hash.addData(bytes);
                    if (!release.sha512.isEmpty())
                        strong.addData(bytes);
                }
                if (guard && progress.elapsed() >= 100) {
                    progress.restart();
                    const auto text =
                        release.fileSize > 0
                            ? "Downloading · " + QString::number(count * 100 / release.fileSize) + "%"
                            : "Downloading";
                    QMetaObject::invokeMethod(
                        guard,
                        [guard, text, cancelled] {
                            if (guard && guard->m_cancelled == cancelled)
                                emit guard->progress(text);
                        },
                        Qt::QueuedConnection);
                }
            };
            QObject::connect(reply, &QNetworkReply::readyRead, &loop, consume);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            if (*cancelled)
                reply->abort();
            if (!reply->isFinished())
                loop.exec();
            consume();
            if (*cancelled)
                return VerifiedDownloadResult{{}, "Download cancelled."};
            if (!error.isEmpty())
                return VerifiedDownloadResult{{}, error};
            if (reply->error() != QNetworkReply::NoError)
                return VerifiedDownloadResult{{}, "Download failed: " + reply->errorString()};
        } else {
            QFile input(existingFile);
            if (QFileInfo(existingFile).isSymLink() || !input.open(QIODevice::ReadOnly))
                return VerifiedDownloadResult{{}, "The existing download is unavailable."};
            while (!input.atEnd()) {
                if (*cancelled)
                    return VerifiedDownloadResult{{}, "Download cancelled."};
                const auto bytes = input.read(128 * 1024);
                count += bytes.size();
                if (bytes.isEmpty() ||
                    count > (release.fileSize > 0 ? release.fileSize : 512LL * 1024 * 1024) ||
                    file.write(bytes) != bytes.size())
                    return VerifiedDownloadResult{{},
                                                  "The existing download changed or could not be staged."};
                hash.addData(bytes);
                if (!release.sha512.isEmpty())
                    strong.addData(bytes);
            }
        }
        if ((release.fileSize > 0 && count != release.fileSize) ||
            QString::fromLatin1(hash.result().toHex()) != release.sha1.toLower() ||
            (!release.sha512.isEmpty() &&
             QString::fromLatin1(strong.result().toHex()) != release.sha512.toLower()))
            return VerifiedDownloadResult{{}, "The download did not match its published checksum."};
        if (!file.commit())
            return VerifiedDownloadResult{{}, "Could not finish the verified download."};
        if (!type.isEmpty()) {
            mz_zip_archive archive{};
            const auto name = QFile::encodeName(path);
            bool valid = mz_zip_reader_init_file(&archive, name.constData(), 0) != 0;
            if (valid) {
                valid = mz_zip_reader_get_num_files(&archive) > 0 &&
                        mz_zip_reader_get_num_files(&archive) <= 100000;
                if (type == "shader") {
                    bool found = false;
                    for (mz_uint i = 0; valid && i < mz_zip_reader_get_num_files(&archive); ++i) {
                        mz_zip_archive_file_stat entry{};
                        if (mz_zip_reader_file_stat(&archive, i, &entry) &&
                            QByteArray(entry.m_filename).startsWith("shaders/")) {
                            found = true;
                            break;
                        }
                    }
                    valid = valid && found;
                } else if (type != "mod")
                    valid = valid && mz_zip_reader_locate_file(&archive, "pack.mcmeta", nullptr, 0) >= 0;
                mz_zip_reader_end(&archive);
            }
            if (!valid)
                return VerifiedDownloadResult{{}, "This download is not a valid content archive."};
        }
        if (*cancelled)
            return VerifiedDownloadResult{{}, "Download cancelled."};
        return VerifiedDownloadResult{path, {}};
    }));
}
} // namespace studio
