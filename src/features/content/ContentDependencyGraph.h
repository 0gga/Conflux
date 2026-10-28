#pragma once
#include <QHash>
#include <QSet>
#include <QVariantList>
namespace studio {
// Exact installed identities only. No network, preferences, filenames-as-identities or file writes.
class ContentDependencyGraph final {
  public:
    explicit ContentDependencyGraph(const QVariantList &);
    QVariantMap relations(int position) const;
    QVariantList disabledRequirements() const;
    QVariantList missingRequirements() const {
        return m_missing;
    }
    QStringList descendants(const QStringList &roots) const;
    QStringList unused(const QStringList &candidates) const;
    bool individuallyUnused(const QString &key) const;

  private:
    struct Edge {
        int node;
        bool required;
    };
    QVariantList m_rows;
    QVariantList m_missing;
    QHash<QString, QList<int>> m_keys;
    QSet<int> m_ambiguous;
    QList<QList<Edge>> m_children, m_parents;
};
} // namespace studio
