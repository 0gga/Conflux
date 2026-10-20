#pragma once
#include "features/content/ContentPorts.h"
#include <QObject>
#include <memory>
namespace studio {
// Instance-scoped browse state; all identity comes from indexed files, never titles.
class BrowseInstalls final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap pack READ pack NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool connectorAvailable READ connectorAvailable NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
  public:
    explicit BrowseInstalls(std::shared_ptr<IContentCatalogue>, QObject *parent = nullptr);
    void setContext(const QVariantMap &, const QVariantList &, bool indexing);
    void setTargetPath(const QString &);
    void setCurseforgeKey(const QByteArray &key) {
        m_catalogue->setCurseforgeKey(key);
    }
    QVariantMap pack() const {
        return ready() ? m_pack : QVariantMap{};
    }
    bool ready() const {
        return !m_target.isEmpty() && m_pack.value("path").toString() == m_target;
    }
    bool busy() const {
        return m_indexing || m_loading;
    }
    bool connectorAvailable() const;
    QString versionLoaders() const;
    QString notice() const {
        return m_notice;
    }
    Q_INVOKABLE QVariantMap state(const QVariantMap &) const;
    Q_INVOKABLE bool installed(const QVariantMap &, const QString &type = {}) const;
    Q_INVOKABLE void install(const QVariantMap &);
    Q_INVOKABLE void installVersion(const QVariantMap &, const QVariantMap &);
  signals:
    void changed();
    void installRequested(QVariantMap release, QVariantMap project);

  private:
    std::shared_ptr<IContentCatalogue> m_catalogue;
    QVariantMap m_pack;
    QVariantList m_rows;
    QString m_target, m_notice;
    bool m_indexing = false, m_loading = false;
    int m_generation = 0;
};
} // namespace studio
