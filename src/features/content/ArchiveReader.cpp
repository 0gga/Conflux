#include "ArchiveReader.h"
#include "miniz.h"
#include <QBuffer>
#include <QFile>
#include <QImageReader>
namespace studio {
QMap<QString, QByteArray> ZipArchiveReader::readEntries(const QString &path, const QStringList &entries,
                                                        qsizetype limit) const {
    QMap<QString, QByteArray> result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return result;
    mz_zip_archive zip{};
    zip.m_pIO_opaque = &file;
    zip.m_pRead = [](void *source, mz_uint64 offset, void *destination, size_t length) -> size_t {
        auto *input = static_cast<QFile *>(source);
        if (!input->seek(qint64(offset)))
            return 0;
        const auto count = input->read(static_cast<char *>(destination), qint64(length));
        return count < 0 ? 0 : size_t(count);
    };
    if (!mz_zip_reader_init(&zip, mz_uint64(file.size()), 0))
        return result;
    for (const auto &entry : entries) {
        if (entry.startsWith('/') || entry.contains("..") || entry.contains('\\'))
            continue;
        const auto name = entry.toUtf8();
        const int index = mz_zip_reader_locate_file(&zip, name.constData(), nullptr, 0);
        mz_zip_archive_file_stat stat{};
        if (index < 0 || !mz_zip_reader_file_stat(&zip, index, &stat) ||
            stat.m_uncomp_size > mz_uint64(limit))
            continue;
        QByteArray bytes(qsizetype(stat.m_uncomp_size), Qt::Uninitialized);
        if (mz_zip_reader_extract_to_mem(&zip, index, bytes.data(), size_t(bytes.size()), 0))
            result[entry] = bytes;
    }
    mz_zip_reader_end(&zip);
    return result;
}
QString rasterIcon(const QByteArray &bytes) {
    QBuffer input;
    input.setData(bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    const auto size = reader.size();
    if (size.isEmpty() || size.width() > 4096 || size.height() > 4096 || reader.format() == "svg")
        return {};
    reader.setScaledSize(size.scaled(64, 64, Qt::KeepAspectRatio));
    const auto image = reader.read();
    if (image.isNull())
        return {};
    QByteArray output;
    QBuffer buffer(&output);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return "data:image/png;base64," + QString::fromLatin1(output.toBase64());
}
} // namespace studio
