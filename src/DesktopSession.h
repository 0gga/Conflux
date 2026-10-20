#pragma once
#include "features/config/ConfigEditor.h"
#include "features/content/ContentActions.h"
#include "features/launch/WorldLaunch.h"
#include "features/navigation/NavigationHistory.h"
#include "features/navigation/NavigationInput.h"
#include "features/preferences/WarningPreferences.h"
#include "features/showcase/ProjectShowcase.h"
#include "features/workspace/PackFileBrowser.h"
#include "features/workspace/PackLogViewer.h"
#include "features/workspace/PackWorkspace.h"
#include "features/workspace/PackWorldList.h"
#include "platform/ContentInstaller.h"
#include "platform/NativeContentStore.h"
#include "platform/PackChangeMonitor.h"
namespace studio {
// Composition root: wires independent feature models; it contains no parsing,
// warning rules, filesystem policy or view layout.
class DesktopSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(studio::NavigationHistory *navigation READ navigation CONSTANT)
    Q_PROPERTY(studio::WarningPreferences *warnings READ warnings CONSTANT)
    Q_PROPERTY(studio::PackWorkspace *workspace READ workspace CONSTANT)
    Q_PROPERTY(studio::PackFileBrowser *files READ files CONSTANT)
    Q_PROPERTY(studio::PackLogViewer *logs READ logs CONSTANT)
    Q_PROPERTY(studio::PackWorldList *worlds READ worlds CONSTANT)
    Q_PROPERTY(studio::ConfigEditor *editor READ editor CONSTANT)
    Q_PROPERTY(studio::WorldLaunch *launch READ launch CONSTANT)
    Q_PROPERTY(studio::ProjectShowcase *showcase READ showcase CONSTANT)
    Q_PROPERTY(studio::ContentActions *content READ content CONSTANT)
  public:
    explicit DesktopSession(QObject *parent = nullptr, QString settingsFile = {}, QString backupRoot = {});
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
    WorldLaunch *launch() {
        return &m_launch;
    }
    ProjectShowcase *showcase() {
        return &m_showcase;
    }
    ContentActions *content() {
        return &m_content;
    }
    void setCurseforgeKey(const QByteArray &key) {
        m_showcase.setCurseforgeKey(key);
        m_content.setCurseforgeKey(key);
    }
    void setPack(const QVariantMap &);
    void attachWindow(QObject &window) {
        m_input.attach(window);
    }

  private:
    std::shared_ptr<const NativePackFileStore> m_fileStore;
    std::shared_ptr<const NativeContentStore> m_contentFiles;
    std::shared_ptr<ContentInstaller> m_contentInstaller;
    NavigationHistory m_navigation;
    NavigationInput m_input;
    WarningPreferences m_warnings;
    PackWorkspace m_workspace;
    PackFileBrowser m_browser;
    PackLogViewer m_logs;
    PackWorldList m_worlds;
    ConfigEditor m_editor;
    WorldLaunch m_launch;
    ProjectShowcase m_showcase;
    ContentActions m_content;
    PackChangeMonitor m_changes;
    bool m_pendingScan = false;
    void activateRoute();
};
} // namespace studio
