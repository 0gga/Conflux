#pragma once
#include <QJsonDocument>
#include <QObject>
#include <functional>
class QNetworkReply;
namespace studio {
enum class JsonResponseFailure { None, Network, Http, TooLarge, InvalidJson };
struct JsonResponse {
    QJsonDocument document;
    JsonResponseFailure failure = JsonResponseFailure::None;
    QString detail;
    int status = 0;
    qsizetype bytes = 0;
};
struct JsonResponseOptions {
    qint64 maximumBytes = 32 * 1024 * 1024;
    qsizetype workerThreshold = 64 * 1024;
    bool allowMissing = false;
};
// Owns bounded reply consumption and parsing only. Callers retain request/auth/cache policy.
// Delivery is terminal: a callback may destroy the context, reader and reply.
void readJsonResponse(QNetworkReply *, QObject *context, JsonResponseOptions,
                      std::function<void(JsonResponse)>);
QString jsonResponseError(const JsonResponse &);
} // namespace studio
