#pragma once
#include "NativeContentStore.h"
#include "VerifiedDownload.h"
#include "features/content/ContentPorts.h"
namespace studio {
// ZIP downloads have a separate lifecycle from mod dependency resolution.
class AssetInstaller final : public QObject {
    Q_OBJECT
  public:
    explicit AssetInstaller(std::shared_ptr<const NativeContentStore>, QObject *parent = nullptr,
                            bool allowLocalTestServer = false);
    void install(const QVariantMap &, const QVariantList &, const ContentInstallRequest &,
                 IContentInstaller::Callback);
    void cancel();
    bool cancellable() const {
        return m_download.busy();
    }
  signals:
    void progress(QString);

  private:
    VerifiedDownload m_download;
    std::shared_ptr<const NativeContentStore> m_files;
    bool m_active = false;
};
} // namespace studio
