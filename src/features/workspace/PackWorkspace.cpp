#include "PackWorkspace.h"
#include <QFutureWatcher>
#include <QPointer>
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
    const QPointer<PackWorkspace> guard(this);
    const auto generation = m_generation + 1;
    refresh();
    if (!guard || generation != m_generation)
        return;
    if (wasBusy && m_busy)
        emit changed();
}
void PackWorkspace::refresh() {
    const auto generation = ++m_generation;
    if (m_pack.isEmpty()) {
        m_content = {};
        m_busy = false;
        emit changed();
        return;
    }
    const bool wasBusy = m_busy;
    const QPointer<PackWorkspace> guard(this);
    m_busy = true;
    if (!wasBusy)
        emit changed();
    if (!guard || generation != m_generation)
        return;
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
            if (!result.diagnostics.contains(message))
                result.diagnostics << message;
            const QVariant issue =
                QVariantMap{{"category", "inventory"}, {"severity", "error"}, {"message", message}};
            if (!result.issues.contains(issue))
                result.issues << issue;
            return result;
        }
    }));
}
} // namespace studio
