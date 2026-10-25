#pragma once
#include <QHash>
#include <QVariantList>
#include <functional>
namespace studio {
// One immutable presentation snapshot. Filtering never reads settings or derives metadata.
class ContentViewIndex final {
  public:
    void remember(const QVariantList &, const QString &instance, quint64 visibilityRevision,
                  const std::function<bool(const QVariantMap &)> &hidden);
    QVariantList filter(const QString &query, const QString &category, const QString &sort,
                        const QVariantMap &filters) const;
    QStringList authors() const {
        return m_authors;
    }

  private:
    struct Entry {
        QVariant value;
        QString text, category, title, author, filename, environment;
        QStringList authors;
        bool hidden = false, enabled = false, updates = false, locked = false;
        bool openSource = false, external = false, dependency = false;
        qint64 updatedAt = 0;
    };
    QVariantList m_snapshot;
    QList<Entry> m_entries;
    QString m_instance;
    quint64 m_visibilityRevision = 0;
    bool m_ready = false;
    QStringList m_authors;
    mutable QHash<QString, QList<int>> m_orders;
    const QList<int> &order(const QString &) const;
};
} // namespace studio
