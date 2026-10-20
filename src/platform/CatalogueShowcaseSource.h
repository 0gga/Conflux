#pragma once
#include "features/showcase/ProjectShowcaseSource.h"
#include "services/CatalogueClient.h"
#include "services/GalleryImages.h"
namespace studio {
class CatalogueShowcaseSource final : public QObject, public IProjectShowcaseSource {
    Q_OBJECT
  public:
    explicit CatalogueShowcaseSource(QObject *parent = nullptr);
    void resolve(const InstalledModIdentity &, ResolveCallback) override;
    void load(const Project &, const QString &, const QString &, DetailCallback) override;
    void setCurseforgeKey(const QByteArray &key) override {
        m_client.setCurseforgeKey(key);
    }

  private:
    CatalogueClient m_client;
    GalleryImages m_images;
    int m_generation = 0;
    int m_activeDetail = 0;
    QHash<int, ResolveCallback> m_resolves;
    QHash<int, DetailCallback> m_details;
    QHash<int, ProjectDetail> m_loading;
};
} // namespace studio
