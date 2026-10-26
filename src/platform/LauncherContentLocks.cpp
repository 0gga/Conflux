#include "LauncherContentLocks.h"
#include "ContentAccess.h"
#include "CurseforgeContentState.h"
#include "LauncherContentState.h"
#include "PackFileStore.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <memory>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
namespace studio {
namespace {
bool save(const QString &path, const QByteArray &bytes) {
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}
ContentWriteResult modrinth(const QVariantMap &pack, const QVariantList &rows, bool locked,
                            const QString &backup) {
    const auto database = LauncherContentState::databasePath(pack);
    if (database.isEmpty())
        return {false, "The Modrinth instance database is unavailable."};
    const auto connection = QUuid::createUuid().toString();
    ContentWriteResult result;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(database);
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=500");
        if (!db.open() || !db.tables().contains("instance_content_locks")) {
            result.error = "This Modrinth database does not support content locks.";
        } else {
            QSqlQuery query(db);
            if (!query.exec("BEGIN IMMEDIATE"))
                result.error = "Modrinth is busy. Retry the lock change shortly.";
            else {
                QStringList fileIds;
                QJsonArray original;
                for (const auto &value : rows) {
                    const auto row = value.toMap();
                    auto path = row.value("relativePath").toString();
                    if (path.endsWith(".disabled"))
                        path.chop(9);
                    const auto instance = pack.value("launcherInstanceId").toString();
                    query.prepare("SELECT f.id,f.sha1,i.path,EXISTS(SELECT 1 FROM instance_content_locks l "
                                  "WHERE l.file_id=f.id) "
                                  "FROM instance_files f JOIN instances i ON i.id=f.instance_id "
                                  "WHERE " +
                                  QString(instance.isEmpty() ? "i.path" : "i.id") +
                                  "=? AND (f.relative_path=? OR f.relative_path=?)");
                    query.addBindValue(instance.isEmpty() ? QDir(pack.value("path").toString()).dirName()
                                                          : instance);
                    query.addBindValue(path);
                    query.addBindValue(path + ".disabled");
                    if (!query.exec() || !query.next()) {
                        result.error =
                            "The content is not tracked by Modrinth. Refresh the source launcher first.";
                        break;
                    }
                    const auto file = query.value(0).toString(), hash = query.value(1).toString();
                    const auto instancePath = query.value(2).toString();
                    const auto expectedPath = QDir(pack.value("path").toString()).canonicalPath();
                    const auto profileRoot = QFileInfo(database).dir().filePath("profiles");
                    const auto actualPath = QFileInfo(QDir::isAbsolutePath(instancePath)
                                                          ? instancePath
                                                          : QDir(profileRoot).filePath(instancePath))
                                                .canonicalFilePath();
                    if ((locked && hash.compare(row.value("sha1").toString(), Qt::CaseInsensitive) != 0) ||
                        !LauncherContentState::sameInstanceDirectory(actualPath, expectedPath)) {
                        result.error = "Modrinth content changed. Refresh before changing its lock.";
                        break;
                    }
                    original << QJsonObject{{"fileId", file}, {"locked", query.value(3).toBool()}};
                    fileIds << file;
                    if (query.next()) {
                        result.error = "Modrinth content identity is ambiguous. No changes were made.";
                        break;
                    }
                }
                if (result.error.isEmpty() && !save(backup, QJsonDocument(original).toJson()))
                    result.error = "Could not back up the Modrinth lock state.";
                for (const auto &file : fileIds) {
                    if (!result.error.isEmpty())
                        break;
                    query.prepare(locked ? "INSERT INTO instance_content_locks(file_id) SELECT ? WHERE NOT "
                                           "EXISTS(SELECT 1 FROM instance_content_locks WHERE file_id=?)"
                                         : "DELETE FROM instance_content_locks WHERE file_id=?");
                    query.addBindValue(file);
                    if (locked)
                        query.addBindValue(file);
                    if (!query.exec())
                        result.error = "Modrinth could not save this content lock. No changes were made.";
                }
                if (result.error.isEmpty() && query.exec("COMMIT"))
                    result = {true, {}, backup};
                else {
                    query.exec("ROLLBACK");
                    if (result.error.isEmpty())
                        result.error = "Modrinth could not commit the lock change.";
                }
            }
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}
} // namespace
ContentWriteResult LauncherContentLocks::setLocked(const QVariantMap &pack, const QVariantList &rows,
                                                   bool locked) const {
    if (rows.isEmpty())
        return {false, "Choose content to lock or unlock."};
    const auto provider = pack.value("provider").toString();
    if (provider != "modrinth" && provider != "curseforge")
        return {false, "This instance has no source launcher lock state."};
    const auto root = pack.value("path").toString();
    ContentAccess access(root, ContentAccess::Write);
    if (!access.ok())
        return {false, access.error()};
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto relative = row.value("relativePath").toString();
        if (relative.count('/') != 1 ||
            !QStringList{"mods", "resourcepacks", "shaderpacks", "datapacks"}.contains(
                relative.section('/', 0, 0)))
            return {false, "Invalid content lock path."};
        const auto path = NativePackFileStore::resolve(root, relative);
        if (path.isEmpty()) {
            const QFileInfo missing(QDir(root).filePath(relative));
            if (!locked && !missing.exists() && !missing.isSymLink() && !missing.isJunction() &&
                !NativePackFileStore::resolve(root, relative.section('/', 0, 0)).isEmpty() &&
                !relative.contains("..") && !relative.contains('\\') && !relative.contains(':'))
                continue;
            return {false, "Invalid content lock path."};
        }
        if (!locked)
            continue; // Unlocking changes only the tracked lock, including missing/stale files.
        QFile file(path);
        QCryptographicHash hash(QCryptographicHash::Sha1);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file) ||
            QString::fromLatin1(hash.result().toHex())
                    .compare(row.value("sha1").toString(), Qt::CaseInsensitive) != 0)
            return {false, "This content changed before its lock could be saved. Refresh and retry."};
    }
    const auto backups =
        (m_backupRoot.isEmpty()
             ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/backups"
             : m_backupRoot) +
        "/launcher-locks/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(backups))
        return {false, "Could not create the content lock backup."};
    if (provider == "modrinth")
        return modrinth(pack, rows, locked, backups + "/modrinth-locks.json");
    const auto path = NativePackFileStore::resolve(root, "minecraftinstance.json");
