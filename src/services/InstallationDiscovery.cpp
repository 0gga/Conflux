#include "InstallationDiscovery.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSet>
#include <QUrl>
#include <QUuid>

namespace studio {
static QVariantMap inspectFolder(const QString& path, const QString& source, const QString& name = {}, const QString& icon = {}) {
    const QDir dir(path);
    QString title = name.isEmpty() ? dir.dirName() : name, game = "", loader = "";
    QFile metadata(dir.filePath("minecraftinstance.json"));
    if (metadata.open(QIODevice::ReadOnly)) {
        const auto json = QJsonDocument::fromJson(metadata.readAll()).object();
        if (name.isEmpty()) title = json.value("name").toString(title);
        game = json.value("gameVersion").toString();
        loader = json.value("baseModLoader").toObject().value("name").toString();
    }
    const auto count = QDir(dir.filePath("mods")).entryList({"*.jar", "*.jar.disabled"}, QDir::Files).size();
    return {{"title", title}, {"path", dir.absolutePath()}, {"provider", source},
        {"gameVersion", game}, {"loader", loader}, {"modCount", count},
        {"icon", QFile::exists(icon) ? QUrl::fromLocalFile(icon).toString() : ""}};
}
DiscoveryResult InstallationDiscovery::scan(const QString& mrRoot, const QString& cfRoot) {
    DiscoveryResult result;
    QSet<QString> paths;
    auto append = [&](const QVariantMap& row) {
        auto path = QFileInfo(row.value("path").toString()).canonicalFilePath();
        if (path.isEmpty() || paths.contains(path)) return;
        paths.insert(path); result.installations.append(row);
    };
    const auto dbPath = QDir(mrRoot).filePath("app.db");
    if (QFile::exists(dbPath)) {
        const auto connection = QUuid::createUuid().toString();
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(dbPath);
            db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000");
            if (db.open()) {
                // Explicit public profile fields only; never query account or token tables.
                QSqlQuery query(db);
                bool valid = query.exec("SELECT i.name, i.path, i.icon_path, c.game_version, c.loader, c.loader_version FROM instances i LEFT JOIN instance_content_sets c ON c.id = i.applied_content_set_id");
                if (!valid) valid = query.exec("SELECT name, path, icon_path FROM instances");
                if (!valid) valid = query.exec("SELECT name, path, icon_path FROM profiles");
                if (valid) {
                    while (query.next()) {
                        auto path = query.value(1).toString();
                        if (QDir::isRelativePath(path)) path = QDir(mrRoot).filePath("profiles/" + path);
                        auto row = inspectFolder(path, "modrinth", query.value(0).toString(), query.value(2).toString());
                        if (query.record().count() > 3) {
                            if (!query.value(3).toString().isEmpty()) row["gameVersion"] = query.value(3).toString();
                            if (!query.value(4).toString().isEmpty()) row["loader"] = query.value(4).toString();
                            row["loaderVersion"] = query.value(5).toString();
                        }
                        append(row);
                    }
                } else result.warnings << "Modrinth profile database format was not recognized; scanning profile folders.";
                db.close();
            } else result.warnings << "Could not read the Modrinth profile database; scanning profile folders.";
        }
        QSqlDatabase::removeDatabase(connection);
    }
    for (const auto& item : QDir(QDir(mrRoot).filePath("profiles")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        append(inspectFolder(item.absoluteFilePath(), "modrinth"));
    for (const auto& item : QDir(cfRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFile::exists(QDir(item.absoluteFilePath()).filePath("minecraftinstance.json")))
            append(inspectFolder(item.absoluteFilePath(), "curseforge"));
    }
    return result;
}
}
