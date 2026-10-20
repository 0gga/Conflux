#include "DesktopSession.h"
#include "features/preferences/ContentPreferences.h"
#include "platform/CatalogueShowcaseSource.h"
#include "platform/ConfigOwnershipStore.h"
#include "platform/InstalledModIdentity.h"
#include "platform/WorldLaunchAdapters.h"
#include "services/ContentCatalogue.h"
namespace studio {
DesktopSession::DesktopSession(QObject *parent, QString settingsFile, QString backupRoot)
    : QObject(parent), m_fileStore(std::make_shared<NativePackFileStore>(backupRoot)),
      m_contentFiles(
          std::make_shared<NativeContentStore>(std::make_shared<NativeInstalledModIdentity>(m_fileStore),
                                               backupRoot.isEmpty() ? QString{} : backupRoot + "/content")),
      m_contentInstaller(std::make_shared<ContentInstaller>(m_contentFiles)), m_navigation(this),
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
      m_changes(this) {
    m_navigation.setLeaveGuard([this] { return !m_editor.dirty(); });
    connect(&m_navigation, &NavigationHistory::changed, this, &DesktopSession::activateRoute);
    connect(&m_logs, &PackLogViewer::changed, this, &DesktopSession::activateRoute);
    connect(&m_workspace, &PackWorkspace::changed, this, [this] {
        if (!m_workspace.busy()) {
            m_configs.setContext(m_workspace.pack(), m_workspace.files());
            m_content.setContext(m_workspace.pack(), m_workspace.files());
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
        m_content.refresh(false);
        m_worlds.refresh();
    });
    connect(&m_content, &ContentActions::changed, this, [this] {
        if (m_pendingScan && !m_content.busy() && !m_workspace.busy()) {
            m_pendingScan = false;
            m_workspace.refresh();
            m_content.refresh(false);
        }
    });
}
void DesktopSession::setPack(const QVariantMap &pack) {
    const auto root = pack.value("path").toString();
    if (root != m_workspace.pack().value("path").toString()) {
        m_editor.close();
        m_showcase.close();
        m_browser.setRoot(root);
        m_logs.setRoot(root);
        m_worlds.setRoot(root);
    }
    m_workspace.setPack(pack);
    m_changes.setRoot(root);
    m_launch.setPack(pack);
    activateRoute();
}
void DesktopSession::activateRoute() {
    const auto route = m_navigation.route();
    const auto pack = m_workspace.pack();
    const auto section = route.value("section").toString();
    if (route.value("page") == "showcase") {
        if (route.value("packPath").toString() != pack.value("path").toString() || m_workspace.busy())
            return;
        const auto project = route.value("project").toMap();
        if (!project.isEmpty()) {
            const auto key = project.value("provider").toString() + ':' + project.value("id").toString();
            if (m_showcase.requestKey() != key)
                m_showcase.openProject(pack, project);
        }
        const auto path = route.value("modPath").toString();
        if (!path.isEmpty() && m_showcase.filePath() != path) {
            for (const auto &item : m_workspace.files()) {
                const auto file = item.toMap();
                if (file.value("relativePath") == path && file.value("category") == "mods") {
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
