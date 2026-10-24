#pragma once
#include "CatalogueShowcaseSource.h"
#include "ContentDependencySource.h"
#include "features/content/OptionalDependencies.h"
namespace studio {
// Separate read clients keep optional UI lookups independent of automatic installation.
class OptionalDependencySource final : public QObject, public IOptionalDependencySource {
  public:
    explicit OptionalDependencySource(QObject *parent = nullptr,
                                      QUrl modrinthApi = QUrl("https://api.modrinth.com/v2/"),
                                      QUrl curseforgeApi = QUrl("https://api.curseforge.com/v1/"));
    void resolve(const QVariantMap &, const QVariantList &, const ContentDependency &, const QStringList &,
                 Callback) override;
    void cancel() override;
    void setCurseforgeKey(const QByteArray &) override;

  private:
    ContentDependencySource m_versions;
    CatalogueShowcaseSource m_projects;
    quint64 m_generation = 0;
};
} // namespace studio
