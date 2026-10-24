#include "CurseforgeFingerprint.h"
#include "ContentFileCache.h"
#include <QCryptographicHash>
#include <QFile>
namespace studio {
namespace {
bool whitespace(unsigned char byte) {
    return byte == 9 || byte == 10 || byte == 13 || byte == 32;
}
} // namespace
CurseforgeFingerprintResult curseforgeFingerprint(const QString &path, const QString &expectedSha1) {
    const auto stamp = ContentFileCache::fingerprint(path);
    QFile file(path);
    if (stamp.isEmpty() || expectedSha1.size() != 40 || !file.open(QIODevice::ReadOnly) ||
        file.size() > 1024LL * 1024 * 1024)
        return {0, "Could not read this file for CurseForge identification."};
    quint32 length = 0;
    qint64 bytesRead = 0;
    QCryptographicHash sha1(QCryptographicHash::Sha1);
    while (!file.atEnd()) {
        const auto bytes = file.read(256 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError)
            return {0, "Could not read this file for CurseForge identification."};
        bytesRead += bytes.size();
        if (bytesRead > 1024LL * 1024 * 1024)
            return {0, "This content file exceeds the identification size limit."};
        sha1.addData(bytes);
        for (const unsigned char byte : bytes)
            if (!whitespace(byte))
                ++length;
    }
    if (QString::fromLatin1(sha1.result().toHex()).compare(expectedSha1, Qt::CaseInsensitive) ||
        ContentFileCache::fingerprint(path) != stamp || !file.seek(0))
        return {0, "Content changed before CurseForge identification. Refresh and retry."};
    // MurmurHash2, seed1, over the non-whitespace byte stream. Blocks are explicitly little endian.
    constexpr quint32 multiplier = 0x5bd1e995;
    quint32 result = 1 ^ length, block = 0, count = 0, consumed = 0;
    bytesRead = 0;
    while (!file.atEnd()) {
        const auto bytes = file.read(256 * 1024);
        if (bytes.isEmpty() && file.error() != QFileDevice::NoError)
            return {0, "Could not read this file for CurseForge identification."};
        bytesRead += bytes.size();
        if (bytesRead > 1024LL * 1024 * 1024)
            return {0, "This content file exceeds the identification size limit."};
        for (const unsigned char byte : bytes) {
            if (whitespace(byte))
                continue;
            ++consumed;
            block |= quint32(byte) << (8 * count++);
            if (count != 4)
                continue;
            block *= multiplier;
            block ^= block >> 24;
            block *= multiplier;
            result *= multiplier;
            result ^= block;
            block = count = 0;
        }
    }
    if (count) {
        result ^= block;
        result *= multiplier;
    }
    result ^= result >> 13;
    result *= multiplier;
    result ^= result >> 15;
    if (consumed != length || ContentFileCache::fingerprint(path) != stamp)
        return {0, "Content changed during CurseForge identification. Refresh and retry."};
    return {result, {}};
}
} // namespace studio
