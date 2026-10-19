#pragma once
#include "features/config/ConfigEditor.h"
#include "features/navigation/NavigationHistory.h"
#include "features/navigation/NavigationInput.h"
#include "features/preferences/WarningPreferences.h"
#include "features/workspace/PackFileBrowser.h"
#include "features/workspace/PackLogViewer.h"
#include "features/workspace/PackWorkspace.h"
#include "features/workspace/PackWorldList.h"
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
    void setPack(const QVariantMap &);
    void attachWindow(QObject &window) {
        m_input.attach(window);
    }

  private:
    std::shared_ptr<const NativePackFileStore> m_fileStore;
    NavigationHistory m_navigation;
    NavigationInput m_input;
    WarningPreferences m_warnings;
    PackWorkspace m_workspace;
    PackFileBrowser m_browser;
    PackLogViewer m_logs;
    PackWorldList m_worlds;
    ConfigEditor m_editor;
    void activateRoute();
};
} // namespace studio
