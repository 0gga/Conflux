#pragma once
#include "domain/Catalogue.h"
#include <QUrl>
#include <functional>
namespace studio {
struct ContentWriteResult {
    bool ok = false;
    QString error, backupPath;
    bool changed = false;
    QStringList warnings;
    QVariantList installedPredictions; // Metadata only; always reconcile against verified bytes.
};
struct ContentFileState {
    QVariantMap pack;
    QVariantList rows;
    QString error;
};
class ILauncherContentLocks {
  public:
    virtual ~ILauncherContentLocks() = default;
    virtual ContentWriteResult setLocked(const QVariantMap &, const QVariantList &, bool) const = 0;
};
class IContentFileStore {
  public:
    virtual ~IContentFileStore() = default;
    virtual QVariantList snapshot(const QVariantMap &pack, const QVariantList &files) const = 0;
    virtual ContentFileState reload(const QVariantMap &pack, const QVariantList &previous,
                                    bool inspectInventory = true) const {
        return {pack, inspectInventory ? snapshot(pack, previous) : previous, {}};
    }
    virtual ContentWriteResult restoreLocked(const QVariantMap &, const QVariantMap &) const {
        return {false, "No verified backup is available for this locked content."};
    }
    virtual ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const = 0;
    virtual ContentWriteResult setEnabledBatch(const QVariantMap &pack, const QVariantList &rows,
                                               bool enabled) const {
        if (rows.size() == 1)
            return setEnabled(pack, rows.first().toMap(), enabled);
        return {false, "Bulk enable/disable is not supported by this file store."};
    }
    virtual ContentWriteResult remove(const QVariantMap &, const QVariantList &) const = 0;
    virtual ContentWriteResult importFiles(const QVariantMap &, const QList<QUrl> &,
                                           const QString &category) const = 0;
};
class IContentCatalogue {
  public:
    using IndexCallback = std::function<void(QVariantList, QString)>;
    using VersionsCallback = std::function<void(QVector<Release>, QString)>;
    virtual ~IContentCatalogue() = default;
    virtual void refresh(const QVariantList &, const QString &, const QString &, IndexCallback) = 0;
    virtual void refreshWithChannels(const QVariantList &rows, const QString &game, const QString &loader,
                                     const QStringList &, IndexCallback callback) {
        refresh(rows, game, loader, std::move(callback));
    }
    virtual void versions(const QVariantMap &, const QString &, const QString &, VersionsCallback) = 0;
    virtual void cancelVersions() {}
    virtual void setCurseforgeKey(const QByteArray &) = 0;
};
class IContentInstaller {
  public:
    using Callback = std::function<void(ContentWriteResult)>;
    virtual ~IContentInstaller() = default;
    virtual void install(const QVariantMap &, const QVariantList &, const QVector<ContentInstallRequest> &,
                         Callback) = 0;
    virtual void cancel() = 0;
    virtual bool cancellable() const = 0;
};
class IContentPreferences {
  public:
    virtual ~IContentPreferences() = default;
    virtual bool frozen(const QString &pack, const QString &identity) const = 0;
    virtual bool hasFrozenPreference(const QString &pack, const QString &identity) const = 0;
    virtual void setFrozen(const QString &pack, const QString &identity, bool) = 0;
    virtual bool locked(const QString &, const QString &) const {
        return false;
    }
    virtual void setLocked(const QString &, const QString &, bool) {}
    virtual qint64 updatedAt(const QString &, const QString &) const {
        return 0;
    }
    virtual void setUpdatedAt(const QString &, const QString &, qint64) {}
};
} // namespace studio
