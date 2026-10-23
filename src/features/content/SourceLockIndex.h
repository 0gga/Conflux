#pragma once
#include <QHash>
#include <QVariantList>
#include <QVariantMap>
namespace studio {
// Accepted source snapshots are indexed outside UI getters. Pending intent stays in the operation queue.
class SourceLockIndex final {
  public:
    void remember(const QVariantMap &pack);
    bool contains(const QString &path) const;
    QVariantList sourceRows(const QString &path) const;
    QVariantMap find(const QString &path, const QVariantMap &projectIds) const;

  private:
    struct Lock {
        int order;
        QVariantMap row;
    };
    struct Snapshot {
        QVariantList rows;
        QHash<QString, Lock> projects;
    };
    QHash<QString, Snapshot> m_packs;
    static QString key(const QString &);
};
} // namespace studio
