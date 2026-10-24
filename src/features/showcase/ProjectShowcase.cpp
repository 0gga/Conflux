#include "ProjectShowcase.h"
#include "DescriptionRenderer.h"
#include "domain/ContentTypes.h"
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QtConcurrent>
#include <algorithm>
namespace studio {
namespace {
Project cachedProject(const QVariantMap &value) {
    Project project;
    project.provider = value.value("provider").toString();
    project.id = value.value("id", value.value("projectId")).toString();
    project.title = value.value("title").toString();
    project.summary = value.value("summary").toString();
    project.icon = value.value("icon").toString();
    project.url = value.value("url", value.value("projectUrl")).toString();
    project.type = value.value("type", categoryType(value.value("category").toString())).toString();
    project.author = value.value("author").toString();
    project.authors = value.value("authors").toList();
    project.categories = value.value("categories").toStringList();
    const auto ids = value.value("sourceIds").toMap(), urls = value.value("sourceUrls").toMap(),
               authors = value.value("authorIds").toMap();
    for (auto it = ids.cbegin(); it != ids.cend(); ++it)
        project.sourceIds[it.key()] = it.value().toString();
    for (auto it = urls.cbegin(); it != urls.cend(); ++it)
        project.sourceUrls[it.key()] = it.value().toString();
    for (auto it = authors.cbegin(); it != authors.cend(); ++it)
        project.authorIds[it.key()] = it.value().toString();
    if (!project.url.isEmpty())
        project.sourceUrls[project.provider] = project.url;
    return project;
}
} // namespace
ProjectShowcase::ProjectShowcase(std::shared_ptr<IProjectShowcaseSource> source,
                                 std::shared_ptr<const IInstalledModIdentity> identity, QObject *parent)
    : QObject(parent), m_source(std::move(source)), m_changelogs(m_source, this),
      m_identity(std::move(identity)) {
    connect(&m_changelogs, &VersionChangelogs::selectionChanged, this, &ProjectShowcase::changed);
}
void ProjectShowcase::reset() {
    m_source->cancel();
    const QSignalBlocker blockChangelogs(&m_changelogs);
    ++m_generation;
    m_changelogs.clear();
    m_project.clear();
    m_versions.clear();
    m_description.clear();
    m_descriptionBlocks.clear();
    m_gallery.clear();
    m_notice.clear();
    m_filePath.clear();
    m_busy = false;
    m_detailsReady = false;
    m_descriptionPending = false;
    m_galleryPending = false;
    m_descriptionKey.clear();
    m_requestKey.clear();
}
void ProjectShowcase::close() {
    reset();
    emit changed();
}
void ProjectShowcase::open(const QVariantMap &pack, const QVariantMap &file) {
    reset();
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
    reset();
    m_project = project;
    m_filePath = project.value("relativePath").toString();
    m_requestKey = project.value("provider").toString() + ':' + project.value("id").toString();
    m_busy = true;
    const auto cached = cachedProject(project);
    static const QRegularExpression hash("^[a-fA-F0-9]{40}$");
    if (project.value("metadataResolved").toBool() &&
        project.value("metadataSha1")
                .toString()
                .compare(project.value("sha1").toString(), Qt::CaseInsensitive) == 0 &&
        hash.match(project.value("sha1").toString()).hasMatch() &&
        (cached.provider == "modrinth" || cached.provider == "curseforge") && !cached.id.isEmpty() &&
        cached.sourceIds.value(cached.provider) == cached.id) {
        presentProject(pack, cached, {}, m_generation);
        return;
    }
    const QPointer<ProjectShowcase> guard(this);
    const auto generation = m_generation;
    emit changed();
    if (!guard || generation != m_generation)
        return;
    InstalledModIdentity identity;
    const auto ids = project.value("sourceIds").toMap();
    for (auto it = ids.begin(); it != ids.end(); ++it)
        if (it.key() == "modrinth" || it.key() == "curseforge")
            identity.sourceIds[it.key()] = it.value().toString();
    if (identity.sourceIds.isEmpty() &&
        (project.value("provider") == "modrinth" || project.value("provider") == "curseforge"))
        identity.sourceIds[project.value("provider").toString()] = project.value("id").toString();
    resolve(identity, pack, generation);
}
void ProjectShowcase::applyInstalledMetadata(const QVariantMap &row) {
    const auto provider = m_project.value("provider").toString(), id = m_project.value("id").toString();
    if (!row.value("metadataResolved").toBool() ||
        row.value("metadataSha1").toString().compare(row.value("sha1").toString(), Qt::CaseInsensitive) !=
            0 ||
        id.isEmpty() || row.value("sourceIds").toMap().value(provider).toString() != id)
        return;
    auto updated = m_project;
    for (const auto &field : {"title", "icon", "author", "authors", "authorIds", "categories"})
        if (row.contains(field))
            updated[field] = row.value(field);
    if (updated != m_project) {
        m_project = std::move(updated);
        emit changed();
    }
}
void ProjectShowcase::selectChangelog(const QVariantMap &requested) {
    m_changelogs.select(requested);
}
void ProjectShowcase::resolve(const InstalledModIdentity &identity, const QVariantMap &pack, int generation) {
    const QPointer<ProjectShowcase> guard(this);
    m_source->resolveProgressive(
        identity,
        [guard, generation](QString description) {
            if (guard && guard->m_generation == generation)
                guard->presentDescription(description, true, generation);
        },
        [guard, generation, pack](Project project, QString error) {
            if (guard && guard->m_generation == generation)
                guard->presentProject(pack, std::move(project), error, generation);
        });
}
void ProjectShowcase::presentProject(const QVariantMap &pack, Project project, const QString &error,
                                     int generation) {
    if (generation != m_generation)
        return;
    const QPointer<ProjectShowcase> guard(this);
    if (project.authors.isEmpty() &&
        m_project.value("sourceIds").toMap().value(project.provider).toString() == project.id) {
        const auto seed = cachedProject(m_project);
        project.authors = seed.authors;
        project.author = seed.author;
        project.authorIds = seed.authorIds;
    }
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
    self->m_galleryPending = true;
    emit self->changed();
    if (!guard || generation != m_generation)
        return;
    self->m_source->loadProgressive(
        presentation, presentation.type == "mod" ? QString{} : pack.value("minecraft").toString(),
        presentation.type == "mod" ? QString{}
                                   : pack.value("catalogueLoader", pack.value("loader")).toString(),
        [guard, generation, markdown = presentation.provider == "modrinth"](QString description) {
            if (guard && guard->m_generation == generation)
                guard->presentDescription(description, markdown, generation);
        },
        [guard, generation](QStringList images, QStringList errors) {
            if (!guard || guard->m_generation != generation)
                return;
            guard->m_gallery = std::move(images);
            guard->m_galleryPending = false;
            if (!errors.isEmpty()) {
                if (!guard->m_notice.isEmpty())
                    guard->m_notice += '\n';
                guard->m_notice += errors.join('\n');
            }
            emit guard->changed();
        },
        [guard, generation, project = presentation](ProjectDetail detail) {
            if (!guard || guard->m_generation != generation)
                return;
            auto *self = guard.data();
            self->presentDescription(detail.description, project.provider == "modrinth", generation);
            if (!guard || guard->m_generation != generation)
                return;
            if (!detail.errors.isEmpty()) {
                if (!self->m_notice.isEmpty())
                    self->m_notice += '\n';
                self->m_notice += detail.errors.join('\n');
            }
            auto *prepared = new QFutureWatcher<PreparedVersions>(self);
            connect(prepared, &QFutureWatcherBase::finished, self, [guard, generation, prepared] {
                const auto page = prepared->result();
                prepared->deleteLater();
                if (!guard || guard->m_generation != generation)
                    return;
                guard->m_versions = page.groups;
                guard->m_changelogs.setGroups(page.changelogs);
                guard->m_detailsReady = true;
                guard->m_busy = guard->m_descriptionPending;
                emit guard->changed();
            });
            prepared->setFuture(QtConcurrent::run([releases = detail.releases, project] {
                auto groups = groupReleaseVariants(releases);
                for (int index = 0; index < groups.size(); ++index) {
                    auto group = groups.at(index).toMap();
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
                    groups[index] = group;
                }
                return PreparedVersions{groups, VersionChangelogs::groupReleases(releases)};
            }));
            emit self->changed();
        });
}
void ProjectShowcase::presentDescription(const QString &raw, bool markdown, int generation) {
    const auto key =
        QString::number(markdown) + ':' +
        QString::fromLatin1(QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (key == m_descriptionKey)
        return;
    m_descriptionKey = key;
    if (const auto *rendered = m_renderedDescriptions.object(key)) {
        m_description = rendered->html;
        m_descriptionBlocks = rendered->blocks;
        m_descriptionPending = false;
        m_busy = !m_detailsReady;
        emit changed();
        return;
    }
    m_descriptionPending = true;
    auto *renderer = new QFutureWatcher<RenderedDescription>(this);
    connect(renderer, &QFutureWatcherBase::finished, this,
            [this, renderer, key, generation, cacheable = raw.size() <= 1024 * 1024] {
                const auto rendered = renderer->result();
                renderer->deleteLater();
                if (cacheable)
                    m_renderedDescriptions.insert(key, new RenderedDescription(rendered));
                if (generation != m_generation || key != m_descriptionKey)
                    return;
                m_description = rendered.html;
                m_descriptionBlocks = rendered.blocks;
                m_descriptionPending = false;
                m_busy = !m_detailsReady;
                emit changed();
            });
    renderer->setFuture(QtConcurrent::run([raw, markdown] { return describeContent(raw, markdown); }));
}
} // namespace studio
