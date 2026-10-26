#include "LauncherContentState.h"
#include "CurseforgeContentState.h"
#include "PackFileStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
namespace studio {
QString LauncherContentState::databasePath(const QVariantMap &pack) {
    if (pack.contains("launcherDatabasePath")) {
        const auto path = pack.value("launcherDatabasePath").toString();
        const QFileInfo database(path);
        if (!QDir::isAbsolutePath(path) || database.fileName() != "app.db")
            return {};
        return NativePackFileStore::resolve(database.absolutePath(), "app.db");
    }
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
    if (error)
        error->clear();
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
    if (database.isEmpty() || !QFileInfo::exists(database)) {
        if (error)
            *error = "Modrinth content locks are unavailable. Refresh the source launcher and try again.";
        return rows;
    }
    const auto connection = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(database);
        db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000");
        if (db.open() && db.transaction()) {
            const bool locks = db.tables().contains("instance_content_locks");
            QSqlQuery query(db);
            const auto instance = pack.value("launcherInstanceId").toString();
            const auto sql =
                QString("SELECT i.id,i.path,f.relative_path,f.sha1,f.enabled,e.project_id,e.version_id,%1 "
                        "FROM instances i LEFT JOIN instance_files f ON f.instance_id=i.id "
                        "LEFT JOIN instance_content_entries e ON e.file_id=f.id %2 WHERE %3")
                    .arg(locks ? "CASE WHEN l.file_id IS NULL THEN 0 ELSE 1 END" : "0",
                         locks ? "LEFT JOIN instance_content_locks l ON l.file_id=f.id" : "",
                         instance.isEmpty() ? "(i.path=? OR i.path=?)" : "i.id=?");
            query.prepare(sql);
            query.addBindValue(instance.isEmpty() ? profile.dirName() : instance);
            if (instance.isEmpty())
                query.addBindValue(profile.absolutePath());
            const auto fail = [&] {
                rows.clear();
                if (error)
                    *error = "Could not read Modrinth content locks. No content was changed.";
            };
            if (!query.exec() || !query.next())
                fail();
            else {
                const auto selectedId = query.value(0).toString(), storedPath = query.value(1).toString();
                const auto expectedPath = profile.canonicalPath();
                const auto profiles = QFileInfo(database).dir().filePath("profiles");
                const auto actualPath =
                    QFileInfo(QDir::isAbsolutePath(storedPath) ? storedPath
                                                               : QDir(profiles).filePath(storedPath))
                        .canonicalFilePath();
#ifdef Q_OS_WIN
                constexpr auto pathCase = Qt::CaseInsensitive;
#else
                constexpr auto pathCase = Qt::CaseSensitive;
#endif
                if (selectedId.isEmpty() || expectedPath.isEmpty() || actualPath.isEmpty() ||
                    actualPath.compare(expectedPath, pathCase) != 0)
                    fail();
                else
                    do {
                        if (query.value(0).toString() != selectedId ||
                            query.value(1).toString() != storedPath) {
                            fail();
                            break;
                        }
                        // The selected instance can authoritatively contain no tracked files.
                        if (query.value(2).isNull())
                            continue;
                        auto relative = query.value(2).toString();
                        const auto hash = query.value(3).toString().toLower();
                        if (!query.value(4).toBool() && !relative.endsWith(".disabled") &&
                            !QFileInfo::exists(profile.filePath(relative)))
                            relative += ".disabled";
                        if (relative.count('/') != 1 || relative.contains('\\') || relative.contains("..") ||
                            hash.size() != 40)
                            continue;
                        QVariantMap ids;
                        if (!query.value(5).toString().isEmpty())
                            ids["modrinth"] = query.value(5).toString();
                        rows << QVariantMap{{"relativePath", relative},
                                            {"category", relative.section('/', 0, 0)},
                                            {"sha1", hash},
                                            {"enabled", query.value(4).toBool()},
                                            {"sourceIds", ids},
                                            {"provider", "modrinth"},
                                            {"projectId", query.value(5).toString()},
                                            {"versionId", query.value(6).toString()},
                                            {"launcherLocked", query.value(7).toBool()},
                                            {"missing", !QFileInfo::exists(profile.filePath(relative))}};
                    } while (query.next());
                if (query.lastError().isValid())
                    fail();
            }
            query.finish();
            if (!db.rollback())
                fail();
        } else if (error)
            *error = "Could not open Modrinth content locks. No content was changed.";
    }
    QSqlDatabase::removeDatabase(connection);
    return rows;
}
} // namespace studio
