#pragma once
#include <QCache>
#include <QMutex>
#include <QVariantMap>
#include <functional>
namespace studio {
// Process-local, bounded caches. File identity and change time invalidate same-size replacements.
class ContentFileCache final {
  public:
    struct Statistics {
        quint64 hashedFiles = 0, hashedBytes = 0, decodedFiles = 0;
    };
    static ContentFileCache &instance();
    static QString fingerprint(const QString &path);
    QString sha1(const QString &path) const;
    QVariantMap metadata(const QString &path, const QString &kind,
                         const std::function<QVariantMap()> &read) const;
    Statistics statistics() const;
    void clear();

  private:
    mutable QMutex m_mutex;
    mutable QCache<QString, QString> m_hashes{4096};
    mutable QCache<QString, QVariantMap> m_metadata{32768}; // approximate KiB
    mutable Statistics m_statistics;
};
} // namespace studio
