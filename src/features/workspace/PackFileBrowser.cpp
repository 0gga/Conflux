#include "PackFileBrowser.h"
#include <QFutureWatcher>
#include <QtConcurrent>
namespace studio {
PackFileBrowser::PackFileBrowser(std::shared_ptr<const IPackFileReader> files, QObject *parent)
    : QObject(parent), m_files(std::move(files)) {}
void PackFileBrowser::setRoot(const QString &root) {
    if (m_root == root)
        return;
    m_root = root;
    open();
}
void PackFileBrowser::open(const QString &relative) {
    m_folder = relative;
    const int generation = ++m_generation;
    m_busy = true;
    emit changed();
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher, generation] {
        const auto entries = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_entries = entries;
        m_busy = false;
        emit changed();
    });
    const auto files = m_files;
    const auto root = m_root;
    watcher->setFuture(QtConcurrent::run([files, root, relative] { return files->list(root, relative); }));
}
void PackFileBrowser::up() {
    open(m_folder.contains('/') ? m_folder.section('/', 0, -2) : QString{});
}
} // namespace studio
