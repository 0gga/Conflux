#pragma once
#include "features/config/ConfigEditor.h"
#include "features/config/ConfigLibrary.h"
#include "features/content/ContentActions.h"
#include "features/content/ContentIndex.h"
#include "features/content/DependencyRemovals.h"
#include "features/content/OptionalDependencies.h"
#include "features/explore/BrowseCatalogue.h"
#include "features/explore/BrowseInstalls.h"
#include "features/explore/BulkContentAdd.h"
#include "features/launch/WorldLaunch.h"
#include "features/navigation/NavigationHistory.h"
#include "features/navigation/NavigationInput.h"
#include "features/preferences/BrowsePreferences.h"
#include "features/preferences/WarningPreferences.h"
#include "features/showcase/ProjectShowcase.h"
#include "features/showcase/ReleaseFilters.h"
#include "features/status/RunningInstances.h"
#include "features/workspace/PackFileBrowser.h"
#include "features/workspace/PackLogViewer.h"
#include "features/workspace/PackWorkspace.h"
#include "features/workspace/PackWorldList.h"
#include "platform/ContentDependencySource.h"
#include "platform/ContentInstaller.h"
#include "platform/NativeContentStore.h"
#include "platform/OptionalDependencySource.h"
#include "platform/PackChangeMonitor.h"
namespace studio {
// Composition root: wires independent feature models; it contains no parsing,
// warning rules, filesystem policy or view layout.
class DesktopSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(studio::RunningInstances *running READ running CONSTANT)
    Q_PROPERTY(studio::NavigationHistory *navigation READ navigation CONSTANT)
    Q_PROPERTY(studio::WarningPreferences *warnings READ warnings CONSTANT)
    Q_PROPERTY(studio::PackWorkspace *workspace READ workspace CONSTANT)
    Q_PROPERTY(studio::PackFileBrowser *files READ files CONSTANT)
    Q_PROPERTY(studio::PackLogViewer *logs READ logs CONSTANT)
    Q_PROPERTY(studio::PackWorldList *worlds READ worlds CONSTANT)
    Q_PROPERTY(studio::ConfigEditor *editor READ editor CONSTANT)
    Q_PROPERTY(studio::ConfigLibrary *configs READ configs CONSTANT)
    Q_PROPERTY(studio::WorldLaunch *launch READ launch CONSTANT)
    Q_PROPERTY(studio::ProjectShowcase *showcase READ showcase CONSTANT)
    Q_PROPERTY(studio::PackReleasePolicy *releasePolicy READ releasePolicy CONSTANT)
    Q_PROPERTY(studio::ReleaseFilters *releaseFilters READ releaseFilters CONSTANT)
    Q_PROPERTY(studio::ContentActions *content READ content CONSTANT)
    Q_PROPERTY(studio::BrowseInstalls *browse READ browse CONSTANT)
    Q_PROPERTY(studio::BrowseCatalogue *catalogue READ catalogue CONSTANT)
    Q_PROPERTY(studio::BulkContentAdd *bulkAdd READ bulkAdd CONSTANT)
    Q_PROPERTY(studio::BrowsePreferences *browsePreferences READ browsePreferences CONSTANT)
    Q_PROPERTY(studio::InstallationPreferences *installationPreferences READ installationPreferences CONSTANT)
    Q_PROPERTY(studio::OptionalDependencies *optionalDependencies READ optionalDependencies CONSTANT)
    Q_PROPERTY(studio::DependencyRemovals *dependencyRemovals READ dependencyRemovals CONSTANT)
  public:
    explicit DesktopSession(QObject *parent = nullptr, QString settingsFile = {}, QString backupRoot = {});
    RunningInstances *running() {
        return &m_running;
    }
    NavigationHistory *navigation() {
        return &m_navigation;
    }
    WarningPreferences *warnings() {
        return &m_warnings;
    }
    PackWorkspace *workspace() {
        return &m_workspace;
    }
    PackFileBrowser *files() {
        return &m_browser;
    }
    PackLogViewer *logs() {
        return &m_logs;
    }
    PackWorldList *worlds() {
        return &m_worlds;
    }
    ConfigEditor *editor() {
        return &m_editor;
    }
    ConfigLibrary *configs() {
        return &m_configs;
    }
    WorldLaunch *launch() {
        return &m_launch;
    }
    ProjectShowcase *showcase() {
        return &m_showcase;
    }
    ReleaseFilters *releaseFilters() {
        return &m_releaseFilters;
    }
    PackReleasePolicy *releasePolicy() {
        return m_releasePolicy.get();
    }
    ContentActions *content() {
        return &m_content;
    }
    BrowseInstalls *browse() {
        return &m_browse;
    }
    BrowseCatalogue *catalogue() {
        return &m_catalogue;
    }
    BulkContentAdd *bulkAdd() {
        return &m_bulkAdd;
    }
    Q_INVOKABLE void openBulkAdd(bool fromBrowse = false);
    BrowsePreferences *browsePreferences() {
        return &m_browsePreferences;
    }
    InstallationPreferences *installationPreferences() {
        return m_installationPreferences.get();
    }
    OptionalDependencies *optionalDependencies() {
        return &m_optionalDependencies;
    }
    DependencyRemovals *dependencyRemovals() {
        return &m_dependencyRemovals;
    }
    void setCurseforgeKey(const QByteArray &key) {
        m_catalogue.setKey(key);
        m_showcase.setCurseforgeKey(key);
        m_content.setCurseforgeKey(key);
        m_browse.setCurseforgeKey(key);
        m_bulkAdd.setCurseforgeKey(key);
        m_contentInstaller->setCurseforgeKey(key);
        m_dependencySource->setCurseforgeKey(key);
        m_optionalSource->setCurseforgeKey(key);
    }
    void setPack(const QVariantMap &);
    void attachWindow(QObject &window) {
        m_input.attach(window);
    }

