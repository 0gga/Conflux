#include "services/JsonResponse.h"
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThreadPool>
#include <QTimer>
#include <QtTest>
#include <algorithm>
#include <cstring>
#include <memory>
using namespace studio;
namespace {
class Reply final : public QNetworkReply {
  public:
    QByteArray payload;
    qsizetype offset = 0;
    bool aborted = false, repeat = false;
    explicit Reply(QByteArray bytes, int status, QObject *parent) : QNetworkReply(parent), payload(bytes) {
        setUrl(QUrl("https://example.invalid/fixture"));
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly);
    }
    void fail() {
        setError(QNetworkReply::ConnectionRefusedError, "Fixture connection failed");
    }
    void send() {
        const QPointer<Reply> guard(this);
        emit readyRead();
        if (!guard || guard->aborted)
            return;
        setFinished(true);
        emit finished();
        if (guard && guard->repeat)
            emit guard->finished();
    }
    void abort() override {
        aborted = true;
        setError(QNetworkReply::OperationCanceledError, "Fixture cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return payload.size() - offset + QNetworkReply::bytesAvailable();
    }

  protected:
    qint64 readData(char *data, qint64 maximum) override {
        const auto size = qMin<qint64>(maximum, payload.size() - offset);
        if (size == 0)
            return -1;
        std::memcpy(data, payload.constData() + offset, size_t(size));
        offset += size;
        return size;
    }
};
} // namespace
class JsonResponseTests final : public QObject {
    Q_OBJECT
  private slots:
    void boundedResponsesKeepOneTerminalDelivery_data() {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<int>("status");
        QTest::addColumn<int>("limit");
        QTest::addColumn<bool>("worker");
        QTest::addColumn<bool>("missing");
        QTest::addColumn<int>("failure");
        QTest::addColumn<bool>("dispose");
        for (const bool dispose : {false, true}) {
            const auto suffix = dispose ? "-dispose" : "-retain";
            const auto add = [&](const QString &name, QByteArray payload, int status, int limit, bool worker,
                                 bool missing, JsonResponseFailure failure) {
                QTest::newRow(qPrintable(name + suffix))
                    << payload << status << limit << worker << missing << int(failure) << dispose;
            };
            add("object", "{\"fixture\":true}", 200, 128, false, false, JsonResponseFailure::None);
            add("array-worker", "[1,2,3]", 200, 128, true, false, JsonResponseFailure::None);
            add("invalid-worker", "[broken", 200, 128, true, false, JsonResponseFailure::InvalidJson);
            add("empty", {}, 200, 128, false, false, JsonResponseFailure::InvalidJson);
            add("exact-limit", "[1234]", 200, 6, false, false, JsonResponseFailure::None);
            add("over-limit", "[12345]", 200, 6, false, false, JsonResponseFailure::TooLarge);
            add("optional-missing", "not JSON", 404, 128, false, true, JsonResponseFailure::None);
            add("required-missing", "not JSON", 404, 128, false, false, JsonResponseFailure::Http);
            add("rate-limit", "not JSON", 429, 128, false, false, JsonResponseFailure::Http);
            add("redirect", "not JSON", 302, 128, false, false, JsonResponseFailure::Http);
            add("server-error", "not JSON", 503, 128, false, false, JsonResponseFailure::Http);
        }
    }
    void boundedResponsesKeepOneTerminalDelivery() {
        QFETCH(QByteArray, payload);
        QFETCH(int, status);
        QFETCH(int, limit);
        QFETCH(bool, worker);
        QFETCH(bool, missing);
        QFETCH(int, failure);
        QFETCH(bool, dispose);
        auto context = std::make_unique<QObject>();
        auto *reply = new Reply(payload, status, context.get());
        reply->repeat = true;
        JsonResponse result;
        int deliveries = 0;
        bool observedWorker = false;
        readJsonResponse(
            reply, context.get(),
            {.maximumBytes = limit, .workerThreshold = worker ? 0 : 1024, .allowMissing = missing},
            [&](JsonResponse response) {
                ++deliveries;
                result = std::move(response);
                if (dispose)
                    context.reset();
            });
        connect(reply, &QNetworkReply::finished, context.get(),
                [&] { observedWorker = !context->findChildren<QFutureWatcherBase *>().isEmpty(); });
        reply->send();
        QTRY_COMPARE(deliveries, 1);
        QCOMPARE(int(result.failure), failure);
        QCOMPARE(result.status, status);
        if (worker)
            QVERIFY(observedWorker);
        if (result.failure == JsonResponseFailure::None && status == 200) {
            QVERIFY(!result.document.isNull());
            QCOMPARE(result.bytes, payload.size());
            QVERIFY(jsonResponseError(result).isEmpty());
        } else if (result.failure != JsonResponseFailure::None) {
            QVERIFY(result.document.isNull());
            QVERIFY(!jsonResponseError(result).isEmpty());
        }
        QCOMPARE(!context, dispose);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(deliveries, 1);
    }
    void contextDisposalCancelsWorkerDelivery() {
        auto context = std::make_unique<QObject>();
        auto *reply = new Reply("[\"" + QByteArray(512 * 1024, 'x') + "\"]", 200, context.get());
        int deliveries = 0;
        readJsonResponse(reply, context.get(), {.workerThreshold = 0}, [&](JsonResponse) { ++deliveries; });
        connect(reply, &QNetworkReply::finished, context.get(), [&] {
            QVERIFY(!context->findChildren<QFutureWatcherBase *>().isEmpty());
            context.reset();
        });
        reply->send();
        QVERIFY(!context);
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::processEvents();
        QCOMPARE(deliveries, 0);
    }
    void networkFailureRetainsItsDiagnosticAndNeverParses() {
        QObject context;
        auto *reply = new Reply("{\"ignored\":true}", 200, &context);
        reply->fail();
        bool done = false;
        readJsonResponse(reply, &context, {}, [&](JsonResponse result) {
            QCOMPARE(result.failure, JsonResponseFailure::Network);
            QCOMPARE(jsonResponseError(result), "Fixture connection failed");
            QVERIFY(result.document.isNull());
            done = true;
        });
        reply->send();
        QVERIFY(done);
    }
    void realHttpResponsesRespectBufferLimit_data() {
        QTest::addColumn<int>("bytes");
        QTest::newRow("under-limit") << 1023;
        QTest::newRow("exact-limit") << 1024;
        QTest::newRow("over-limit") << 1025;
        QTest::newRow("large-over-limit") << 64 * 1024;
    }
    void realHttpResponsesRespectBufferLimit() {
        QFETCH(int, bytes);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto payload = "[\"" + QByteArray(bytes - 4, 'x') + "\"]";
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, payload] {
                auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("answered").toBool())
                    return;
                socket->setProperty("answered", true);
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(payload.size()) +
                              "\r\nConnection: close\r\n\r\n" + payload);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        QObject context;
        QNetworkAccessManager network(&context);
        auto *reply = network.get(
            QNetworkRequest(QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) + "/fixture")));
        int deliveries = 0;
        readJsonResponse(reply, &context, {.maximumBytes = 1024}, [&](JsonResponse response) {
            ++deliveries;
            QCOMPARE(response.failure,
                     bytes > 1024 ? JsonResponseFailure::TooLarge : JsonResponseFailure::None);
            QCOMPARE(response.document.isNull(), bytes > 1024);
        });
        QCOMPARE(reply->readBufferSize(), 1025);
        QTRY_COMPARE(deliveries, 1);
    }
};
QTEST_GUILESS_MAIN(JsonResponseTests)
#include "JsonResponseTests.moc"
