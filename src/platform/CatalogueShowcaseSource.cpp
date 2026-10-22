#include "CatalogueShowcaseSource.h"
#include "ReviewedProjectIdentities.h"
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
namespace studio {
CatalogueShowcaseSource::CatalogueShowcaseSource(QObject *parent, QUrl modrinthApi, QUrl curseforgeApi,
                                                 bool allowLoopbackImages)
    : QObject(parent), m_client(this, std::move(modrinthApi), std::move(curseforgeApi)),
      m_images(this, allowLoopbackImages) {
    m_client.setReadCacheLifetime(60000);
    m_client.setCurseforgeKey(qgetenv("CURSEFORGE_API_KEY"));
    connect(&m_client, &CatalogueClient::projectResolved, this,
            [this](int generation, const Project &project, const QString &error) {
                const auto callback = m_resolves.take(generation);
                if (callback)
                    callback(project, error);
            });
    connect(&m_client, &CatalogueClient::detailFinished, this, [this](int generation, ProjectDetail detail) {
        if (generation != m_activeDetail)
            return;
        const auto images = detail.images;
        detail.images.clear();
        const auto callback = m_details.take(generation);
        if (callback)
            callback(detail);
        if (generation == m_activeDetail)
            m_images.load(images, generation);
    });
    connect(&m_client, &CatalogueClient::descriptionReady, this,
            [this](int generation, const QString &description) {
                if (generation == m_activeResolve && m_resolveDescription)
                    m_resolveDescription(description);
                if (generation == m_activeDetail && m_description)
                    m_description(description);
            });
    connect(&m_images, &GalleryImages::ready, this,
            [this](int generation, const QStringList &images, const QStringList &errors) {
                if (generation == m_activeDetail && m_gallery)
                    m_gallery(images, errors);
            });
}
void CatalogueShowcaseSource::loadChangelog(const Release &release, ChangelogCallback callback) {
    m_client.loadChangelog(release, std::move(callback));
}
void CatalogueShowcaseSource::resolve(const InstalledModIdentity &identity, ResolveCallback callback) {
    resolveProgressive(identity, {}, std::move(callback));
}
void CatalogueShowcaseSource::resolveProgressive(const InstalledModIdentity &identity,
                                                 DescriptionCallback description, ResolveCallback callback) {
    const int generation = ++m_generation;
    m_activeResolve = generation;
    m_resolveDescription = std::move(description);
    m_resolves[generation] = [this, callback = std::move(callback)](Project project, QString error) {
        const auto expanded = ReviewedProjectIdentities::expand(project.sourceIds);
        if (!project.id.isEmpty() && expanded.size() > project.sourceIds.size()) {
            const int second = ++m_generation;
            m_activeResolve = second;
            m_resolves[second] = [callback, project, error](Project merged, QString secondaryError) {
                QStringList errors;
                if (!error.isEmpty())
                    errors << error;
                if (!secondaryError.isEmpty())
                    errors << secondaryError;
                callback(merged.id.isEmpty() ? project : merged, errors.join('\n'));
            };
            m_client.resolveProject({}, expanded, second);
        } else
            callback(project, error);
    };
    m_client.resolveProject(identity.sha1, ReviewedProjectIdentities::expand(identity.sourceIds), generation);
}
void CatalogueShowcaseSource::load(const Project &project, const QString &game, const QString &loader,
                                   DetailCallback callback) {
    // Legacy consumers receive a complete, validated gallery.
    auto detail = std::make_shared<ProjectDetail>();
    loadProgressive(
        project, game, loader, {},
        [detail, callback](QStringList images, QStringList errors) {
            detail->images = images;
            detail->errors += errors;
            callback(*detail);
        },
        [detail](ProjectDetail value) { *detail = std::move(value); });
}
void CatalogueShowcaseSource::cancel() {
    m_activeDetail = ++m_generation;
    m_resolves.clear();
    m_details.clear();
    m_description = {};
    m_activeResolve = 0;
    m_resolveDescription = {};
    m_gallery = {};
    m_images.cancel();
}
void CatalogueShowcaseSource::loadProgressive(const Project &project, const QString &game,
                                              const QString &loader, DescriptionCallback description,
                                              GalleryCallback gallery, DetailCallback callback) {
    cancel();
    const int generation = ++m_generation;
    m_activeDetail = generation;
    m_description = std::move(description);
    m_gallery = std::move(gallery);
    m_details[generation] = std::move(callback);
    m_client.loadProject(project, game, loader, generation, true);
}
} // namespace studio
