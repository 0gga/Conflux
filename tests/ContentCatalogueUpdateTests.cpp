#include "domain/Catalogue.h"
#include "services/ContentCatalogue.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
using namespace studio;
namespace {
struct Server : QTcpServer {
    using Handler = std::function<QPair<int, QByteArray>(QByteArray, QJsonObject)>;
    Handler handler;
    int calls = 0, largestBatch = 0;
    explicit Server(QObject *parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto bytes = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", bytes);
                    const auto end = bytes.indexOf("\r\n\r\n");
                    if (end < 0 || socket->property("answered").toBool())
                        return;
                    int length = 0;
                    for (const auto &header : bytes.left(end).split('\n'))
                        if (header.toLower().startsWith("content-length:"))
                            length = header.mid(15).trimmed().toInt();
                    if (bytes.size() < end + 4 + length)
                        return;
                    socket->setProperty("answered", true);
                    const auto body = QJsonDocument::fromJson(bytes.mid(end + 4, length)).object();
                    ++calls;
                    largestBatch = qMax(largestBatch, int(body.value("hashes").toArray().size()));
                    const auto response = handler(bytes.split(' ').value(1), body);
                    socket->write(
                        "HTTP/1.1 " + QByteArray::number(response.first) +
                        " Fixture\r\nContent-Length: " + QByteArray::number(response.second.size()) +
                        "\r\nConnection: close\r\n\r\n" + response.second);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl api() const {
        return QUrl("http://127.0.0.1:" + QString::number(serverPort()) + '/');
    }
};
QVariantMap installed(int id, const QString &category = "mods", const QStringList &loaders = {"fabric"}) {
    Release release;
    release.provider = "modrinth";
    release.projectId = "project-" + QString::number(id);
    release.id = "old-" + QString::number(id);
    release.version = "1.0";
    release.sha1 = QString::number(id + 1, 16).rightJustified(40, '0');
    release.filename = QString::number(id) + (category == "mods" ? ".jar" : ".zip");
    release.channel = "release";
    release.published = "2026-01-01T00:00:00Z";
    release.gameVersions = {"1.21.1"};
    release.loaders = loaders;
    return {{"title", release.projectId},
            {"category", category},
            {"sha1", release.sha1},
            {"relativePath", category + '/' + release.filename},
            {"installedRelease", release.toVariant()}};
}
QJsonObject candidate(int id, const QString &category = "mods", const QStringList &loaders = {"fabric"}) {
    return {
        {"id", "new-" + QString::number(id)},
        {"project_id", "project-" + QString::number(id)},
        {"version_number", "2.0"},
        {"version_type", "release"},
        {"date_published", "2026-02-01T00:00:00Z"},
        {"game_versions", QJsonArray{"1.21.1"}},
        {"loaders", QJsonArray::fromStringList(loaders)},
        {"files",
         QJsonArray{QJsonObject{
             {"primary", true},
             {"url", "https://example.invalid/fixture"},
             {"filename", QString::number(id) + (category == "mods" ? ".jar" : ".zip")},
             {"hashes", QJsonObject{{"sha1", QString::number(id + 100001, 16).rightJustified(40, '0')}}}}}}};
}
} // namespace
class ContentCatalogueUpdateTests final : public QObject {
    Q_OBJECT
  private slots:
    void largeUpdateChecksHaveBoundedProcessingCost() {
        Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        server.handler = [](QByteArray path, QJsonObject body) {
            QJsonObject response;
            if (path == "/version_files/update")
                for (const auto &hash : body.value("hashes").toArray()) {
                    const auto id = hash.toString().toInt(nullptr, 16) - 1;
                    response[hash.toString()] = candidate(id);
                }
            return qMakePair(200, QJsonDocument(response).toJson(QJsonDocument::Compact));
        };
        QVariantList rows;
        for (int i = 0; i < 5000; ++i)
            rows << installed(i);
        ContentCatalogue catalogue(nullptr, server.api(), server.api());
        catalogue.setCurseforgeKey({});
        bool done = false;
        QString error;
        QVariantList result;
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 lastHeartbeat = 0, maximumPause = 0;
        QTimer heartbeat;
        heartbeat.setInterval(5);
        connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] {
            const auto now = elapsed.elapsed();
            maximumPause = qMax(maximumPause, now - lastHeartbeat);
            lastHeartbeat = now;
        });
        heartbeat.start();
        catalogue.checkUpdates(rows, "1.21.1", "fabric", {}, [&](QVariantList updated, QString failure) {
            result = std::move(updated);
            error = failure;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
        const auto milliseconds = elapsed.elapsed();
        heartbeat.stop();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.size(), rows.size());
        QCOMPARE(server.calls, 10);
        QCOMPARE(server.largestBatch, 500);
        for (int i = 0; i < result.size(); ++i) {
            const auto row = result[i].toMap();
            QVERIFY(row.value("updateAvailable").toBool());
            QCOMPARE(row.value("installedRelease"), rows[i].toMap().value("installedRelease"));
            QCOMPARE(row.value("updateRelease").toMap().value("projectId").toString(),
                     "project-" + QString::number(i));
        }
        qInfo() << "Check5000 installed updates over10 loopback batches ms" << milliseconds;
        qInfo() << "Maximum UI event-loop heartbeat gap ms" << maximumPause;
        QVERIFY2(milliseconds < 1000, "Update result handling regressed to repeated full-inventory scans.");
        QVERIFY2(maximumPause < 150, "Provider processing blocked the UI event loop.");
    }
    void candidateFactsAndStagePolicyRemainAuthoritative_data() {
        QTest::addColumn<QString>("variation");
        QTest::addColumn<bool>("available");
        for (const auto &variation : {"compatible", "cross-compatible", "beta-allowed", "alpha-allowed"})
            QTest::newRow(variation) << QString(variation) << true;
        for (const auto &variation : {"project", "game", "loader", "beta-excluded", "alpha-excluded", "same",
                                      "older", "download", "extension", "missing"})
            QTest::newRow(variation) << QString(variation) << false;
    }
    void candidateFactsAndStagePolicyRemainAuthoritative() {
        QFETCH(QString, variation);
        QFETCH(bool, available);
        Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        auto row = installed(0);
        row["updateAvailable"] = true;
        row["updateRelease"] = QVariantMap{{"id", "stale"}};
        auto release = candidate(0);
        if (variation == "project")
            release["project_id"] = "other";
        else if (variation == "game")
            release["game_versions"] = QJsonArray{"1.21.2"};
        else if (variation == "loader")
            release["loaders"] = QJsonArray{"neoforge"};
        else if (variation == "cross-compatible") {
            release["game_versions"] = QJsonArray{"1.21", "1.21.1", "1.21.2"};
            release["loaders"] = QJsonArray{"fabric", "quilt"};
        } else if (variation.startsWith("beta"))
            release["version_type"] = "beta";
        else if (variation.startsWith("alpha"))
            release["version_type"] = "alpha";
        else if (variation == "same") {
            auto files = release.value("files").toArray();
            auto file = files.first().toObject();
            file["hashes"] = QJsonObject{{"sha1", row.value("sha1").toString()}};
            files[0] = file;
            release["files"] = files;
        } else if (variation == "older")
            release["date_published"] = "2025-01-01T00:00:00Z";
        else if (variation == "download" || variation == "extension") {
            auto files = release.value("files").toArray();
            auto file = files.first().toObject();
            file[variation == "download" ? "url" : "filename"] = variation == "download" ? "" : "wrong.zip";
            files[0] = file;
            release["files"] = files;
        }
        QStringList selection;
        if (variation.endsWith("allowed"))
            selection = {variation.section('-', 0, 0)};
        QJsonArray requestedLoaders;
        server.handler = [&](QByteArray, QJsonObject query) {
            requestedLoaders = query.value("loaders").toArray();
            QJsonObject result;
            if (variation != "missing")
                result[row.value("sha1").toString()] = release;
            return qMakePair(200, QJsonDocument(result).toJson(QJsonDocument::Compact));
        };
        ContentCatalogue catalogue(nullptr, server.api(), server.api());
        catalogue.setCurseforgeKey({});
        bool done = false;
        catalogue.checkUpdates({row}, "1.21.1", "fabric", selection, [&](QVariantList rows, QString error) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(rows.first().toMap().value("updateAvailable").toBool(), available);
            QCOMPARE(rows.first().toMap().contains("updateRelease"), available);
            QCOMPARE(rows.first().toMap().value("installedRelease"), row.value("installedRelease"));
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(requestedLoaders, QJsonArray{"fabric"});
    }
    void sharedHashesStayScopedToTheirCategoryAndInstalledProject() {
        Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        server.handler = [](QByteArray, QJsonObject query) {
            const bool resource = query.value("loaders").toArray().contains("minecraft");
            const auto release = candidate(0, resource ? "resourcepacks" : "mods",
                                           resource ? QStringList{"minecraft"} : QStringList{"fabric"});
            return qMakePair(
                200, QJsonDocument(QJsonObject{{query.value("hashes").toArray().first().toString(), release}})
                         .toJson(QJsonDocument::Compact));
        };
        auto first = installed(0), mismatch = first, resource = installed(0, "resourcepacks", {"minecraft"}),
             directory = first;
        auto wrong = mismatch.value("installedRelease").toMap();
        wrong["projectId"] = "other-project";
        mismatch["installedRelease"] = wrong;
        directory["directory"] = true;
        ContentCatalogue catalogue(nullptr, server.api(), server.api());
        catalogue.setCurseforgeKey({});
        bool done = false;
        catalogue.checkUpdates({first, mismatch, resource, first, directory}, "1.21.1", "fabric", {},
                               [&](QVariantList rows, QString error) {
                                   QVERIFY(error.isEmpty());
                                   QCOMPARE(rows.size(), 5);
                                   for (int i = 0; i < rows.size(); ++i)
                                       QCOMPARE(rows[i].toMap().value("updateAvailable").toBool(),
                                                i == 0 || i == 2 || i == 3);
                                   done = true;
                               });
        QTRY_VERIFY(done);
        QCOMPARE(server.calls, 2);
    }
    void updateCompletionCanDisposeItsCatalogue_data() {
        QTest::addColumn<QString>("kind");
        for (const auto &kind : {"success", "worker", "http-error", "rate-limit", "invalid-json"})
            QTest::newRow(kind) << QString(kind);
    }
    void updateCompletionCanDisposeItsCatalogue() {
        QFETCH(QString, kind);
        Server server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        server.handler = [&](QByteArray, QJsonObject query) {
            const int status = kind == "http-error" ? 503 : kind == "rate-limit" ? 429 : 200;
            QJsonObject result{{query.value("hashes").toArray().first().toString(), candidate(0)}};
            if (kind == "worker")
                result["unused"] = QString(256 * 1024, 'x');
            return qMakePair(status,
                             kind == "invalid-json" ? QByteArray("broken") : QJsonDocument(result).toJson());
        };
        auto catalogue = std::make_unique<ContentCatalogue>(nullptr, server.api(), server.api());
        catalogue->setCurseforgeKey({});
        const QPointer<ContentCatalogue> guard(catalogue.get());
        bool done = false;
        catalogue->checkUpdates({installed(0)}, "1.21.1", "fabric", {},
                                [&](QVariantList result, QString error) {
                                    QCOMPARE(result.size(), 1);
                                    QCOMPARE(error.isEmpty(), kind == "success" || kind == "worker");
                                    if (kind == "rate-limit")
                                        QVERIFY(error.contains("Modrinth rate limit"));
                                    catalogue.reset();
                                    done = true;
                                });
        QTRY_VERIFY(done);
        QVERIFY(!guard);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};
QTEST_GUILESS_MAIN(ContentCatalogueUpdateTests)
#include "ContentCatalogueUpdateTests.moc"
