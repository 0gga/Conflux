#pragma once
#include "ContentListModel.h"
#include <QObject>
#include <QSet>
#include <functional>
namespace studio {
class DependencyRemovals final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QVariantMap pack READ pack NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(int unusedCount READ unusedCount NOTIFY changed)
  public:
    using Remove = std::function<bool(QVariantMap, QVariantList)>;
    explicit DependencyRemovals(QObject *parent = nullptr);
    bool visible() const {
        return !m_keys.isEmpty();
    }
    QVariantMap pack() const {
        return m_pack;
    }
    QVariantList rows() const {
        return m_rows;
    }
    QAbstractItemModel *model() {
        return &m_model;
    }
    int unusedCount() const {
        return m_unused.size();
    }
    void setRemover(Remove remove) {
        m_remove = std::move(remove);
    }
    void observe(const QVariantMap &pack, const QVariantList &before, const QVariantList &current,
                 const QStringList &targets, const QString &error, bool cleanup);
    Q_INVOKABLE void remove(const QString &key);
    Q_INVOKABLE void removeUnused();
    Q_INVOKABLE void retry(const QString &key) {
        remove(key);
    }
    Q_INVOKABLE void dismiss();
  signals:
    void changed();

  private:
    struct Offer {
        QVariantMap pack;
        QVariantList files;
        QStringList keys;
    };
    ContentListModel m_model;
    QVariantMap m_pack;
    QVariantList m_files;
    QVariantList m_rows;
    QStringList m_unused;
    QSet<QString> m_individual;
    QStringList m_keys;
    QHash<QString, QVariantMap> m_details;
    QHash<QString, QString> m_errors;
    QSet<QString> m_queued;
    QList<Offer> m_offers;
    Remove m_remove;
    quint64 m_generation = 0;
    void accept(Offer);
    void apply(const QStringList &);
    void rebuild();
    void publish();
};
} // namespace studio
