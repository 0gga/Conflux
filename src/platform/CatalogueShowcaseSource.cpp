#include "CatalogueShowcaseSource.h"
#include "ReviewedProjectIdentities.h"
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
namespace studio {
CatalogueShowcaseSource::CatalogueShowcaseSource(QObject *parent)
    : QObject(parent), m_client(this), m_images(this) {
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
        m_loading[generation] = detail;
        m_images.load(detail.images, generation);
    });
    connect(&m_images, &GalleryImages::ready, this,
            [this](int generation, const QStringList &images, const QStringList &errors) {
                auto detail = m_loading.take(generation);
                detail.images = images;
                detail.errors += errors;
                const auto callback = m_details.take(generation);
                if (callback)
                    callback(detail);
            });
}
void CatalogueShowcaseSource::resolve(const InstalledModIdentity &identity, ResolveCallback callback) {
    const int generation = ++m_generation;
    m_resolves[generation] = [this, callback = std::move(callback)](Project project, QString error) {
        const auto expanded = ReviewedProjectIdentities::expand(project.sourceIds);
        if (!project.id.isEmpty() && expanded.size() > project.sourceIds.size()) {
            const int second = ++m_generation;
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
    const int generation = ++m_generation;
    m_activeDetail = generation;
    m_loading.clear();
    m_details.clear();
    m_details[generation] = std::move(callback);
    m_client.loadProject(project, game, loader, generation);
}
} // namespace studio
