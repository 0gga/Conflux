#include "GalleryImages.h"
#include "domain/Catalogue.h"
#include <QBuffer>
#include <QFutureWatcher>
#include <QImageReader>
#include <QSet>
#include <QtConcurrent>
#include <memory>
namespace studio {
void GalleryImages::cancel() {
    ++m_epoch;
    for (auto &reply : m_replies)
        if (reply) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    m_replies.clear();
}
void GalleryImages::load(QStringList urls, int generation) {
    cancel();
    const int epoch = m_epoch;
    urls.removeDuplicates();
    struct Batch {
        QStringList urls, errors;
        QMap<int, QByteArray> hashes;
        QSet<int> failed;
        int next = 0, pending = 0;
    };
    auto batch = std::make_shared<Batch>();
    if (urls.size() > 40)
        batch->errors << "Gallery is limited to the first 40 images.";
    batch->urls = urls.mid(0, 40);
    if (batch->urls.isEmpty()) {
        emit ready(generation, {}, {});
        return;
    }
    auto pump = std::make_shared<std::function<void()>>();
    // A weak reference prevents a self-owning callback cycle after the batch ends.
    const std::weak_ptr<std::function<void()>> weakPump = pump;
    *pump = [this, batch, generation, epoch, weakPump] {
        if (epoch != m_epoch)
            return;
        while (batch->pending < 4 && batch->next < batch->urls.size()) {
            const int index = batch->next++;
            const auto address = batch->urls.at(index);
            if (const auto *cached = m_cache.object(address); cached && cached->expires > m_clock.elapsed()) {
                batch->hashes[index] = cached->hash;
                continue;
            }
            const QUrl url(address);
            if ((url.scheme() != "https" &&
                 !(m_allowLoopback && url.scheme() == "http" && url.host() == "127.0.0.1")) ||
                !url.userInfo().isEmpty()) {
                batch->failed.insert(index);
                continue;
            }
            QNetworkRequest request(url);
            request.setHeader(QNetworkRequest::UserAgentHeader,
                              "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
            request.setTransferTimeout(12000);
            auto *reply = m_network.get(request);
            m_replies << reply;
            ++batch->pending;
            connect(reply, &QNetworkReply::readyRead, this, [reply] {
                if (reply->bytesAvailable() > 8 * 1024 * 1024)
                    reply->abort();
            });
            auto next = weakPump.lock();
            connect(reply, &QNetworkReply::finished, this, [this, reply, batch, index, next, epoch, address] {
                const auto bytes = reply->readAll();
                const bool valid =
                    reply->error() == QNetworkReply::NoError && bytes.size() <= 8 * 1024 * 1024;
                m_replies.removeAll(reply);
                reply->deleteLater();
                auto *watcher = new QFutureWatcher<QByteArray>(this);
                connect(watcher, &QFutureWatcherBase::finished, this,
                        [this, watcher, batch, index, next, epoch, address] {
                            const auto hash = watcher->result();
                            watcher->deleteLater();
                            if (epoch != m_epoch)
                                return;
                            if (!hash.isEmpty()) {
                                batch->hashes[index] = hash;
                                m_cache.insert(address, new Fingerprint{hash, m_clock.elapsed() + 300000});
                            } else
                                batch->failed.insert(index);
                            --batch->pending;
                            if (next)
                                (*next)();
                        });
                watcher->setFuture(QtConcurrent::run([bytes, valid]() -> QByteArray {
                    if (!valid)
                        return {};
                    QBuffer buffer;
                    buffer.setData(bytes);
                    buffer.open(QIODevice::ReadOnly);
                    QImageReader reader(&buffer);
                    const auto size = reader.size();
                    if (!size.isValid() || size.width() > 4096 || size.height() > 4096)
                        return {};
                    return decodedImageFingerprint(bytes);
                }));
            });
        }
        if (batch->pending || batch->next < batch->urls.size())
            return;
        QStringList result;
        QSet<QByteArray> seen;
        for (int i = 0; i < batch->urls.size(); ++i) {
            const auto hash = batch->hashes.value(i);
            if (hash.isEmpty())
                continue;
            if (!seen.contains(hash)) {
                result << batch->urls.at(i);
                seen.insert(hash);
            }
        }
        if (!batch->failed.isEmpty())
            batch->errors << QString::number(batch->failed.size()) +
                                 " gallery images could not be safely loaded.";
        emit ready(generation, result, batch->errors);
    };
    (*pump)();
}
} // namespace studio
