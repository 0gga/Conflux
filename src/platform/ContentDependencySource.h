#pragma once
#include "features/content/ContentDependencyInstaller.h"
#include "platform/CurseforgeInstallSource.h"
#include "services/CatalogueClient.h"
namespace studio {
class ContentDependencySource final : public QObject, public IContentDependencySource {
  public:
    explicit ContentDependencySource(QObject *parent = nullptr,
                                     QUrl modrinthApi = QUrl("https://api.modrinth.com/v2/"),
                                     QUrl curseforgeApi = QUrl("https://api.curseforge.com/v1/"));
    void setCurseforgeKey(const QByteArray &key) {
        m_curseforge.setKey(key);
    }
    void resolve(const QVariantMap &, const QVariantList &, const ContentDependency &, const QStringList &,
                 Callback) override;
    void cancel() override;

  private:
    CatalogueClient m_modrinth;
    CurseforgeInstallSource m_curseforge;
};
} // namespace studio
