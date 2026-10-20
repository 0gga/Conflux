#pragma once
#include "NativeContentStore.h"
#include "features/content/ContentPorts.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
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
        return !m_reply.isNull();
    }
  signals:
    void progress(QString);

  private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    std::shared_ptr<const NativeContentStore> m_files;
    bool m_active = false, m_allowLocal = false;
};
} // namespace studio
