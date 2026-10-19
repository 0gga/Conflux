#include "PackFileStore.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
namespace studio {
NativePackFileStore::NativePackFileStore(QString backupRoot) : m_backupRoot(std::move(backupRoot)) {
    if (m_backupRoot.isEmpty())
        m_backupRoot =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/config-backups";
}
QString NativePackFileStore::resolve(const QString &root, const QString &relative) {
    if (root.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains('\\'))
        return {};
    const auto parts = relative.split('/', Qt::SkipEmptyParts);
    for (const auto &part : parts)
        if (part == ".." || part == ".git" || part == ".env")
            return {};
    const auto base = QFileInfo(root).canonicalFilePath();
    if (base.isEmpty())
        return {};
    QString current = base;
    for (const auto &part : parts) {
        current = QDir(current).filePath(part);
        const QFileInfo info(current);
        if (info.isSymLink() || info.isJunction())
            return {};
    }
    const auto path = QFileInfo(current).canonicalFilePath();
    return path == base || path.startsWith(base + '/') ? path : QString{};
}
FileReadResult NativePackFileStore::read(const QString &root, const QString &relative, qint64 limit,
                                         bool tail) const {
    const auto path = resolve(root, relative);
    QFile file(path);
    if (path.isEmpty() || !QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly))
        return {{}, "This file is unavailable or outside the pack."};
    if (file.size() > limit && !tail)
        return {{}, "File exceeds the editor's size limit."};
    const bool truncated = file.size() > limit;
    if (truncated)
        file.seek(file.size() - limit);
    return {file.read(limit), {}, truncated};
}
QVariantList NativePackFileStore::list(const QString &root, const QString &relative) const {
    QVariantList rows;
    const auto path = resolve(root, relative);
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return rows;
    for (const auto &file :
         QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                  QDir::DirsFirst | QDir::Name | QDir::IgnoreCase)) {
        if (file.isJunction() || file.fileName() == ".git" || file.fileName() == ".env")
            continue;
        const auto rel = QDir(root).relativeFilePath(file.absoluteFilePath());
        rows.append(QVariantMap{{"title", file.fileName()},
                                {"relativePath", rel},
                                {"directory", file.isDir()},
                                {"size", file.size()},
                                {"modified", file.lastModified().toString("dd MMM yyyy · hh:mm")},
                                {"iconName", file.isDir() ? "folder" : "file"}});
        if (rows.size() >= 2000)
            break;
    }
    return rows;
}
FileWriteResult NativePackFileStore::writeConfig(const QString &root, const QString &relative,
                                                 const QByteArray &expected,
                                                 const QByteArray &replacement) const {
    if ((!relative.startsWith("config/") && !relative.startsWith("defaultconfigs/")) ||
        replacement.size() > 2 * 1024 * 1024)
        return {false, "Only supported config files can be saved."};
    const auto path = resolve(root, relative);
    const auto current = read(root, relative, 2 * 1024 * 1024);
    if (path.isEmpty() || !current.error.isEmpty())
        return {false, current.error.isEmpty() ? "Unsafe config path." : current.error};
    if (current.bytes != expected)
        return {false,
                "This file changed outside Conflux. Reload it before saving; your edits are still here."};
    const auto packId = QString::fromLatin1(
        QCryptographicHash::hash(QFileInfo(root).canonicalFilePath().toUtf8(), QCryptographicHash::Sha256)
            .toHex());
    const auto folder = QDir(m_backupRoot).filePath(packId);
    if (!QDir().mkpath(folder))
        return {false, "Could not create the backup folder. No config was changed."};
    const auto backupPath = QDir(folder).filePath(
        QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss-zzz") + "-" +
        QUuid::createUuid().toString(QUuid::Id128) + "-" + QFileInfo(relative).fileName());
    QSaveFile backup(backupPath);
    if (!backup.open(QIODevice::WriteOnly) || backup.write(expected) != expected.size() || !backup.commit())
        return {false, "Could not save a backup. No config was changed."};
    // Recheck after backup I/O; QSaveFile replaces the file atomically without a direct-write fallback.
    if (read(root, relative, 2 * 1024 * 1024).bytes != expected || resolve(root, relative) != path)
        return {false, "The config changed while saving. Reload before trying again."};
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(replacement) != replacement.size() ||
        !output.commit())
        return {false, "Could not save config: " + output.errorString(), backupPath};
    return {true, {}, backupPath};
}
} // namespace studio
