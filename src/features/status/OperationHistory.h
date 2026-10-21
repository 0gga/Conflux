#pragma once
#include <QAbstractListModel>
#include <QVariantList>
namespace studio {
class OperationHistory final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY changed)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY changed)
  public:
    explicit OperationHistory(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    QAbstractItemModel *model() {
        return this;
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(m_entries.size());
    }
    QVariant data(const QModelIndex &, int) const override;
    QHash<int, QByteArray> roleNames() const override {
        return {{Qt::UserRole + 1, "modelData"}};
    }
    QVariantList entries() const {
        return m_entries;
    }
    int activeCount() const;
    int failedCount() const;
    void queued(quint64, const QVariantMap &, const QString &, const QString &);
    void start(quint64);
    void progress(quint64, const QString &);
    void finish(quint64, bool, const QString &, bool retryable = true);
    Q_INVOKABLE void dismiss(quint64);
    Q_INVOKABLE void retry(quint64 id) {
        emit retryRequested(id);
    }
    QString details(quint64) const;
  signals:
    void changed();
    void retryRequested(quint64);
    void dismissed(quint64);

  private:
    QVariantList m_entries;
    void set(quint64, const QString &, const QVariant &);
    void prune();
};
} // namespace studio
