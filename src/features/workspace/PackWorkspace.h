#pragma once
#include "services/PackContent.h"
#include <QObject>
namespace studio {
class PackWorkspace final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap pack READ pack NOTIFY changed)
    Q_PROPERTY(QVariantList files READ files NOTIFY changed)
    Q_PROPERTY(QVariantList issues READ issues NOTIFY changed)
    Q_PROPERTY(QString coverage READ coverage NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
  public:
    PackWorkspace(std::shared_ptr<const IPackContentInspector>, QObject *parent = nullptr);
    QVariantMap pack() const {
        return m_pack;
    }
    QVariantList files() const {
        return m_content.files;
    }
    QVariantList issues() const {
        return m_content.issues;
    }
    QString coverage() const {
        return m_content.coverage;
    }
    bool busy() const {
        return m_busy;
    }
    void setPack(const QVariantMap &);
    Q_INVOKABLE void refresh();
  signals:
    void changed();

  private:
    std::shared_ptr<const IPackContentInspector> m_inspector;
    QVariantMap m_pack;
    PackContentResult m_content;
    bool m_busy = false;
    int m_generation = 0;
};
} // namespace studio
