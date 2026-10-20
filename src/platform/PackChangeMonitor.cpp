#include "PackChangeMonitor.h"
#include "platform/PackFileStore.h"
#include <QDir>
namespace studio {
PackChangeMonitor::PackChangeMonitor(QObject *parent) : QObject(parent), m_watcher(this), m_debounce(this) {
    m_debounce.setInterval(450);
    m_debounce.setSingleShot(true);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        rebuild();
        emit changed();
    });
}
void PackChangeMonitor::setRoot(const QString &root) {
    if (root != m_root) {
        m_root = root;
        rebuild();
    }
}
void PackChangeMonitor::rebuild() {
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    QStringList paths;
    const auto root = NativePackFileStore::resolve(m_root, {});
    if (root.isEmpty())
        return;
    paths << root;
    for (const auto &category : QStringList{"mods", "resourcepacks", "shaderpacks", "config", "saves"}) {
        const auto dir = NativePackFileStore::resolve(root, category);
        if (dir.isEmpty())
            continue;
        paths << dir;
        // The parent catches additions/removals; files catch in-place edits.
        for (const auto &entry :
             QDir(dir).entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot)) {
            if (paths.size() >= 1500)
                break;
            paths << entry.absoluteFilePath();
        }
    }
    m_watcher.addPaths(paths);
}
} // namespace studio
