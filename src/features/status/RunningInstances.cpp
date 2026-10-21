#include "RunningInstances.h"
#include <QFutureWatcher>
#include <QtConcurrent>
namespace studio {
RunningInstances::RunningInstances(std::shared_ptr<const IInstanceProcessSource> source, QObject *parent)
    : QObject(parent), m_source(std::move(source)) {
    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, &RunningInstances::refresh);
    m_timer.start();
}
void RunningInstances::setPacks(const QVariantList &packs) {
    if (packs == m_packs)
        return;
    m_packs = packs;
    ++m_generation;
    refresh();
}
void RunningInstances::refresh() {
    if (m_busy)
        return;
    m_busy = true;
    const auto packs = m_packs;
    const auto generation = m_generation;
    const auto source = m_source;
    auto *watcher = new QFutureWatcher<RunningInstanceResult>(this);
    connect(watcher, &QFutureWatcher<RunningInstanceResult>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        if (generation != m_generation) {
            refresh();
            return;
        }
        if (result.error == m_error && (!result.error.isEmpty() || result.instances == m_instances))
            return;
        if (result.error.isEmpty())
            m_instances = result.instances;
        m_error = result.error;
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run([source, packs] {
        try {
            return source->scan(packs);
        } catch (...) {
            return RunningInstanceResult{{}, "Running instance status is unavailable."};
        }
    }));
}
} // namespace studio
