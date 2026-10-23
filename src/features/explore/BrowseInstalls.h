#pragma once
#include "features/content/ContentOperationQueue.h"
#include "features/content/ContentPorts.h"
#include "features/preferences/PackReleasePolicy.h"
#include <QHash>
#include <QObject>
#include <QSet>
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
    Q_PROPERTY(int revision READ revision NOTIFY changed)
  public:
    explicit BrowseInstalls(std::shared_ptr<IContentCatalogue>, QObject *parent = nullptr);
    void setReleasePolicy(std::shared_ptr<PackReleasePolicy>);
    void setContext(const QVariantMap &, const QVariantList &, bool indexing);
    void setTargetPath(const QString &);
    void rememberReleases(const QVariantMap &project, const QVariantList &groups);
    QVariantList inventory() const {
        return m_rows;
    }
    int revision() const {
        return m_revision;
    }
    void setCurseforgeKey(const QByteArray &key) {
        m_catalogue->setCurseforgeKey(key);
        const bool enabled = !key.trimmed().isEmpty();
        if (enabled != m_curseforgeEnabled) {
            m_curseforgeEnabled = enabled;
            ++m_revision;
            emit changed();
        }
    }
    QVariantMap pack() const {
        return ready() ? m_pack : QVariantMap{};
    }
    bool ready() const {
        return !m_target.isEmpty() && m_pack.value("path").toString() == m_target;
    }
    bool busy() const {
        return m_indexing;
    }
    bool lookupsPending() const {
        return !m_pendingLookups.isEmpty();
    }
    bool connectorAvailable() const;
    QString versionLoaders() const;
    QString notice() const {
        return m_notice;
    }
    Q_INVOKABLE QVariantMap state(const QVariantMap &) const;
    Q_INVOKABLE bool installed(const QVariantMap &, const QString &type = {}) const;
    Q_INVOKABLE void install(const QVariantMap &);
    Q_INVOKABLE bool releaseAllowed(const QVariantMap &) const;
    Q_INVOKABLE void installVersion(const QVariantMap &, const QVariantMap &);
    Q_INVOKABLE void cancelPending();
  signals:
    void changed();
    void operationFailed(QString message);
    void installRequested(QVariantMap release, QVariantMap project);

  private:
    std::shared_ptr<IContentCatalogue> m_catalogue;
    std::shared_ptr<PackReleasePolicy> m_releasePolicy;
    QVariantMap m_pack;
    QVariantList m_rows, m_inputRows;
    QHash<QString, int> m_projectRows;
    bool m_identitiesChanged = false;
    QString m_target, m_notice;
    bool m_indexing = false;
    bool m_curseforgeEnabled = false;
    int m_lookupGeneration = 0;
    QHash<QString, int> m_packLookupGenerations;
    QHash<QString, QString> m_lookupPacks;
    ContentOperationQueue::Completion m_activeLookup;
    QString m_activeLookupPack;
    void cancelPendingForPack(const QString &);
    int m_revision = 0;
    QHash<QString, QVariantMap> m_verifiedIdentities;
    ContentOperationQueue m_lookups;
    QSet<QString> m_pendingLookups;
    QString lookupKey(const QVariantMap &) const;
};
} // namespace studio
