#include "OptionalDependencySource.h"
#include <QPointer>
namespace studio {
OptionalDependencySource::OptionalDependencySource(QObject *parent, QUrl mr, QUrl cf)
    : QObject(parent), m_versions(this, mr, cf), m_projects(this, mr, cf) {}
void OptionalDependencySource::setCurseforgeKey(const QByteArray &key) {
    m_versions.setCurseforgeKey(key);
    m_projects.setCurseforgeKey(key);
}
void OptionalDependencySource::cancel() {
    ++m_generation;
    m_versions.cancel();
    m_projects.cancel();
}
void OptionalDependencySource::resolve(const QVariantMap &pack, const QVariantList &rows,
                                       const ContentDependency &reference, const QStringList &channels,
                                       Callback callback) {
    const auto generation = ++m_generation;
    const QPointer<OptionalDependencySource> guard(this);
    m_versions.resolve(pack, rows, reference, channels,
                       [guard, generation, reference, callback = std::move(callback)](
                           ContentInstallRequest request, QString error) mutable {
                           if (!guard || generation != guard->m_generation)
                               return;
                           InstalledModIdentity identity;
                           for (auto it = request.sourceIds.begin(); it != request.sourceIds.end(); ++it)
                               identity.sourceIds[it.key()] = it.value().toString();
                           if (!request.release.projectId.isEmpty())
                               identity.sourceIds[request.release.provider] = request.release.projectId;
                           else if (!reference.projectId.isEmpty())
                               identity.sourceIds[reference.provider] = reference.projectId;
                           if (identity.sourceIds.isEmpty()) {
                               callback(std::move(request), {}, error);
                               return;
                           }
                           guard->m_projects.resolve(identity, [guard, generation, request, callback, error](
                                                                   Project project, QString warning) mutable {
                               if (!guard || generation != guard->m_generation)
                                   return;
                               auto info = project.toVariant();
                               if (!project.title.isEmpty())
                                   request.title = project.title;
                               info["warning"] = warning;
                               callback(std::move(request), std::move(info), error);
                           });
                       });
}
} // namespace studio
