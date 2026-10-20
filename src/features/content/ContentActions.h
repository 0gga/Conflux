#pragma once
#include "ContentPorts.h"
#include "features/preferences/ContentVisibility.h"
#include <QObject>
#include <memory>
namespace studio {
class ContentActions final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
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
    Q_PROPERTY(QVariantList versions READ versions NOTIFY changed)
    Q_PROPERTY(QVariantList versionGroups READ versionGroups NOTIFY changed)
    Q_PROPERTY(QVariantMap versionTarget READ versionTarget NOTIFY changed)
    Q_PROPERTY(bool versionsBusy READ versionsBusy NOTIFY changed)
    Q_PROPERTY(QString versionError READ versionError NOTIFY changed)
  public:
    ContentActions(std::shared_ptr<const IContentFileStore>, std::shared_ptr<IContentCatalogue>,
                   std::shared_ptr<IContentInstaller>, std::shared_ptr<IContentPreferences>,
                   QObject *parent = nullptr);
    void setContext(const QVariantMap &, const QVariantList &);
    void setCurseforgeKey(const QByteArray &key) {
        m_catalogue->setCurseforgeKey(key);
    }
    QVariantList rows() const;
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
        return m_busy;
    }
    bool cancellable() const {
        return m_busy && (!m_mutating || m_installer->cancellable());
    }
    QVariantList inventory() const {
        return m_rows;
    }
    int updateCount() const;
    QString notice() const {
        return m_notice;
    }
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
    }
    void visibilityChanged() {
        emit changed();
    }
    Q_INVOKABLE void hide(const QString &path, bool hidden = true);
    Q_INVOKABLE void refresh(bool checkUpdates = true);
    Q_INVOKABLE void setEnabled(const QString &, bool);
    Q_INVOKABLE void remove(const QString &);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void freeze(const QString &, bool);
    Q_INVOKABLE void update(const QString &);
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void openVersions(const QString &);
    Q_INVOKABLE void installVersion(int index);
    Q_INVOKABLE void installRelease(const QVariantMap &release, const QVariantMap &project);
    Q_INVOKABLE void importFiles(const QList<QUrl> &, const QString &);
    Q_INVOKABLE void copyLink(const QString &);
    Q_INVOKABLE void cancel();
    void setProgress(const QString &message) {
        m_notice = message;
        emit changed();
    }
  signals:
    void changed();
    void contentChanged();
    void copyRequested(QString text);

  private:
    QVariantMap find(const QString &) const;
    QString preferenceIdentity(const QVariantMap &) const;
    void applyPreferences();
    void runWrite(std::function<ContentWriteResult()>);
    void finishWrite(ContentWriteResult);
    void install(const QVector<ContentInstallRequest> &);
    std::shared_ptr<IContentVisibility> m_visibility;
    std::shared_ptr<const IContentFileStore> m_files;
    std::shared_ptr<IContentCatalogue> m_catalogue;
    std::shared_ptr<IContentInstaller> m_installer;
    std::shared_ptr<IContentPreferences> m_preferences;
    QVariantMap m_pack, m_filters, m_versionTarget;
    QVariantList m_inventory, m_rows, m_versions;
    QStringList m_selected;
    QString m_query, m_category = "all", m_sort = "name-asc", m_notice, m_versionError;
    bool m_busy = false, m_mutating = false, m_dirty = false, m_versionsBusy = false;
    int m_generation = 0, m_versionGeneration = 0;
};
} // namespace studio
