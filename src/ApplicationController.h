#pragma once
#include "services/CatalogueClient.h"
#include "services/ModInstaller.h"
#include "services/PackRepository.h"
#include <QObject>
#include <QSettings>
#include <QVariantList>
#include <functional>
#include <memory>

namespace studio {
class ApplicationController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList installations READ installations NOTIFY stateChanged)
    Q_PROPERTY(QVariantList packs READ packs NOTIFY stateChanged)
    Q_PROPERTY(QVariantList libraryPacks READ libraryPacks NOTIFY stateChanged)
    Q_PROPERTY(int visiblePackCount READ visiblePackCount NOTIFY stateChanged)
    Q_PROPERTY(int hiddenPackCount READ hiddenPackCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantList projects READ projects NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap selectedProject READ selectedProject NOTIFY stateChanged)
    Q_PROPERTY(QVariantList releases READ releases NOTIFY stateChanged)
    Q_PROPERTY(QString description READ description NOTIFY stateChanged)
    Q_PROPERTY(QStringList gallery READ gallery NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap activePack READ activePack NOTIFY stateChanged)
    Q_PROPERTY(QVariantList activeMods READ activeMods NOTIFY stateChanged)
    Q_PROPERTY(QString gitStatus READ gitStatus NOTIFY stateChanged)
    Q_PROPERTY(QString history READ history NOTIFY stateChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY stateChanged)
    Q_PROPERTY(QString catalogueNotice READ catalogueNotice NOTIFY stateChanged)
    Q_PROPERTY(QString detailNotice READ detailNotice NOTIFY stateChanged)
    Q_PROPERTY(bool libraryBusy READ libraryBusy NOTIFY stateChanged)
    Q_PROPERTY(bool catalogueBusy READ catalogueBusy NOTIFY stateChanged)
    Q_PROPERTY(bool detailBusy READ detailBusy NOTIFY stateChanged)
    Q_PROPERTY(bool repositoryBusy READ repositoryBusy NOTIFY stateChanged)
    Q_PROPERTY(bool navigationBusy READ navigationBusy NOTIFY stateChanged)
    Q_PROPERTY(bool installBusy READ installBusy NOTIFY stateChanged)
    Q_PROPERTY(bool installCancellable READ installCancellable NOTIFY stateChanged)
    Q_PROPERTY(QString installationStatus READ installationStatus NOTIFY stateChanged)
    Q_PROPERTY(bool curseforgeEnabled READ curseforgeEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString modrinthRoot READ modrinthRoot NOTIFY stateChanged)
    Q_PROPERTY(QString curseforgeRoot READ curseforgeRoot NOTIFY stateChanged)
    Q_PROPERTY(QString packsRoot READ packsRoot CONSTANT)
    Q_PROPERTY(bool gitEnabled READ gitEnabled NOTIFY gitConfigurationChanged)
    Q_PROPERTY(QString gitAuthorName READ gitAuthorName NOTIFY gitConfigurationChanged)
    Q_PROPERTY(QString gitAuthorEmail READ gitAuthorEmail NOTIFY gitConfigurationChanged)
  public:
    explicit ApplicationController(QObject *parent = nullptr, QString packsRootOverride = {},
                                   QString settingsFileOverride = {});
    QVariantList installations() const {
        return m_installations;
    }
    QVariantList packs() const {
        return m_packs;
    }
    QVariantList libraryPacks() const;
    int visiblePackCount() const;
    int hiddenPackCount() const;
    QVariantList projects() const {
        return m_projects;
    }
    QVariantMap selectedProject() const {
        return m_selectedProject;
    }
    QVariantList releases() const {
        return m_releases;
    }
    QString description() const {
        return m_description;
    }
    QStringList gallery() const {
        return m_gallery;
    }
    QVariantMap activePack() const {
        return m_activePack;
    }
    QVariantList activeMods() const {
        return m_activeMods;
    }
    QString gitStatus() const {
        return m_gitStatus;
    }
    QString history() const {
        return m_history;
    }
    QString notice() const {
        return m_notice;
    }
    QString catalogueNotice() const {
        return m_catalogueNotice;
    }
    QString detailNotice() const {
        return m_detailNotice;
    }
    bool libraryBusy() const {
        return m_libraryBusy;
    }
    bool catalogueBusy() const {
        return m_catalogueBusy;
    }
    bool detailBusy() const {
        return m_detailBusy;
    }
    bool repositoryBusy() const {
        return m_repositoryBusy || m_installer.busy() || m_contentBusy;
    }
    bool installBusy() const {
        return m_installer.busy();
    }
    bool installCancellable() const {
        return m_installer.cancellable();
    }
    QString installationStatus() const {
        return m_installationStatus;
    }
    bool curseforgeEnabled() const {
        return m_client.hasCurseforgeKey();
    }
    QString modrinthRoot() const;
    QString curseforgeRoot() const;
    QString packsRoot() const;
    bool gitEnabled() const {
        return m_settings->value("git/enabled", false).toBool();
    }
    QString gitAuthorName() const {
        return m_settings->value("git/authorName").toString();
    }
    QString gitAuthorEmail() const {
        return m_settings->value("git/authorEmail").toString();
    }
    Q_INVOKABLE void configureGit(bool enabled, const QString &name, const QString &email);
    Q_INVOKABLE void enablePackGit();
    Q_INVOKABLE void refreshLibrary();
    void refreshActivePack();
    bool activeRefreshPending() const {
        return m_activeRefreshPending;
    }
    Q_INVOKABLE void reportContentError(const QString &error) {
        m_notice = error;
        emit stateChanged();
    }
    bool navigationBusy() const {
        return m_repositoryBusy || m_installer.busy();
    }
    void setContentBusy(bool busy) {
        if (m_contentBusy != busy) {
            m_contentBusy = busy;
            if (busy) {
                ++m_activeRefreshGeneration;
                m_activeRefreshPending = false;
            }
            emit stateChanged();
        }
    }
    Q_INVOKABLE void search(const QString &query, const QString &source, const QString &game,
                            const QString &loader, const QString &type = "mod");
    Q_INVOKABLE void selectProject(int index);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE void createPack(const QString &name, const QString &game, const QString &loader);
    Q_INVOKABLE void openPack(const QString &path);
    Q_INVOKABLE void setPackHidden(const QString &path, bool hidden);
    Q_INVOKABLE void openLibraryPack(const QString &path, bool explore = false);
    Q_INVOKABLE void openContentFolder(const QString &relativePath);
    Q_INVOKABLE void pinRelease(int index);
    Q_INVOKABLE void cancelInstallation();
    Q_INVOKABLE void removeMod(const QString &provider, const QString &projectId);
    Q_INVOKABLE void saveChanges(const QString &message);
    Q_INVOKABLE void setInstallationRoots(const QString &modrinth, const QString &curseforge);
    Q_INVOKABLE void configureCurseforge(const QString &key);
    Q_INVOKABLE void openFolder(const QString &path);
    Q_INVOKABLE void openWeb(const QString &url);
    Q_INVOKABLE void clearNotice();
  signals:
    void stateChanged();
    void gitConfigurationChanged();
    void packOpened();
    void exploreRequested();
    void curseforgeKeyConfigured(QByteArray key);

  private:
    CatalogueClient m_client;
    ModInstaller m_installer;
    bool m_installCancelRequested = false;
    std::unique_ptr<QSettings> m_settings;
    QString m_packsRootOverride;
    QVariantList m_installations, m_packs, m_projects, m_releases, m_activeMods;
    bool m_exploreAfterOpen = false;
    QVariantMap m_selectedProject, m_activePack;
    QVector<Project> m_projectData;
    QVector<Release> m_releaseData;
    QHash<QString, QString> m_identities;
    QString m_description, m_history, m_gitStatus, m_notice, m_catalogueNotice, m_detailNotice;
    QStringList m_gallery;
    QString m_searchGame, m_searchLoader;
    QString m_installationStatus;
    int m_searchGeneration = 0, m_detailGeneration = 0, m_activeRefreshGeneration = 0;
    bool m_libraryBusy = false, m_catalogueBusy = false, m_detailBusy = false, m_repositoryBusy = false;
    bool m_contentBusy = false, m_activeRefreshPending = false;
    void refreshPacks();
    void runRepository(std::function<RepositoryResult()> operation, bool navigate = false);
};
} // namespace studio
