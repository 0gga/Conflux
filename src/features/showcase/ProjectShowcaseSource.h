#pragma once
#include "domain/Catalogue.h"
#include <functional>
namespace studio {
struct InstalledModIdentity {
    QString sha1, error;
    QMap<QString, QString> sourceIds;
    QString projectUrl;
};
class IInstalledModIdentity {
  public:
    virtual ~IInstalledModIdentity() = default;
    virtual InstalledModIdentity inspect(const QVariantMap &pack, const QVariantMap &file) const = 0;
    // The file store supplies a fingerprint-verified hash, avoiding another read of the same JAR.
    virtual InstalledModIdentity inspectVerified(const QVariantMap &pack, const QVariantMap &file,
                                                 const QString &) const {
        return inspect(pack, file);
    }
};
class IProjectShowcaseSource {
  public:
    using ResolveCallback = std::function<void(Project, QString)>;
    using DetailCallback = std::function<void(ProjectDetail)>;
    using DescriptionCallback = std::function<void(QString)>;
    using GalleryCallback = std::function<void(QStringList, QStringList)>;
    using ChangelogCallback = std::function<void(QString, QString)>;
    virtual void loadChangelog(const Release &release, ChangelogCallback callback) {
        callback(release.changelog, {});
    }
    virtual ~IProjectShowcaseSource() = default;
    virtual void resolve(const InstalledModIdentity &, ResolveCallback) = 0;
    // This early description is always verified Modrinth Markdown.
    virtual void resolveProgressive(const InstalledModIdentity &identity, DescriptionCallback,
                                    ResolveCallback complete) {
        resolve(identity, std::move(complete));
    }
    virtual void load(const Project &, const QString &game, const QString &loader, DetailCallback) = 0;
    virtual void loadProgressive(const Project &project, const QString &game, const QString &loader,
                                 DescriptionCallback, GalleryCallback gallery, DetailCallback complete) {
        load(project, game, loader,
             [gallery = std::move(gallery), complete = std::move(complete)](ProjectDetail detail) {
                 if (gallery)
                     gallery(detail.images, {});
                 complete(std::move(detail));
             });
    }
    virtual void cancel() {}
    virtual void setCurseforgeKey(const QByteArray &) = 0;
};
} // namespace studio
