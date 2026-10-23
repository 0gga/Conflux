#pragma once
#include "ContentOperationState.h"
#include <QObject>
#include <memory>
namespace studio {
// Owns asynchronous IO/installer port execution and confirmed-state reconciliation.
class ContentOperationExecutor final : public QObject {
  public:
    using Callback = std::function<void(ContentWriteResult, ContentFileState)>;
    ContentOperationExecutor(std::shared_ptr<const IContentFileStore> files,
                             std::shared_ptr<IContentInstaller> installer, QObject *parent = nullptr)
        : QObject(parent), m_files(std::move(files)), m_installer(std::move(installer)) {}
    void setLauncherLocks(std::shared_ptr<const ILauncherContentLocks> locks) {
        m_launcherLocks = std::move(locks);
    }
    void execute(ContentOperation, ContentFileState, Callback);

  private:
    std::shared_ptr<const IContentFileStore> m_files;
    std::shared_ptr<const ILauncherContentLocks> m_launcherLocks;
    std::shared_ptr<IContentInstaller> m_installer;
};
} // namespace studio
