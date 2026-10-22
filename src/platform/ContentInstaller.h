#pragma once
#include "features/content/ContentPorts.h"
#include "platform/NativeContentStore.h"
#include "platform/VerifiedDownload.h"
#include "services/ModInstaller.h"
#include <QTemporaryDir>
namespace studio {
class ContentInstaller final : public QObject, public IContentInstaller {
    Q_OBJECT
  public:
    explicit ContentInstaller(std::shared_ptr<const NativeContentStore> files, QObject *parent = nullptr,
                              bool allowLocalTestServer = false, QUrl apiRoot = {});
    void install(const QVariantMap &, const QVariantList &, const QVector<ContentInstallRequest> &,
                 Callback) override;
    void cancel() override {
        m_cancelled = true;
        m_download.cancel();
        if (m_preparing)
            m_cancelled = true;
        else
            m_installer.cancel();
    }
    bool cancellable() const override {
        return !m_committing && (m_preparing || m_download.busy() || m_installer.cancellable());
    }
  signals:
    void progress(QString message);

  private:
    ModInstaller m_installer;
    VerifiedDownload m_download;
    std::shared_ptr<const NativeContentStore> m_files;
    std::shared_ptr<QTemporaryDir> m_stage;
    QVariantMap m_pack, m_targetPack;
    QVariantList m_targetRows;
    QSet<QString> m_shallowProjects;
    bool m_committing = false;
    QVariantList m_rows;
    Callback m_callback;
    bool m_preparing = false;
    bool m_cancelled = false;
    QList<QVector<ContentInstallRequest>> m_batches;

    QStringList m_warnings;
    QVector<ContentInstallRequest> m_directRequests;
    QJsonArray m_directMods, m_directAssets;
    int m_directIndex = 0;
    void downloadDirect();
    void commitDirect();
    void startNextBatch();
    void installBatch(const QVector<ContentInstallRequest> &);
    void finishBatch(ContentWriteResult);
    void finish(ContentWriteResult);
};
} // namespace studio
