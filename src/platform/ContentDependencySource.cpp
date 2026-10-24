#include "ContentDependencySource.h"
#include "domain/ContentCompatibility.h"
namespace studio {
ContentDependencySource::ContentDependencySource(QObject *parent, QUrl modrinthApi, QUrl curseforgeApi)
    : QObject(parent), m_modrinth(this, std::move(modrinthApi)),
      m_curseforge(this, std::move(curseforgeApi)) {}
void ContentDependencySource::cancel() {
    m_modrinth.cancelPendingReads();
    if (m_curseforge.busy())
        m_curseforge.cancel();
}
void ContentDependencySource::resolve(const QVariantMap &pack, const QVariantList &rows,
                                      const ContentDependency &reference, const QStringList &channels,
                                      Callback callback) {
    if (reference.provider == "modrinth") {
        m_modrinth.modrinthDependency(
            reference.projectId, reference.versionId, pack.value("minecraft").toString(),
            compatibleLoaders(pack.value("loader").toString(), rows), channels,
            [channels, callback = std::move(callback)](Release release, QString error) mutable {
                ContentInstallRequest request;
                request.release = std::move(release);
                request.title = request.release.title;
                if (request.title.isEmpty())
                    request.title = request.release.projectId;
                request.sourceIds = {{"modrinth", request.release.projectId}};
                request.allowedChannels = channels;
                callback(std::move(request), error);
            });
        return;
    }
    if (reference.provider == "curseforge") {
        ContentInstallRequest request;
        request.release.provider = "curseforge";
        request.release.projectId = reference.projectId;
        request.allowedChannels = channels;
        m_curseforge.resolveDependency(
            pack, rows, request,
            [callback = std::move(callback)](QVector<ContentInstallRequest> requests, QString error) mutable {
                if (error.isEmpty() && requests.size() != 1)
                    error = "The provider returned no dependency version.";
                callback(requests.size() == 1 ? requests.first() : ContentInstallRequest{}, error);
            });
        return;
    }
    callback({}, "This dependency provider is not supported.");
}
} // namespace studio
