#include "ProjectShowcase.h"
#include "DescriptionRenderer.h"
#include "domain/ContentTypes.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent>
#include <algorithm>
namespace studio {
ProjectShowcase::ProjectShowcase(std::shared_ptr<IProjectShowcaseSource> source,
                                 std::shared_ptr<const IInstalledModIdentity> identity, QObject *parent)
    : QObject(parent), m_source(std::move(source)), m_identity(std::move(identity)) {}
void ProjectShowcase::close() {
    ++m_generation;
    ++m_changelogGeneration;
    m_changelogVersions.clear();
    m_changelogBlocks.clear();
    m_changelogRelease.clear();
    m_changelogCache.clear();
    m_releaseChangelogs.clear();
    m_changelogNotice.clear();
    m_changelogBusy = false;
    m_project.clear();
    m_versions.clear();
    m_description.clear();
    m_descriptionBlocks.clear();
    m_gallery.clear();
    m_notice.clear();
    m_filePath.clear();
    m_busy = false;
    m_requestKey.clear();
    emit changed();
}
void ProjectShowcase::open(const QVariantMap &pack, const QVariantMap &file) {
    close();
    m_filePath = file.value("relativePath").toString();
    m_requestKey = m_filePath;
    m_project = {{"title", file.value("title")},
                 {"icon", file.value("icon")},
                 {"type", categoryType(file.value("category").toString())}};
    m_busy = true;
    const int generation = m_generation;
    emit changed();
    auto *watcher = new QFutureWatcher<InstalledModIdentity>(this);
    connect(watcher, &QFutureWatcher<InstalledModIdentity>::finished, this,
            [this, watcher, generation, pack] {
                const auto identity = watcher->result();
                watcher->deleteLater();
                if (generation != m_generation)
                    return;
                if (!identity.error.isEmpty()) {
                    m_busy = false;
                    m_notice = identity.error;
                    emit changed();
                    return;
                }
                resolve(identity, pack, generation);
            });
    const auto identity = m_identity;
    watcher->setFuture(QtConcurrent::run([identity, pack, file] { return identity->inspect(pack, file); }));
}
void ProjectShowcase::openProject(const QVariantMap &pack, const QVariantMap &project) {
    close();
    m_project = project;
    m_requestKey = project.value("provider").toString() + ':' + project.value("id").toString();
    m_busy = true;
    emit changed();
    InstalledModIdentity identity;
    const auto ids = project.value("sourceIds").toMap();
    for (auto it = ids.begin(); it != ids.end(); ++it)
        if (it.key() == "modrinth" || it.key() == "curseforge")
            identity.sourceIds[it.key()] = it.value().toString();
    if (identity.sourceIds.isEmpty() &&
        (project.value("provider") == "modrinth" || project.value("provider") == "curseforge"))
        identity.sourceIds[project.value("provider").toString()] = project.value("id").toString();
    resolve(identity, pack, m_generation);
}
void ProjectShowcase::selectChangelog(const QVariantMap &requested) {
    QVariantMap selected;
    for (const auto &value : m_changelogVersions) {
        const auto row = value.toMap();
        if (row.value("provider") == requested.value("provider") &&
            row.value("id") == requested.value("id")) {
            selected = row;
            break;
        }
    }
    if (selected.isEmpty())
        return;
    const auto key = selected.value("provider").toString() + ':' + selected.value("id").toString();
    if (m_changelogRelease.value("provider") == selected.value("provider") &&
        m_changelogRelease.value("id") == selected.value("id"))
        return;
    const auto generation = ++m_changelogGeneration;
    m_changelogRelease = selected;
    m_changelogBlocks.clear();
    m_changelogNotice.clear();
    if (m_changelogCache.contains(key)) {
        m_changelogBlocks = m_changelogCache.value(key);
        m_changelogBusy = false;
        emit changed();
        return;
    }
    m_changelogBusy = true;
    emit changed();
    const QPointer<ProjectShowcase> guard(this);
    auto release = releaseFromVariant(selected);
    release.changelog = m_releaseChangelogs.value(key);
    m_source->loadChangelog(release, [guard, generation, key, selected](QString raw, QString error) {
        if (!guard || guard->m_changelogGeneration != generation)
            return;
        if (!error.isEmpty()) {
            guard->m_changelogNotice = error;
            guard->m_changelogBusy = false;
            emit guard->changed();
            return;
        }
        auto *watcher = new QFutureWatcher<QVariantList>(guard);
        connect(watcher, &QFutureWatcher<QVariantList>::finished, guard, [guard, watcher, generation, key] {
            const auto blocks = watcher->result();
            watcher->deleteLater();
            if (!guard || guard->m_changelogGeneration != generation)
                return;
            guard->m_changelogBlocks = blocks;
            guard->m_changelogBusy = false;
            if (guard->m_changelogCache.size() >= 32)
                guard->m_changelogCache.erase(guard->m_changelogCache.begin());
            guard->m_changelogCache.insert(key, blocks);
            emit guard->changed();
        });
        watcher->setFuture(QtConcurrent::run([raw, selected] {
            return describeContent(raw, selected.value("provider") == "modrinth").blocks;
        }));
    });
}
void ProjectShowcase::resolve(const InstalledModIdentity &identity, const QVariantMap &pack, int generation) {
    const QPointer<ProjectShowcase> guard(this);
    m_source->resolve(identity, [guard, generation, pack](Project project, QString error) {
        if (!guard || guard->m_generation != generation)
            return;
        auto *self = guard.data();
        self->m_notice = error;
        if (project.id.isEmpty()) {
            self->m_busy = false;
            if (self->m_notice.isEmpty())
                self->m_notice = "No verified online project was found for this file. Local or unpublished "
                                 "mods may not have a showcase.";
            emit self->changed();
            return;
        }
        auto presentation = project;
        if (!self->m_project.value("type").toString().isEmpty())
            presentation.type = self->m_project.value("type").toString();
        self->m_project = presentation.toVariant();
        emit self->changed();
        self->m_source->load(
            presentation, presentation.type == "mod" ? QString{} : pack.value("minecraft").toString(),
            presentation.type == "mod" ? QString{}
                                       : pack.value("catalogueLoader", pack.value("loader")).toString(),
            [guard, generation, project = presentation](ProjectDetail detail) {
                if (!guard || guard->m_generation != generation)
                    return;
                auto *self = guard.data();
                auto *renderer = new QFutureWatcher<RenderedDescription>(self);
                connect(renderer, &QFutureWatcher<RenderedDescription>::finished, self,
                        [guard, renderer, generation] {
                            const auto rendered = renderer->result();
                            renderer->deleteLater();
                            if (!guard || guard->m_generation != generation)
                                return;
                            guard->m_description = rendered.html;
                            guard->m_descriptionBlocks = rendered.blocks;
                            guard->m_busy = false;
                            emit guard->changed();
                        });
                renderer->setFuture(
                    QtConcurrent::run([raw = detail.description, markdown = project.provider == "modrinth"] {
                        return describeContent(raw, markdown);
                    }));
                self->m_gallery = detail.images;
                if (!detail.errors.isEmpty()) {
                    if (!self->m_notice.isEmpty())
                        self->m_notice += '\n';
                    self->m_notice += detail.errors.join('\n');
                }
                self->m_versions = groupReleaseVariants(detail.releases);
                for (const auto &release : detail.releases) {
                    self->m_changelogVersions << release.toVariant();
                    self->m_releaseChangelogs[release.provider + ':' + release.id] = release.changelog;
                }
                std::stable_sort(self->m_changelogVersions.begin(), self->m_changelogVersions.end(),
                                 [](const QVariant &a, const QVariant &b) {
                                     return a.toMap().value("published").toString() >
                                            b.toMap().value("published").toString();
                                 });
                for (int index = 0; index < self->m_versions.size(); ++index) {
                    auto group = self->m_versions.at(index).toMap();
                    auto variants = group.value("variants").toList();
                    for (int variantIndex = 0; variantIndex < variants.size(); ++variantIndex) {
                        auto variant = variants.at(variantIndex).toMap();
                        variant["pageUrl"] = variant.value("provider") == "modrinth"
                                                 ? "https://modrinth.com/" + project.type + "/" +
                                                       variant.value("projectId").toString() + "/version/" +
                                                       variant.value("id").toString()
                                                 : project.sourceUrls.value("curseforge") + "/files/" +
                                                       variant.value("id").toString();
                        variants[variantIndex] = variant;
                    }
                    group["variants"] = variants;
                    self->m_versions[index] = group;
                }
                emit self->changed();
            });
    });
}
} // namespace studio
