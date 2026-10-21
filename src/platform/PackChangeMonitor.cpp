#include "PackChangeMonitor.h"
#include "platform/PackFileStore.h"
#include <QDir>
#include <QSet>
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
        m_debounce.stop();
        m_root = root;
        rebuild();
    }
}
void PackChangeMonitor::rebuild() {
    QStringList paths;
    const auto root = NativePackFileStore::resolve(m_root, {});
    if (!root.isEmpty())
        paths << root;
    for (const auto &category :
         QStringList{"mods", "resourcepacks", "shaderpacks", "datapacks", "config", "saves"}) {
        const auto dir = root.isEmpty() ? QString{} : NativePackFileStore::resolve(root, category);
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
    const auto watched = m_watcher.files() + m_watcher.directories();
    const QSet<QString> desired(paths.cbegin(), paths.cend());
    const QSet<QString> current(watched.cbegin(), watched.cend());
    QStringList removed, added;
    for (const auto &path : current)
        if (!desired.contains(path))
            removed << path;
    for (const auto &path : desired)
        if (!current.contains(path))
            added << path;
    if (!removed.isEmpty())
        m_watcher.removePaths(removed);
    if (!added.isEmpty())
        m_watcher.addPaths(added);
}
} // namespace studio
