#include "DesktopSession.h"
#include "domain/ContentTypes.h"
#include "features/explore/TolerantCatalogueSearch.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/BrowseSettingsStore.h"
#include "platform/CatalogueShowcaseSource.h"
#include "platform/ConfigOwnershipStore.h"
#include "platform/InstalledModIdentity.h"
#include "platform/InstanceProcessSource.h"
#include "platform/LauncherContentLocks.h"
#include "platform/ReviewedProjectIdentities.h"
#include "platform/WorldLaunchAdapters.h"
#include "services/ContentCatalogue.h"
namespace studio {
DesktopSession::DesktopSession(QObject *parent, QString settingsFile, QString backupRoot)
    : QObject(parent), m_browseStore(std::make_shared<BrowseSettingsStore>(settingsFile)),
      m_visibility(std::make_shared<ContentVisibility>(m_browseStore)),
      m_releasePolicy(std::make_shared<PackReleasePolicy>(m_browseStore)),
      m_fileStore(std::make_shared<NativePackFileStore>(backupRoot)),
      m_contentLocks(std::make_shared<ContentLockRegistry>()),
      m_contentFiles(std::make_shared<NativeContentStore>(
          std::make_shared<NativeInstalledModIdentity>(m_fileStore),
          backupRoot.isEmpty() ? QString{} : backupRoot + "/content", m_contentLocks)),
      m_contentInstaller(std::make_shared<ContentInstaller>(m_contentFiles)),
      m_running(std::make_shared<InstanceProcessSource>(), this), m_navigation(this),
      m_input(m_navigation, this),
      m_warnings(std::make_shared<SettingsWarningPreferenceStore>(settingsFile), this),
      m_workspace(defaultPackInspector(), this), m_browser(m_fileStore, this), m_logs(m_fileStore, this),
      m_worlds(m_fileStore, this),
      m_editor(std::make_shared<ConfigRepository>(m_fileStore, m_fileStore, defaultConfigCodecs()), this),
      m_configs(std::make_shared<SettingsConfigOwnershipStore>(settingsFile), this),
      m_launch({std::make_shared<ModrinthWorldLaunchAdapter>()}, std::make_shared<DesktopLaunchUrlOpener>(),
               m_fileStore, this),
      m_showcase(std::make_shared<CatalogueShowcaseSource>(),
                 std::make_shared<NativeInstalledModIdentity>(m_fileStore), this),
      m_content(m_contentFiles, std::make_shared<ContentCatalogue>(), m_contentInstaller,
                std::make_shared<SettingsContentPreferences>(settingsFile), this),
      m_browse(std::make_shared<ContentCatalogue>(), this), m_browsePreferences(m_browseStore, this),
      m_catalogue(std::make_shared<TolerantCatalogueSearch>(std::make_shared<CatalogueClient>()),
                  m_visibility, ReviewedProjectIdentities::load(),
                  std::make_shared<BrowseFilterPreferences>(m_browseStore), this),
      m_changes(this) {
    m_content.setReleasePolicy(m_releasePolicy);
    m_browse.setReleasePolicy(m_releasePolicy);
    m_content.setVisibility(m_visibility);
    m_content.setLockRegistry(m_contentLocks);
    m_content.setLauncherLocks(std::make_shared<LauncherContentLocks>(backupRoot));
    connect(m_visibility.get(), &ContentVisibility::changed, &m_content, &ContentActions::visibilityChanged);
    connect(m_visibility.get(), &ContentVisibility::changed, &m_catalogue, &BrowseCatalogue::refresh);
    m_catalogue.setDefaults(m_browsePreferences.defaults());
    connect(&m_browsePreferences, &BrowsePreferences::defaultsChanged, this,
            [this] { m_catalogue.setDefaults(m_browsePreferences.defaults()); });
    auto updateBrowseContext = [this] {
        const auto pack = m_browse.pack();
        QStringList versions, loaders;
        if (!pack.value("minecraft").toString().isEmpty())
            versions << pack.value("minecraft").toString();
        if (!pack.value("loader").toString().isEmpty() && pack.value("loader") != "vanilla")
            loaders << pack.value("loader").toString();
        if (m_browse.connectorAvailable() && m_browsePreferences.connectorFabric() &&
            !loaders.contains("fabric"))
            loaders << "fabric";
        const auto target = m_navigation.route().value("packPath").toString();
        m_catalogue.setContext(target.isEmpty() ? QString("global") : target, versions, loaders,
                               m_navigation.route().value("author").toMap());
        if (m_browse.ready() && !m_browse.busy())
            m_catalogue.setInstalled(m_browse.inventory());
    };
    connect(&m_browse, &BrowseInstalls::changed, this, updateBrowseContext);
    connect(&m_browsePreferences, &BrowsePreferences::changed, this, updateBrowseContext);
    connect(&m_navigation, &NavigationHistory::changed, this, updateBrowseContext);
    auto updateReleaseFilters = [this] {
        const auto pack = m_browse.pack();
        // Wait for resolved project type before seeding loader defaults.
        const auto game =
            m_showcase.project().contains("type") ? pack.value("minecraft").toString() : QString{};
        const auto loaders = m_showcase.project().value("type") == "mod"
                                 ? m_browse.versionLoaders().split('|', Qt::SkipEmptyParts)
                                 : QStringList{};
        m_releaseFilters.setContext(
            m_navigation.route().value("packPath").toString() + '|' + m_showcase.requestKey(),
            m_showcase.versions(), game.isEmpty() ? QStringList{} : QStringList{game}, loaders);
    };
    connect(&m_showcase, &ProjectShowcase::changed, this, [this, updateReleaseFilters] {
        m_browse.rememberReleases(m_showcase.project(), m_showcase.versions());
        updateReleaseFilters();
    });
    connect(&m_browse, &BrowseInstalls::changed, this, updateReleaseFilters);
    setCurseforgeKey(qgetenv("CURSEFORGE_API_KEY"));
    updateBrowseContext();
    m_navigation.setLeaveGuard([this] { return !m_editor.dirty(); });
    connect(&m_browse, &BrowseInstalls::installRequested, &m_content, &ContentActions::installRelease);
    connect(&m_content, &ContentActions::changed, this, [this] {
        m_browse.setContext(m_workspace.pack(), m_content.inventory(),
                            m_content.inventory().isEmpty() && (m_content.busy() || m_workspace.busy()));
    });

    connect(&m_navigation, &NavigationHistory::changed, this, &DesktopSession::activateRoute);
    connect(&m_logs, &PackLogViewer::changed, this, &DesktopSession::activateRoute);
    connect(&m_workspace, &PackWorkspace::changed, this, [this] {
        if (!m_workspace.busy()) {
            m_configs.setContext(m_workspace.pack(), m_workspace.files());
            m_content.setContext(m_workspace.pack(), m_workspace.files(), true);
            m_changes.rebuild();
        }
        activateRoute();
    });
    connect(m_contentInstaller.get(), &ContentInstaller::progress, &m_content, &ContentActions::setProgress);
    connect(&m_content, &ContentActions::contentChanged, this, [this] { m_workspace.refresh(); });
    connect(&m_changes, &PackChangeMonitor::changed, this, [this] {
        if (m_content.busy() || m_workspace.busy()) {
            m_pendingScan = true;
            return;
        }
        m_workspace.refresh();
        m_worlds.refresh();
    });
    connect(&m_content, &ContentActions::changed, this, [this] {
        if (m_pendingScan && !m_content.busy() && !m_workspace.busy()) {
            m_pendingScan = false;
            m_workspace.refresh();
            m_worlds.refresh();
        }
    });
}
void DesktopSession::setPack(const QVariantMap &pack) {
    const auto root = pack.value("path").toString();
    const bool changedRoot = root != m_workspace.pack().value("path").toString();
    if (changedRoot) {
        m_editor.close();
        m_showcase.close();
        m_browser.setRoot(root);
        m_logs.setRoot(root);
        m_worlds.setRoot(root);
    }
    m_workspace.setPack(pack);
    if (changedRoot)
        m_content.setContext(pack, {});
    m_changes.setRoot(root);
    m_launch.setPack(pack);
    activateRoute();
}
void DesktopSession::activateRoute() {
    const auto route = m_navigation.route();
    m_browse.setTargetPath(route.value("packPath").toString());
    auto pack = m_workspace.pack();
    pack["catalogueLoader"] = m_browse.versionLoaders();
    const auto section = route.value("section").toString();
    if (route.value("page") == "showcase") {
        const auto target = route.value("packPath").toString();
        if (!target.isEmpty() && (target != pack.value("path").toString() || m_workspace.busy()))
            return;
        if (target.isEmpty())
            pack.clear();
        const auto project = route.value("project").toMap();
        if (!project.isEmpty()) {
            const auto key = project.value("provider").toString() + ':' + project.value("id").toString();
            if (m_showcase.requestKey() != key)
                m_showcase.openProject(pack, project);
        }
        const auto path = route.value("modPath").toString();
        if (!path.isEmpty() && m_showcase.filePath() != path) {
            bool pending = false;
            for (const auto &value : m_content.inventory()) {
                const auto row = value.toMap();
                if (row.value("relativePath") == path && row.value("pending").toBool() &&
                    !row.value("projectId").toString().isEmpty()) {
                    auto project = row;
                    project["id"] = row.value("projectId");
                    project["type"] = categoryType(row.value("category").toString());
                    const auto key =
                        project.value("provider").toString() + ':' + project.value("id").toString();
                    if (m_showcase.requestKey() != key)
                        m_showcase.openProject(pack, project);
                    pending = true;
                    break;
                }
            }
            if (pending)
                return;
            for (const auto &item : m_workspace.files()) {
                const auto file = item.toMap();
                if (file.value("relativePath") == path && file.value("category") != "configs") {
                    m_showcase.open(pack, file);
                    break;
                }
            }
        }
    } else if (!m_showcase.requestKey().isEmpty())
        m_showcase.close();
    if (route.value("page") != "workspace" || section != "configs" ||
        route.value("configPath").toString().isEmpty())
        m_editor.close();
    if (route.value("packPath") != pack.value("path") || route.value("page") != "workspace")
        return;
    if (section == "files" && route.value("folder").toString() != m_browser.folder())
        m_browser.open(route.value("folder").toString());
    if (section == "logs" && !route.value("logPath").toString().isEmpty() &&
        route.value("logPath").toString() != m_logs.selected())
        m_logs.select(route.value("logPath").toString());
    if (section != "configs" || route.value("configPath").toString().isEmpty() || m_workspace.busy())
        return;
    const auto path = route.value("configPath").toString();
    const auto row = m_configs.file(path);
    m_editor.open({pack.value("path").toString(), path, row.value("relatedModId").toString(),
                   row.value("relatedModTitle").toString(), row.value("icon").toString(),
                   row.value("association").toString()});
}
} // namespace studio
