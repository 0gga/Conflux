#include "ContentFileCache.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <sys/stat.h>
#endif
namespace studio {
ContentFileCache &ContentFileCache::instance() {
    static ContentFileCache cache;
    return cache;
}
QString ContentFileCache::fingerprint(const QString &path) {
#ifdef Q_OS_WIN
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {};
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_BASIC_INFO basic{};
    const bool ok = GetFileInformationByHandle(handle, &info) &&
                    GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic));
    CloseHandle(handle);
    if (!ok || info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        return {};
    return QString("%1:%2:%3:%4:%5:%6:%7")
        .arg(info.dwVolumeSerialNumber)
        .arg(info.nFileIndexHigh)
        .arg(info.nFileIndexLow)
        .arg(info.nFileSizeHigh)
        .arg(info.nFileSizeLow)
        .arg(basic.LastWriteTime.QuadPart)
        .arg(basic.ChangeTime.QuadPart);
#else
    struct stat info{};
    const auto name = QFile::encodeName(path);
    if (lstat(name.constData(), &info) != 0 || !S_ISREG(info.st_mode))
        return {};
#ifdef Q_OS_MACOS
    const auto modified = info.st_mtimespec, changed = info.st_ctimespec;
#else
    const auto modified = info.st_mtim, changed = info.st_ctim;
#endif
    return QString("%1:%2:%3:%4:%5:%6:%7")
        .arg(info.st_dev)
        .arg(info.st_ino)
        .arg(info.st_size)
        .arg(modified.tv_sec)
        .arg(modified.tv_nsec)
        .arg(changed.tv_sec)
        .arg(changed.tv_nsec);
#endif
}
QString ContentFileCache::sha1(const QString &path) const {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto key = fingerprint(path);
        if (key.isEmpty())
            return {};
        {
            QMutexLocker lock(&m_mutex);
            if (auto *value = m_hashes.object(key))
                return *value;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        QCryptographicHash digest(QCryptographicHash::Sha1);
        if (!digest.addData(&file))
            return {};
        const auto hash = QString::fromLatin1(digest.result().toHex());
        QMutexLocker lock(&m_mutex);
        ++m_statistics.hashedFiles;
        m_statistics.hashedBytes += file.size();
        if (fingerprint(path) != key)
            continue;
        m_hashes.insert(key, new QString(hash));
        return hash;
    }
    return {};
}
QVariantMap ContentFileCache::metadata(const QString &path, const QString &kind,
                                       const std::function<QVariantMap()> &read) const {
    const auto stamp = fingerprint(path), key = kind + ':' + stamp;
    if (stamp.isEmpty())
        return read();
    {
        QMutexLocker lock(&m_mutex);
        if (auto *value = m_metadata.object(key))
            return *value;
    }
    const auto result = read();
    if (fingerprint(path) != stamp)
        return result;
    const int cost = 1 + QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).size() / 512;
    QMutexLocker lock(&m_mutex);
    ++m_statistics.decodedFiles;
    m_metadata.insert(key, new QVariantMap(result), cost);
    return result;
}
ContentFileCache::Statistics ContentFileCache::statistics() const {
    QMutexLocker lock(&m_mutex);
    return m_statistics;
}
void ContentFileCache::clear() {
    QMutexLocker lock(&m_mutex);
    m_hashes.clear();
    m_metadata.clear();
    m_statistics = {};
}
} // namespace studio
