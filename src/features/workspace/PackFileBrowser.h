#pragma once
#include "platform/PackFileStore.h"
#include <QObject>
#include <memory>
namespace studio {
class PackFileBrowser final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
  public:
    PackFileBrowser(std::shared_ptr<const IPackFileReader>, QObject *parent = nullptr);
    void setRoot(const QString &);
    QVariantList entries() const {
        return m_entries;
    }
    QString folder() const {
        return m_folder;
    }
    bool busy() const {
        return m_busy;
    }
    Q_INVOKABLE void open(const QString &relative = {});
    Q_INVOKABLE void up();
  signals:
    void changed();

  private:
    std::shared_ptr<const IPackFileReader> m_files;
    QString m_root, m_folder;
    QVariantList m_entries;
    bool m_busy = false;
    int m_generation = 0;
};
} // namespace studio
