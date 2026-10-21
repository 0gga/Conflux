#pragma once
#include "AssetInstaller.h"
#include "features/content/ContentPorts.h"
#include "platform/NativeContentStore.h"
#include "services/ModInstaller.h"
#include <QTemporaryDir>
namespace studio {
class ContentInstaller final : public QObject, public IContentInstaller {
    Q_OBJECT
  public:
    explicit ContentInstaller(std::shared_ptr<const NativeContentStore> files, QObject *parent = nullptr,
                              bool allowLocalTestServer = false);
    void install(const QVariantMap &, const QVariantList &, const QVector<ContentInstallRequest> &,
                 Callback) override;
    void cancel() override {
        m_cancelled = true;
        if (m_assets.cancellable()) {
            m_assets.cancel();
            return;
        }
        if (m_preparing)
            m_cancelled = true;
        else
            m_installer.cancel();
    }
    bool cancellable() const override {
        return !m_committing && (m_preparing || m_installer.cancellable() || m_assets.cancellable());
    }
  signals:
    void progress(QString message);

  private:
    ModInstaller m_installer;
    AssetInstaller m_assets;
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
    void startNextBatch();
    void installBatch(const QVector<ContentInstallRequest> &);
    void finishBatch(ContentWriteResult);
    void finish(ContentWriteResult);
};
} // namespace studio
