#pragma once
#include <QByteArray>
#include <QMap>
#include <QStringList>
namespace studio {
class IArchiveReader {
  public:
    virtual ~IArchiveReader() = default;
    // Inclusive uncompressed byte limit per entry. Negative rejects all entries;
    // zero permits only empty entries. Missing/invalid/oversized entries are omitted.
    virtual QMap<QString, QByteArray> readEntries(const QString &path, const QStringList &entries,
                                                  qsizetype limit = 1024 * 1024) const = 0;
};
class ZipArchiveReader final : public IArchiveReader {
  public:
    QMap<QString, QByteArray> readEntries(const QString &, const QStringList &, qsizetype) const override;
};
QString rasterIcon(const QByteArray &bytes);
} // namespace studio
