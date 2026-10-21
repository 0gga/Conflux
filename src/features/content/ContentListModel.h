#pragma once
#include <QAbstractListModel>
#include <QVariantList>
namespace studio {
class ContentListModel final : public QAbstractListModel {
    Q_OBJECT
  public:
    explicit ContentListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : m_rows.size();
    }
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override {
        return {{Qt::UserRole + 1, "modelData"}};
    }
    void setRows(const QVariantList &);

  private:
    QVariantList m_rows;
};
} // namespace studio
