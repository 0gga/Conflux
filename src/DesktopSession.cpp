#include "DesktopSession.h"
namespace studio {
DesktopSession::DesktopSession(QObject *parent, QString settingsFile, QString backupRoot)
    : QObject(parent), m_fileStore(std::make_shared<NativePackFileStore>(std::move(backupRoot))),
      m_navigation(this), m_input(m_navigation, this),
      m_warnings(std::make_shared<SettingsWarningPreferenceStore>(std::move(settingsFile)), this),
      m_workspace(defaultPackInspector(), this), m_browser(m_fileStore, this), m_logs(m_fileStore, this),
      m_worlds(m_fileStore, this),
      m_editor(std::make_shared<ConfigRepository>(m_fileStore, m_fileStore, defaultConfigCodecs()), this) {
    m_navigation.setLeaveGuard([this] { return !m_editor.dirty(); });
    connect(&m_navigation, &NavigationHistory::changed, this, &DesktopSession::activateRoute);
    connect(&m_workspace, &PackWorkspace::changed, this, &DesktopSession::activateRoute);
    connect(&m_logs, &PackLogViewer::changed, this, &DesktopSession::activateRoute);
}
void DesktopSession::setPack(const QVariantMap &pack) {
    const auto root = pack.value("path").toString();
    if (root != m_workspace.pack().value("path").toString()) {
        m_editor.close();
        m_browser.setRoot(root);
        m_logs.setRoot(root);
        m_worlds.setRoot(root);
    }
    m_workspace.setPack(pack);
    activateRoute();
}
void DesktopSession::activateRoute() {
    const auto route = m_navigation.route();
    const auto pack = m_workspace.pack();
    const auto section = route.value("section").toString();
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
    QVariantMap row;
    for (const auto &file : m_workspace.files())
        if (file.toMap().value("relativePath") == path) {
            row = file.toMap();
            break;
        }
    m_editor.open({pack.value("path").toString(), path, row.value("relatedModId").toString(),
                   row.value("relatedModTitle").toString(), row.value("icon").toString(),
                   row.value("association").toString()});
}
} // namespace studio