#ifdef Q_OS_WIN
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {false, "CurseForge metadata is busy or unavailable. Retry shortly."};
    const auto closeHandle = [](void *value) { CloseHandle(value); };
    std::unique_ptr<void, decltype(closeHandle)> lease(handle, closeHandle);
#endif
    QFile input(path);
    if (path.isEmpty() || !input.open(QIODevice::ReadOnly) || input.size() > 32 * 1024 * 1024)
        return {false, "Could not read CurseForge instance lock metadata."};
    const auto original = input.readAll();
    input.close();
    QString error;
    const auto replacement = CurseforgeContentState::setLocked(original, pack, rows, locked, &error);
    if (!error.isEmpty())
        return {false, error};
    const auto backup = backups + "/minecraftinstance.json";
    if (!save(backup, original))
        return {false, "Could not back up CurseForge lock metadata."};
    if (!input.open(QIODevice::ReadOnly) || input.readAll() != original)
        return {false, "CurseForge metadata changed. Retry after refreshing the source launcher."};
    input.close();
#ifdef Q_OS_WIN
    // Windows replacement needs the old file closed. Keep the sharing guard through validation;
    // QSaveFile then publishes one complete replacement under the Conflux instance lease.
    lease.reset();
#endif
    if (!save(path, replacement))
        return {false, "Could not save the CurseForge content lock. No changes were made."};
    return {true, {}, backup};
}
} // namespace studio
