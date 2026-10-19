#pragma once
#include <QByteArray>
#include <QString>
#include <QVariantList>
namespace studio {
struct FileReadResult {
    QByteArray bytes;
    QString error;
    bool truncated = false;
};
struct FileWriteResult {
    bool ok = false;
    QString error, backupPath;
};
// Filesystem access is injected. Feature models never call QFile directly.
class IPackFileReader {
  public:
    virtual ~IPackFileReader() = default;
    virtual FileReadResult read(const QString &root, const QString &relative, qint64 limit,
                                bool tail = false) const = 0;
    virtual QVariantList list(const QString &root, const QString &relative) const = 0;
};
class IConfigFileWriter {
  public:
    virtual ~IConfigFileWriter() = default;
    virtual FileWriteResult writeConfig(const QString &root, const QString &relative,
                                        const QByteArray &expected, const QByteArray &replacement) const = 0;
};
class NativePackFileStore final : public IPackFileReader, public IConfigFileWriter {
  public:
    explicit NativePackFileStore(QString backupRoot = {});
    FileReadResult read(const QString &, const QString &, qint64, bool = false) const override;
    QVariantList list(const QString &, const QString &) const override;
    FileWriteResult writeConfig(const QString &, const QString &, const QByteArray &,
                                const QByteArray &) const override;
    static QString resolve(const QString &root, const QString &relative);

  private:
    QString m_backupRoot;
};
} // namespace studio
