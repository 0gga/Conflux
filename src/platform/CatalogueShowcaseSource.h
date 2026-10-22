#pragma once
#include "features/showcase/ProjectShowcaseSource.h"
#include "services/CatalogueClient.h"
#include "services/GalleryImages.h"
namespace studio {
class CatalogueShowcaseSource final : public QObject, public IProjectShowcaseSource {
    Q_OBJECT
  public:
    explicit CatalogueShowcaseSource(QObject *parent = nullptr,
                                     QUrl modrinthApi = QUrl("https://api.modrinth.com/v2/"),
                                     QUrl curseforgeApi = QUrl("https://api.curseforge.com/v1/"),
                                     bool allowLoopbackImages = false);
    void resolve(const InstalledModIdentity &, ResolveCallback) override;
    void resolveProgressive(const InstalledModIdentity &, DescriptionCallback, ResolveCallback) override;
    void load(const Project &, const QString &, const QString &, DetailCallback) override;
    void loadProgressive(const Project &, const QString &, const QString &, DescriptionCallback,
                         GalleryCallback, DetailCallback) override;
    void cancel() override;
    void loadChangelog(const Release &, ChangelogCallback) override;
    void setCurseforgeKey(const QByteArray &key) override {
        m_client.setCurseforgeKey(key);
    }

  private:
    CatalogueClient m_client;
    GalleryImages m_images;
    int m_generation = 0;
    int m_activeDetail = 0;
    int m_activeResolve = 0;
    DescriptionCallback m_resolveDescription;
    QHash<int, ResolveCallback> m_resolves;
    QHash<int, DetailCallback> m_details;
    DescriptionCallback m_description;
    GalleryCallback m_gallery;
};
} // namespace studio
