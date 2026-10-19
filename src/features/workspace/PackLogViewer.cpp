#include "PackLogViewer.h"
#include <QFutureWatcher>
#include <QtConcurrent>
namespace studio {
PackLogViewer::PackLogViewer(std::shared_ptr<const IPackFileReader> files, QObject *parent)
    : QObject(parent), m_files(std::move(files)) {}
void PackLogViewer::setRoot(const QString &root) {
    if (root == m_root)
        return;
    ++m_generation;
    m_root = root;
    m_entries.clear();
    m_selected.clear();
    m_text.clear();
    m_error.clear();
    refresh();
}
void PackLogViewer::refresh() {
    const int generation = ++m_generation;
    m_busy = true;
    emit changed();
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher, generation] {
        auto entries = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_entries.clear();
        for (const auto &item : entries) {
            const auto row = item.toMap();
            if (!row.value("directory").toBool() && (row.value("title").toString().endsWith(".log") ||
                                                     row.value("title").toString().endsWith(".txt")))
                m_entries << row;
        }
        m_busy = false;
        emit changed();
        if (m_selected.isEmpty() && !m_entries.isEmpty()) {
            auto selected = m_entries.first().toMap().value("relativePath").toString();
            for (const auto &entry : m_entries)
                if (entry.toMap().value("title") == "latest.log")
                    selected = entry.toMap().value("relativePath").toString();
            select(selected);
        } else if (!m_selected.isEmpty())
            select(m_selected);
    });
    const auto files = m_files;
    const auto root = m_root;
    watcher->setFuture(QtConcurrent::run([files, root] { return files->list(root, "logs"); }));
}
void PackLogViewer::select(const QString &relative) {
    if (m_selected == relative && m_busy)
        return;
    bool found = false;
    for (const auto &item : m_entries)
        if (item.toMap().value("relativePath") == relative)
            found = true;
    if (!found)
        return;
    const int generation = ++m_generation;
    m_selected = relative;
    m_busy = true;
    m_error.clear();
    emit changed();
    auto *watcher = new QFutureWatcher<FileReadResult>(this);
    connect(watcher, &QFutureWatcher<FileReadResult>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_busy = false;
        m_error = result.error;
        m_text = (result.truncated ? "[Showing the last 2 MiB of this log]\n" : "") +
                 QString::fromUtf8(result.bytes);
        emit changed();
    });
    const auto files = m_files;
    const auto root = m_root;
    watcher->setFuture(QtConcurrent::run(
        [files, root, relative] { return files->read(root, relative, 2 * 1024 * 1024, true); }));
}
} // namespace studio
