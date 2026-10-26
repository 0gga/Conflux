#include "PackFileStore.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
namespace studio {
namespace {
QString finalExistingPath(const QString &path) {
#ifdef Q_OS_WIN
    if (path.isEmpty())
        return {};
    auto native = QDir::toNativeSeparators(path);
    if (!native.startsWith("\\\\?\\")) {
        if (native.startsWith("\\\\.\\"))
            return {};
        native = native.startsWith("\\\\") ? "\\\\?\\UNC\\" + native.mid(2) : "\\\\?\\" + native;
    }
    const auto handle = CreateFileW(native.toStdWString().c_str(), FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {};
    wchar_t buffer[32768];
    const auto length =
        GetFinalPathNameByHandleW(handle, buffer, 32768, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    if (!length || length >= 32768)
        return {};
    auto result = QDir::fromNativeSeparators(QString::fromWCharArray(buffer, length));
    if (result.startsWith("//?/UNC/"))
        result = "//" + result.mid(8);
    else if (result.startsWith("//?/"))
        result = result.mid(4);
    return result;
#else
    return path;
#endif
}
} // namespace
NativePackFileStore::NativePackFileStore(QString backupRoot) : m_backupRoot(std::move(backupRoot)) {
    if (m_backupRoot.isEmpty())
        m_backupRoot =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/config-backups";
}
bool NativePackFileStore::isAllowedComponent(const QString &name) {
    if (name.isEmpty() || name == "." || name == ".." || name.contains(QChar::Null))
        return false;
#ifdef Q_OS_WIN
    if (name.contains(':') || name.endsWith('.') || name.endsWith(' '))
        return false;
    return name.compare(".git", Qt::CaseInsensitive) != 0 && name.compare(".env", Qt::CaseInsensitive) != 0;
#else
    return name != ".git" && name != ".env";
#endif
}
QString NativePackFileStore::resolve(const QString &root, const QString &relative) {
    if (root.isEmpty() || root.contains(QChar::Null) || relative.contains(QChar::Null) ||
        QDir::isAbsolutePath(relative) || relative.contains('\\'))
        return {};
    const auto parts = relative.split('/', Qt::SkipEmptyParts);
    for (const auto &part : parts)
        if (part != "." && !isAllowedComponent(part))
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
    if (path != base && !path.startsWith(base + '/'))
        return {};
#ifdef Q_OS_WIN
    if (!parts.isEmpty()) {
        const auto expandedBase = finalExistingPath(base), expandedPath = finalExistingPath(path);
        if (expandedBase.isEmpty() || expandedPath.isEmpty() ||
            (expandedPath != expandedBase && !expandedPath.startsWith(expandedBase + '/')))
            return {};
        for (const auto &part : expandedPath.mid(expandedBase.size()).split('/', Qt::SkipEmptyParts))
            if (!isAllowedComponent(part))
                return {};
    }
#endif
    // Keep existing logical path spelling; authority checks above use actual long names.
    return path;
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
        if (file.isJunction() || !isAllowedComponent(file.fileName()))
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
    if (replacement.size() > 2 * 1024 * 1024)
        return {false, "Only supported config files can be saved."};
    const auto validated = resolve(root, relative);
    const auto path = finalExistingPath(validated);
    bool allowed = false;
    if (!path.isEmpty())
        for (const auto &category : {"config", "defaultconfigs"}) {
            const auto directory = finalExistingPath(resolve(root, category));
            if (!directory.isEmpty() && path.startsWith(directory + '/')) {
                allowed = true;
                break;
            }
        }
    if (!allowed)
        return {false, "Only supported config files can be saved."};
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
    const auto backupPath =
        QDir(folder).filePath(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss-zzz") + "-" +
                              QUuid::createUuid().toString(QUuid::Id128) + "-" + QFileInfo(path).fileName());
    QSaveFile backup(backupPath);
    if (!backup.open(QIODevice::WriteOnly) || backup.write(expected) != expected.size() || !backup.commit())
        return {false, "Could not save a backup. No config was changed."};
    // Recheck after backup I/O; QSaveFile replaces the file atomically without a direct-write fallback.
    const auto checked = read(root, relative, 2 * 1024 * 1024);
    const auto revalidated = resolve(root, relative);
    if (!checked.error.isEmpty() || checked.bytes != expected || revalidated != validated ||
        finalExistingPath(revalidated) != path)
        return {false, "The config changed while saving. Reload before trying again."};
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(replacement) != replacement.size() ||
        !output.commit())
        return {false, "Could not save config: " + output.errorString(), backupPath};
    return {true, {}, backupPath};
}
} // namespace studio
