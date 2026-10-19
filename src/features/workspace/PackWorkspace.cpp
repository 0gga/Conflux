#include "PackWorkspace.h"
#include <QFutureWatcher>
#include <QtConcurrent>
namespace studio {
PackWorkspace::PackWorkspace(std::shared_ptr<const IPackContentInspector> inspector, QObject *parent)
    : QObject(parent), m_inspector(std::move(inspector)) {}
void PackWorkspace::setPack(const QVariantMap &pack) {
    if (pack == m_pack)
        return;
    m_pack = pack;
    refresh();
}
void PackWorkspace::refresh() {
    const int generation = ++m_generation;
    m_content = {};
    if (m_pack.isEmpty()) {
        m_busy = false;
        emit changed();
        return;
    }
    m_busy = true;
    emit changed();
    auto *watcher = new QFutureWatcher<PackContentResult>(this);
    connect(watcher, &QFutureWatcher<PackContentResult>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_content = result;
        m_busy = false;
        emit changed();
    });
    const auto inspector = m_inspector;
    const auto pack = m_pack;
    watcher->setFuture(QtConcurrent::run([inspector, pack] { return inspector->inspect(pack); }));
}
} // namespace studio
