#include "GalleryImages.h"
#include "domain/Catalogue.h"
#include <QBuffer>
#include <QImageReader>
#include <QSet>
#include <memory>
namespace studio {
void GalleryImages::load(QStringList urls, int generation) {
    for (auto &reply : m_replies)
        if (reply) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    m_replies.clear();
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
    *pump = [this, batch, generation, weakPump] {
        while (batch->pending < 4 && batch->next < batch->urls.size()) {
            const int index = batch->next++;
            const QUrl url(batch->urls.at(index));
            if (url.scheme() != "https" || !url.userInfo().isEmpty()) {
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
            connect(reply, &QNetworkReply::finished, this, [this, reply, batch, index, next] {
                const auto bytes = reply->readAll();
                QBuffer buffer;
                buffer.setData(bytes);
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                const auto size = reader.size();
                if (reply->error() == QNetworkReply::NoError && bytes.size() <= 8 * 1024 * 1024 &&
                    size.isValid() && size.width() <= 4096 && size.height() <= 4096) {
                    const auto hash = decodedImageFingerprint(bytes);
                    if (!hash.isEmpty())
                        batch->hashes[index] = hash;
                    else
                        batch->failed.insert(index);
                } else
                    batch->failed.insert(index);
                --batch->pending;
                m_replies.removeAll(reply);
                reply->deleteLater();
                if (next)
                    (*next)();
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
