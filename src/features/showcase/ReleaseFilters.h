#pragma once
#include <QObject>
#include <QVariantMap>
namespace studio {
// Pure release selection state. Never changes pack compatibility or installation policy.
class ReleaseFilters final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QVariantMap filters READ filters NOTIFY changed)
    Q_PROPERTY(QVariantMap options READ options NOTIFY changed)
  public:
    explicit ReleaseFilters(QObject *parent = nullptr);
    void setContext(const QString &identity, const QVariantList &groups, const QStringList &versions,
                    const QStringList &loaders);
    QVariantList rows() const;
    QVariantMap filters() const {
        return m_filters;
    }
    QVariantMap options() const;
    Q_INVOKABLE void configure(const QVariantMap &);
    Q_INVOKABLE void clear();
  signals:
    void changed();

  private:
    QString m_identity;
    QVariantList m_groups;
    QVariantMap m_filters;
    bool m_seeded = false;
};
} // namespace studio
