#pragma once
#include "features/content/ContentIndex.h"
#include <QMutex>
namespace studio {
class SqliteContentIndex final : public IContentIndex {
  public:
    explicit SqliteContentIndex(QString path);
    quint64 beginSnapshot(const QVariantMap &) const override;
    ContentIndexResult lookup(const QVariantMap &, const QVariantList &, quint64 snapshot = 0) const override;
    QString save(const QVariantMap &, const QVariantList &) const override;

  private:
    QString m_path;
    mutable QMutex m_mutex;
    mutable QMutex m_snapshotMutex;
    mutable QHash<QString, quint64> m_snapshots;
    mutable quint64 m_snapshot = 0;
};
} // namespace studio
