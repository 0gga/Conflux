#include "PackWorkspace.h"
#include <QFutureWatcher>
#include <QtConcurrent>
#include <exception>
namespace studio {
PackWorkspace::PackWorkspace(std::shared_ptr<const IPackContentInspector> inspector, QObject *parent)
    : QObject(parent), m_inspector(std::move(inspector)) {}
void PackWorkspace::setPack(const QVariantMap &pack) {
    if (pack == m_pack)
        return;
    const bool wasBusy = m_busy;
    const bool changedRoot = pack.value("path") != m_pack.value("path");
    m_pack = pack;
    if (changedRoot)
        m_content = {};
    refresh();
    if (wasBusy && m_busy)
        emit changed();
}
void PackWorkspace::refresh() {
    ++m_generation;
    if (m_pack.isEmpty()) {
        m_content = {};
        m_busy = false;
        emit changed();
        return;
    }
    const bool wasBusy = m_busy;
    m_busy = true;
    if (!wasBusy)
        emit changed();
    if (!m_scanRunning)
        startScan();
}
void PackWorkspace::startScan() {
    m_scanRunning = true;
    const int generation = m_generation;
    auto *watcher = new QFutureWatcher<PackContentResult>(this);
    connect(watcher, &QFutureWatcher<PackContentResult>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_scanRunning = false;
        if (generation != m_generation) {
            if (!m_pack.isEmpty())
                startScan();
            return;
        }
        m_content = result;
        m_busy = false;
        emit changed();
    });
    const auto inspector = m_inspector;
    const auto pack = m_pack;
    const auto previous = m_content;
    watcher->setFuture(QtConcurrent::run([inspector, pack, previous] {
        try {
            return inspector->inspect(pack);
        } catch (...) {
            auto result = previous;
            const QString message = "Could not inspect this instance. Refresh to retry.";
            result.diagnostics << message;
            result.issues << QVariantMap{
                {"category", "inventory"}, {"severity", "error"}, {"message", message}};
            return result;
        }
    }));
}
} // namespace studio
