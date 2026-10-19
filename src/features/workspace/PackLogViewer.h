#pragma once
#include "platform/PackFileStore.h"
#include <QObject>
#include <memory>
namespace studio {
class PackLogViewer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString selected READ selected NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
  public:
    PackLogViewer(std::shared_ptr<const IPackFileReader>, QObject *parent = nullptr);
    void setRoot(const QString &);
    QVariantList entries() const {
        return m_entries;
    }
    QString text() const {
        return m_text;
    }
    QString selected() const {
        return m_selected;
    }
    QString error() const {
        return m_error;
    }
    bool busy() const {
        return m_busy;
    }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void select(const QString &);
  signals:
    void changed();

  private:
    std::shared_ptr<const IPackFileReader> m_files;
    QString m_root, m_text, m_selected, m_error;
    QVariantList m_entries;
    bool m_busy = false;
    int m_generation = 0;
};
} // namespace studio