  private:
    std::shared_ptr<IBrowseSettingsStore> m_browseStore;
    std::shared_ptr<ContentVisibility> m_visibility;
    std::shared_ptr<PackReleasePolicy> m_releasePolicy;
    std::shared_ptr<InstallationPreferences> m_installationPreferences;
    QHash<QString, QString> m_reviewedIdentities;
    std::shared_ptr<const IContentIndex> m_contentIndex;
    std::shared_ptr<const NativePackFileStore> m_fileStore;
    std::shared_ptr<ContentLockRegistry> m_contentLocks;
    std::shared_ptr<const NativeContentStore> m_contentFiles;
    std::shared_ptr<ContentInstaller> m_contentInstaller;
    std::shared_ptr<ContentDependencySource> m_dependencySource;
    std::shared_ptr<ContentDependencyInstaller> m_dependencyInstaller;
    std::shared_ptr<OptionalDependencySource> m_optionalSource;
    RunningInstances m_running;
    NavigationHistory m_navigation;
    NavigationInput m_input;
    WarningPreferences m_warnings;
    PackWorkspace m_workspace;
    PackFileBrowser m_browser;
    PackLogViewer m_logs;
    PackWorldList m_worlds;
    ConfigEditor m_editor;
    ConfigLibrary m_configs;
    WorldLaunch m_launch;
    ProjectShowcase m_showcase;
    ReleaseFilters m_releaseFilters;
    ContentActions m_content;
    BrowseInstalls m_browse;
    BrowsePreferences m_browsePreferences;
    OptionalDependencies m_optionalDependencies;
    DependencyRemovals m_dependencyRemovals;
    BrowseCatalogue m_catalogue;
    BulkContentAdd m_bulkAdd;
    PackChangeMonitor m_changes;
    bool m_pendingScan = false;
    void activateRoute();
};
} // namespace studio
