#pragma once
#include "ContentListModel.h"
#include "ContentLockRegistry.h"
#include "ContentOperationExecutor.h"
#include "ContentOperationQueue.h"
#include "ContentOperationReservations.h"
#include "ContentOperationState.h"
#include "ContentPorts.h"
#include "ContentTargetIndex.h"
#include "ContentViewIndex.h"
#include "SourceLockIndex.h"
#include "features/preferences/ContentVisibility.h"
#include "features/preferences/PackReleasePolicy.h"
#include "features/status/OperationHistory.h"
#include <QHash>
#include <QObject>
#include <QTimer>
#include <memory>
namespace studio {
class ContentActions final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(studio::OperationHistory *operations READ operations CONSTANT)
    Q_PROPERTY(int lockRevision READ lockRevision NOTIFY changed)
    Q_PROPERTY(bool operationsBusy READ operationsBusy NOTIFY operationsChanged)
    Q_PROPERTY(int actionableUpdateCount READ actionableUpdateCount NOTIFY changed)
    Q_PROPERTY(QStringList authors READ authors NOTIFY changed)
    Q_PROPERTY(QStringList selected READ selected NOTIFY changed)
    Q_PROPERTY(QVariantMap filters READ filters NOTIFY changed)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY changed)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY changed)
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY changed)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(QString activity READ activity NOTIFY changed)
    Q_PROPERTY(QString activityText READ activityText NOTIFY changed)
    Q_PROPERTY(QVariantList selection READ selection NOTIFY changed)
    Q_PROPERTY(QVariantList versions READ versions NOTIFY changed)
    Q_PROPERTY(QVariantList versionGroups READ versionGroups NOTIFY changed)
    Q_PROPERTY(QVariantMap versionTarget READ versionTarget NOTIFY changed)
    Q_PROPERTY(bool versionsBusy READ versionsBusy NOTIFY changed)
    Q_PROPERTY(QString versionError READ versionError NOTIFY changed)
  public:
    ContentActions(std::shared_ptr<const IContentFileStore>, std::shared_ptr<IContentCatalogue>,
                   std::shared_ptr<IContentInstaller>, std::shared_ptr<IContentPreferences>,
                   QObject *parent = nullptr);
    void setReleasePolicy(std::shared_ptr<PackReleasePolicy>);
    void setContext(const QVariantMap &, const QVariantList &, bool forceRefresh = false);
    void setCurseforgeKey(const QByteArray &key) {
        m_catalogue->setCurseforgeKey(key);
    }
    QVariantList rows() const {
        return m_visibleRows;
    }
    QAbstractItemModel *model() {
        return &m_model;
    }
    OperationHistory *operations() {
        return &m_operations;
    }
    int lockRevision() const {
        return m_lockRevision;
    }
    bool operationsBusy() const {
        return m_queue.busy();
    }
    int actionableUpdateCount() const;
    void setLauncherLocks(std::shared_ptr<const ILauncherContentLocks> locks) {
        m_executor->setLauncherLocks(std::move(locks));
    }
    void setLockRegistry(std::shared_ptr<ContentLockRegistry> locks) {
        m_locks = std::move(locks);
    }
    QStringList authors() const;
    QStringList selected() const {
        return m_selected;
    }
    QVariantMap filters() const {
        return m_filters;
    }
    QString query() const {
        return m_query;
    }
    QString category() const {
        return m_category;
    }
    QString sort() const {
        return m_sort;
    }
    bool busy() const {
        return m_busy || m_queue.busy();
    }
    bool cancellable() const {
        return m_queue.busy() ? m_installer->cancellable() : m_busy;
    }
    QVariantList inventory() const {
        return m_rows;
    }
    int updateCount() const;
    QString notice() const {
        return m_notice;
    }
    QString activity() const {
        return m_activity;
    }
    QString activityText() const {
        return m_activityText;
    }
    QVariantList selection() const;
    QVariantList versions() const {
        return m_versions;
    }
    QVariantList versionGroups() const;
    QVariantMap versionTarget() const {
        return m_versionTarget;
    }
    bool versionsBusy() const {
        return m_versionsBusy;
    }
    QString versionError() const {
        return m_versionError;
    }
    void setQuery(const QString &);
    void setCategory(const QString &);
    void setSort(const QString &);
    Q_INVOKABLE void setFilter(const QString &, const QVariant &);
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE void select(const QString &, bool);
    Q_INVOKABLE void selectVisible(bool);
    Q_INVOKABLE void clearSelection() {
        m_selected.clear();
        emit changed();
    }
    void setVisibility(std::shared_ptr<IContentVisibility> visibility) {
        m_visibility = std::move(visibility);
        ++m_visibilityRevision;
    }
    void visibilityChanged() {
        ++m_visibilityRevision;
        emit changed();
    }
    Q_INVOKABLE void hide(const QString &path, bool hidden = true);
    Q_INVOKABLE void refresh(bool checkUpdates = true);
    Q_INVOKABLE void setEnabled(const QString &, bool);
    Q_INVOKABLE void remove(const QString &);
    Q_INVOKABLE void removeSelected();
    bool removeDependencies(const QVariantMap &, const QVariantList &);
    Q_INVOKABLE void freeze(const QString &, bool);
    Q_INVOKABLE bool projectLocked(const QVariantMap &) const;
    Q_INVOKABLE QVariantMap sourceLock(const QVariantMap &) const;
    Q_INVOKABLE void setProjectLocked(const QVariantMap &, bool);
    Q_INVOKABLE void restoreLocked(const QVariantMap &);
    Q_INVOKABLE void cancelOperation(quint64);
    Q_INVOKABLE void recordExternalFailure(const QString &message) {
        reject(message, "Content lookup");
    }
    Q_INVOKABLE void retryOperation(quint64);
    Q_INVOKABLE void copyOperationDetails(quint64 id) {
        emit copyRequested(m_operations.details(id));
    }
    Q_INVOKABLE void setLocked(const QString &, bool);
    Q_INVOKABLE void update(const QString &);
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void updateSelected();
    Q_INVOKABLE void setSelectedEnabled(bool enabled);
    Q_INVOKABLE void copySelected(const QString &format);
    Q_INVOKABLE void openVersions(const QString &);
    Q_INVOKABLE void installVersion(int index);
    Q_INVOKABLE bool releaseAllowed(const QVariantMap &) const;
    Q_INVOKABLE bool installRelease(const QVariantMap &release, const QVariantMap &project);
    Q_INVOKABLE void importFiles(const QList<QUrl> &, const QString &);
    Q_INVOKABLE void copyLink(const QString &);
    Q_INVOKABLE void cancel();
    void setProgress(const QString &message) {
        m_activityText = message;
        if (m_activeOperationId)
            m_operations.progress(m_activeOperationId, message);
        emit changed();
    }
  signals:
    void changed();
    void rowsChanged();
    void operationsChanged();
    void operationFailed(QString message);
    void contentChanged();
    void copyRequested(QString text);
    void installationFinished(QString packPath, QVariantList releases, QVariantList files, QString error);
    void removalFinished(QVariantMap pack, QVariantList before, QVariantList current, QStringList targets,
                         QString error, bool cleanup);

  private:
    void queueUpdates(const QVariantList &);
    QVariantMap find(const QString &) const;
    QString preferenceIdentity(const QVariantMap &) const;
    void applyPreferences();
    void storeState(const QString &, ContentFileState);
    void seedState(const QString &, ContentFileState);
    void saveLockPreferences(const QString &, const QVariantList &, bool);
    void changeLocks(const QVariantMap &, const QVariantList &, bool, bool sourceOnly = false);
    void prepareView() const;
    QVariantList filteredRows() const;
    void syncVisibleRows();
    bool hasPending(const QString &) const;
    bool enqueue(ContentOperation);
    void reject(const QString &, const QString &title = {});
    void startOperation(ContentOperation, ContentOperationQueue::Completion);
    void completeOperation(ContentOperation, ContentWriteResult, ContentFileState,
                           ContentOperationQueue::Completion);
    void publishProjection();
    void syncLocks(const QString &);
    bool install(const QVector<ContentInstallRequest> &, const QVariantMap &project = {});
    QVariantList withPreferences(const QVariantMap &, QVariantList) const;
    std::shared_ptr<IContentVisibility> m_visibility;
    std::shared_ptr<const IContentFileStore> m_files;
    std::shared_ptr<IContentCatalogue> m_catalogue;
    std::shared_ptr<PackReleasePolicy> m_releasePolicy;
    std::shared_ptr<IContentInstaller> m_installer;
    std::shared_ptr<IContentPreferences> m_preferences;
    std::unique_ptr<ContentOperationExecutor> m_executor;
    ContentOperationQueue m_queue;
    QTimer m_policyRefresh;
    OperationHistory m_operations;
    QHash<quint64, ContentOperation> m_failedOperations;
    quint64 m_activeOperationId = 0;
    int m_lockRevision = 0;
    ContentListModel m_model;
    std::shared_ptr<ContentLockRegistry> m_locks;
    QHash<QString, ContentFileState> m_states;
    SourceLockIndex m_sourceLocks;
    QList<ContentOperation> m_pendingOperations;
    ContentOperationReservations m_reservations;
    ContentTargetIndex m_reservationTargets, m_confirmedTargets;
    QVariantList m_visibleRows;
    mutable ContentViewIndex m_view;
    quint64 m_visibilityRevision = 0;
    quint64 m_operationId = 0;
    bool m_anyChanged = false, m_checkUpdatesPending = false;
    QVariantMap m_pack, m_filters, m_versionTarget;
    QVariantList m_inventory, m_rows, m_versions;
    QStringList m_selected;
    QString m_query, m_category = "all", m_sort = "name-asc", m_notice, m_versionError;
    QString m_activity = "idle", m_activityText;
    bool m_cancelRequested = false;
    bool m_batchingUpdates = false;
    bool m_busy = false, m_versionsBusy = false;
    int m_generation = 0, m_versionGeneration = 0;
};
} // namespace studio
