#include "InstalledContentVersions.h"
#include <QDebug>
#include <QPointer>
#include <exception>
namespace studio {
InstalledContentVersions::InstalledContentVersions(std::shared_ptr<IContentVersionSource> source,
                                                   QObject *parent)
    : QObject(parent), m_source(std::move(source)) {}
void InstalledContentVersions::reset() {
    ++m_generation;
    m_target.clear();
    m_versions.clear();
    m_groups.clear();
    m_error.clear();
    m_busy = false;
    const auto source = m_source;
    try {
        source->cancelVersions();
    } catch (...) {
        // Obsolete replies remain generation-guarded even if the adapter cannot abort.
        qInfo() << "Could not cancel an obsolete content version lookup.";
    }
}
void InstalledContentVersions::open(QVariantMap target, QString game, QString loaders) {
    const auto generation = m_generation + 1;
    const QPointer<InstalledContentVersions> guard(this);
    const auto source = m_source;
    reset();
    if (!guard || generation != m_generation)
        return;
    m_target = target;
    m_busy = !target.isEmpty();
    emit changed();
    if (!guard || generation != m_generation || !m_busy)
        return;
    auto complete = [guard, generation](QVector<Release> releases, QString error) {
        if (!guard || generation != guard->m_generation || !guard->m_busy)
            return;
        QVariantList versions;
        versions.reserve(releases.size());
        for (const auto &release : releases)
            versions << release.toVariant();
        auto groups = groupReleaseVariants(releases);
        guard->m_versions = std::move(versions);
        guard->m_groups = std::move(groups);
        guard->m_error = std::move(error);
        guard->m_busy = false;
        emit guard->changed();
    };
    try {
        source->versions(target, game, loaders, complete);
    } catch (const std::exception &error) {
        complete({}, QString("Could not load content versions: ") + QString::fromUtf8(error.what()));
    } catch (...) {
        complete({}, "Could not load content versions.");
    }
}
} // namespace studio
