#pragma once
#include "features/content/ContentLockRegistry.h"
#include "features/content/ContentPorts.h"
#include "features/showcase/ProjectShowcaseSource.h"
#include <memory>
namespace studio {
class NativeContentStore final : public IContentFileStore {
  public:
    explicit NativeContentStore(std::shared_ptr<const IInstalledModIdentity> identity,
                                QString backupRoot = {}, std::shared_ptr<ContentLockRegistry> locks = {});
    QVariantList snapshot(const QVariantMap &, const QVariantList &) const override;
    ContentFileState reload(const QVariantMap &, const QVariantList &,
                            bool inspectInventory = true) const override;
    ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const override;
    ContentWriteResult setEnabledBatch(const QVariantMap &, const QVariantList &, bool) const override;
    ContentWriteResult remove(const QVariantMap &, const QVariantList &) const override;
    ContentWriteResult importFiles(const QVariantMap &, const QList<QUrl> &, const QString &) const override;
    // Used by the external install adapter after verified dependency staging.
    ContentWriteResult applyStaged(const QVariantMap &, const QVariantList &expected, const QString &stage,
                                   const QJsonObject &manifest) const;
    ContentWriteResult commitStagedContent(const QVariantMap &, const QVariantList &, const QString &,
                                           const QJsonObject &, const QSet<QString> &shallowProjects = {},
                                           bool selectedOnly = false) const;
    ContentWriteResult restoreLocked(const QVariantMap &, const QVariantMap &) const override;
    ContentWriteResult installAsset(const QVariantMap &, const QVariantList &, const ContentInstallRequest &,
                                    const QString &verifiedFile) const;

  private:
    struct Edit {
        QString from, to, source, expectedSha1;
    };
    ContentWriteResult apply(const QVariantMap &, QList<Edit>, const QJsonArray *replacementPins = nullptr,
                             const QJsonArray *replacementAssets = nullptr) const;
    std::shared_ptr<const IInstalledModIdentity> m_identity;
    QString m_backupRoot;
    std::shared_ptr<ContentLockRegistry> m_locks;
};
} // namespace studio
