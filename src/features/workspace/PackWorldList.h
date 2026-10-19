#pragma once
#include "platform/PackFileStore.h"
#include <QObject>
#include <memory>
namespace studio {
class PackWorldList final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
  public:
    PackWorldList(std::shared_ptr<const IPackFileReader>, QObject *parent = nullptr);
    void setRoot(const QString &);
    QVariantList entries() const {
        return m_entries;
    }
    bool busy() const {
        return m_busy;
    }
    Q_INVOKABLE void refresh();
  signals:
    void changed();

  private:
    std::shared_ptr<const IPackFileReader> m_files;
    QString m_root;
    QVariantList m_entries;
    bool m_busy = false;
    int m_generation = 0;
};
} // namespace studio
