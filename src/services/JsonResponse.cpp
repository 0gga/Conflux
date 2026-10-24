#include "JsonResponse.h"
#include <QFutureWatcher>
#include <QNetworkReply>
#include <QtConcurrent>
#include <algorithm>
#include <memory>
#include <utility>
namespace studio {
QString jsonResponseError(const JsonResponse &response) {
    if (response.failure == JsonResponseFailure::None)
        return {};
    if (response.failure == JsonResponseFailure::TooLarge)
        return "The provider response exceeded its size limit.";
    if (response.status == 429)
        return "Rate limit reached. Wait a moment before searching again.";
    if (response.failure == JsonResponseFailure::InvalidJson)
        return "The provider returned invalid JSON.";
    if (response.failure == JsonResponseFailure::Network)
        return response.detail;
    return "Provider request failed (HTTP " + QString::number(response.status) + ").";
}
void readJsonResponse(QNetworkReply *reply, QObject *context, JsonResponseOptions options,
                      std::function<void(JsonResponse)> callback) {
    struct State {
        bool oversized = false, finished = false;
    };
    const auto state = std::make_shared<State>();
    const auto limit = std::clamp<qint64>(options.maximumBytes, 1, 32 * 1024 * 1024);
    reply->setReadBufferSize(limit + 1);
    QObject::connect(reply, &QNetworkReply::readyRead, context, [reply, state, limit] {
        if (!state->finished && reply->bytesAvailable() > limit) {
            state->oversized = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, context,
                     [reply, context, state, limit, options, callback = std::move(callback)]() mutable {
                         if (std::exchange(state->finished, true))
                             return;
                         JsonResponse response;
                         response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                         if (state->oversized || reply->bytesAvailable() > limit)
                             response.failure = JsonResponseFailure::TooLarge;
                         else if (options.allowMissing && response.status == 404) {
                             reply->deleteLater();
                             callback(std::move(response));
                             return;
                         } else if (reply->error() != QNetworkReply::NoError) {
                             response.failure = JsonResponseFailure::Network;
                             response.detail = reply->errorString();
                         } else if (response.status != 200)
                             response.failure = JsonResponseFailure::Http;
                         if (response.failure != JsonResponseFailure::None) {
                             reply->deleteLater();
                             callback(std::move(response));
                             return;
                         }
                         const auto bytes = reply->readAll();
                         response.bytes = bytes.size();
                         // The reply must be retired before any callback can dispose its owner.
                         reply->deleteLater();
                         auto parse = [bytes, response = std::move(response)]() mutable {
                             QJsonParseError error;
                             response.document = QJsonDocument::fromJson(bytes, &error);
                             if (error.error != QJsonParseError::NoError || response.document.isNull())
                                 response.failure = JsonResponseFailure::InvalidJson;
                             return response;
                         };
                         if (bytes.size() >= options.workerThreshold) {
                             auto *watcher = new QFutureWatcher<JsonResponse>(context);
                             QObject::connect(watcher, &QFutureWatcherBase::finished, context,
                                              [watcher, callback = std::move(callback)]() mutable {
                                                  auto result = watcher->result();
                                                  watcher->deleteLater();
                                                  callback(std::move(result));
                                              });
                             watcher->setFuture(QtConcurrent::run(std::move(parse)));
                         } else
                             callback(parse());
                     });
}
} // namespace studio
