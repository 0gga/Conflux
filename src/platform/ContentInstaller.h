#pragma once
#include "features/content/ContentPorts.h"
#include "platform/NativeContentStore.h"
#include "services/ModInstaller.h"
#include <QTemporaryDir>
namespace studio {
class ContentInstaller final : public QObject, public IContentInstaller {
    Q_OBJECT
  public:
    explicit ContentInstaller(std::shared_ptr<const NativeContentStore> files, QObject *parent = nullptr);
    void install(const QVariantMap &, const QVariantList &, const QVector<ContentInstallRequest> &,
                 Callback) override;
    void cancel() override {
        if (m_preparing)
            m_cancelled = true;
        else
            m_installer.cancel();
    }
    bool cancellable() const override {
        return m_preparing || m_installer.cancellable();
    }
  signals:
    void progress(QString message);

  private:
    ModInstaller m_installer;
    std::shared_ptr<const NativeContentStore> m_files;
    std::shared_ptr<QTemporaryDir> m_stage;
    QVariantMap m_pack;
    QVariantList m_rows;
    Callback m_callback;
    bool m_preparing = false;
    bool m_cancelled = false;
    void finish(ContentWriteResult);
};
} // namespace studio
