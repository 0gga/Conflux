#include "PackWorldList.h"
#include "features/content/ArchiveReader.h"
#include <QFutureWatcher>
#include <QtConcurrent>
namespace studio {
PackWorldList::PackWorldList(std::shared_ptr<const IPackFileReader> files, QObject *parent)
    : QObject(parent), m_files(std::move(files)) {}
void PackWorldList::setRoot(const QString &root) {
    if (root == m_root)
        return;
    m_root = root;
    refresh();
}
void PackWorldList::refresh() {
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
    watcher->setFuture(QtConcurrent::run([files, root] {
        QVariantList worlds;
        for (const auto &item : files->list(root, "saves")) {
            auto row = item.toMap();
            if (!row.value("directory").toBool())
                continue;
            const auto icon =
                files->read(root, row.value("relativePath").toString() + "/icon.png", 1024 * 1024);
            row["icon"] = rasterIcon(icon.bytes);
            worlds << row;
        }
        return worlds;
    }));
}
} // namespace studio
