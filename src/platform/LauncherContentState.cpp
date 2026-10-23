#include "LauncherContentState.h"
#include "CurseforgeContentState.h"
#include "PackFileStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
namespace studio {
QString LauncherContentState::databasePath(const QVariantMap &pack) {
    const auto profile = QDir(pack.value("path").toString());
    QDir root(profile);
    int ancestors = 0;
    while (root.dirName().compare("profiles", Qt::CaseInsensitive) != 0 && ancestors++ < 16)
        if (!root.cdUp())
            return {};
    if (root.dirName().compare("profiles", Qt::CaseInsensitive) != 0 || !root.cdUp())
        return {};
    return NativePackFileStore::resolve(root.absolutePath(), "app.db");
}
QVariantList LauncherContentState::read(const QVariantMap &pack, QString *error) {
    QVariantList rows;
    if (pack.value("provider") == "curseforge") {
        const auto path =
            NativePackFileStore::resolve(pack.value("path").toString(), "minecraftinstance.json");
        QFile file(path);
        if (path.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) {
            if (error)
                *error = "Could not read CurseForge content locks.";
            return {};
        }
        return CurseforgeContentState::read(file.readAll(), pack, error);
    }
    if (pack.value("provider") != "modrinth")
        return rows;
    const QDir profile(pack.value("path").toString());
    const auto database = databasePath(pack);
    if (database.isEmpty() || !QFileInfo::exists(database))
        return rows;
    const auto connection = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(database);
        db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000");
        if (db.open()) {
            const bool locks = db.tables().contains("instance_content_locks");
            QSqlQuery query(db);
            const auto sql =
                QString("SELECT f.relative_path,f.sha1,f.enabled,e.project_id,e.version_id,%1 FROM instances "
                        "i JOIN instance_files f ON f.instance_id=i.id LEFT JOIN instance_content_entries e "
                        "ON e.file_id=f.id %2 WHERE %3=?")
                    .arg(locks ? "CASE WHEN l.file_id IS NULL THEN 0 ELSE 1 END" : "0",
                         locks ? "LEFT JOIN instance_content_locks l ON l.file_id=f.id" : "",
                         pack.value("launcherInstanceId").toString().isEmpty() ? "i.path" : "i.id");
            query.prepare(sql);
            query.addBindValue(pack.value("launcherInstanceId").toString().isEmpty()
                                   ? profile.dirName()
                                   : pack.value("launcherInstanceId").toString());
            if (!query.exec()) {
                if (error)
                    *error = "Could not read Modrinth content locks. No content was changed.";
            } else
                while (query.next()) {
                    auto relative = query.value(0).toString();
                    const auto hash = query.value(1).toString().toLower();
                    if (!query.value(2).toBool() && !relative.endsWith(".disabled") &&
                        !QFileInfo::exists(profile.filePath(relative)))
                        relative += ".disabled";
                    if (relative.count('/') != 1 || relative.contains('\\') || relative.contains("..") ||
                        hash.size() != 40)
                        continue;
                    QVariantMap ids;
                    if (!query.value(3).toString().isEmpty())
                        ids["modrinth"] = query.value(3).toString();
                    rows << QVariantMap{{"relativePath", relative},
                                        {"category", relative.section('/', 0, 0)},
                                        {"sha1", hash},
                                        {"enabled", query.value(2).toBool()},
                                        {"sourceIds", ids},
                                        {"provider", "modrinth"},
                                        {"projectId", query.value(3).toString()},
                                        {"versionId", query.value(4).toString()},
                                        {"launcherLocked", query.value(5).toBool()},
                                        {"missing", !QFileInfo::exists(profile.filePath(relative))}};
                }
        } else if (error)
            *error = "Could not open Modrinth content locks. No content was changed.";
    }
    QSqlDatabase::removeDatabase(connection);
    return rows;
}
} // namespace studio
