#pragma once
#include <QHash>
#include <QReadWriteLock>
#include <QSet>
#include <QVariantList>
namespace studio {
class ContentLockRegistry final {
  public:
    void setRows(const QString &pack, const QVariantList &);
    bool locked(const QString &pack, const QString &relativePath) const;
    bool projectLocked(const QString &pack, const QString &provider, const QString &id) const;

  private:
    static QString packKey(const QString &);
    static QString pathKey(QString);
    mutable QReadWriteLock m_mutex;
    QHash<QString, QSet<QString>> m_paths, m_projects;
};
} // namespace studio
