#pragma once
#include "domain/Catalogue.h"
#include "miniz.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
namespace dependency_test {
inline QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}
inline QByteArray archive(const QString &path, const QString &id) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, QFile::encodeName(path).constData(), 0))
        return {};
    const auto data = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"id", id}, {"version", "1"}}).toJson();
    const bool added = mz_zip_writer_add_mem(&zip, "fabric.mod.json", data.constData(), data.size(), 0);
    const bool saved = mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    QFile file(path);
    return added && saved && file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
class Server final : public QTcpServer {
  public:
    QHash<QString, QByteArray> bodies;
    QHash<QString, int> statuses;
    QStringList requests;
    QHash<QString, QList<QByteArray>> payloads;
    QHash<QString, QByteArray> requestHeaders;
    std::function<void(const QString &)> requested;
    Server() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const auto input = socket->property("input").toByteArray() + socket->readAll();
                    socket->setProperty("input", input);
                    if (!input.contains("\r\n\r\n") || socket->property("answered").toBool())
                        return;
                    const int headerEnd = input.indexOf("\r\n\r\n") + 4;
                    int length = 0;
                    for (const auto &line : input.left(headerEnd).split('\n'))
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(15).trimmed().toInt();
                    if (input.size() < headerEnd + length)
                        return;
                    socket->setProperty("answered", true);
                    const auto target = QString::fromUtf8(input.split(' ').value(1));
                    requests << target;
                    const auto path = QUrl(target).path();
                    payloads[path] << input.mid(headerEnd, length);
                    requestHeaders[path] = input.left(headerEnd);
                    if (requested)
                        requested(path);
                    const auto body = bodies.value(path, "not found");
                    const auto status = statuses.value(path, bodies.contains(path) ? 200 : 404);
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: " +
                                  QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl base() const {
        return QUrl("http://127.0.0.1:" + QString::number(serverPort()) + '/');
    }
    void json(const QString &path, QJsonValue value) {
        bodies[path] = value.isArray() ? QJsonDocument(value.toArray()).toJson()
                                       : QJsonDocument(value.toObject()).toJson();
    }
    QJsonObject modrinth(const QString &project, const QString &id, const QByteArray &bytes,
                         const QJsonArray &dependencies = {}) {
        QJsonObject value{
            {"project_id", project},
            {"id", id},
            {"name", project},
            {"version_number", id},
            {"game_versions", QJsonArray{"1.21.1"}},
            {"loaders", QJsonArray{"fabric"}},
            {"version_type", "release"},
            {"date_published", "2026-10-24T10:00:00Z"},
            {"dependencies", dependencies},
            {"files", QJsonArray{QJsonObject{{"filename", id + ".jar"},
                                             {"primary", true},
                                             {"size", bytes.size()},
                                             {"url", base().resolved(QUrl("files/" + id)).toString()},
                                             {"hashes", QJsonObject{{"sha1", hash(bytes)}}}}}}};
        json("/version/" + id, value);
        json("/project/" + project + "/version", QJsonArray{value});
        bodies["/files/" + id] = bytes;
        return value;
    }
    QJsonObject curseforge(int project, int id, const QByteArray &bytes,
                           const QJsonArray &dependencies = {}) {
        QJsonObject value{{"modId", project},
                          {"id", id},
                          {"displayName", QString::number(id)},
                          {"fileName", QString::number(id) + ".jar"},
                          {"gameVersions", QJsonArray{"1.21.1", "Fabric"}},
                          {"releaseType", 1},
                          {"isAvailable", true},
                          {"fileDate", "2026-10-24T10:00:00Z"},
                          {"fileLength", bytes.size()},
                          {"downloadUrl", base().resolved(QUrl("files/" + QString::number(id))).toString()},
                          {"hashes", QJsonArray{QJsonObject{{"algo", 1}, {"value", hash(bytes)}}}},
                          {"dependencies", dependencies}};
        const auto path = "/mods/" + QString::number(project);
        json(path, QJsonObject{{"data", QJsonObject{{"id", project},
                                                    {"gameId", 432},
                                                    {"name", QString::number(project)},
                                                    {"allowModDistribution", true}}}});
        json(path + "/files/" + QString::number(id), QJsonObject{{"data", value}});
        json(path + "/files", QJsonObject{{"data", QJsonArray{value}}});
        bodies["/files/" + QString::number(id)] = bytes;
        return value;
    }
};
inline QJsonObject required(const QString &project, const QString &version = {}) {
    return {{"dependency_type", "required"}, {"project_id", project}, {"version_id", version}};
}
inline studio::ContentInstallRequest request(studio::Release release) {
    studio::ContentInstallRequest value;
    value.release = std::move(release);
    value.title = value.release.projectId;
    value.sourceIds = {{value.release.provider, value.release.projectId}};
    return value;
}
} // namespace dependency_test
