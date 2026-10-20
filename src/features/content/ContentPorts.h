#pragma once
#include "domain/Catalogue.h"
#include <QUrl>
#include <functional>
namespace studio {
struct ContentWriteResult {
    bool ok = false;
    QString error, backupPath;
};
class IContentFileStore {
  public:
    virtual ~IContentFileStore() = default;
    virtual QVariantList snapshot(const QVariantMap &pack, const QVariantList &files) const = 0;
    virtual ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const = 0;
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
    virtual void versions(const QVariantMap &, const QString &, const QString &, VersionsCallback) = 0;
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
};
} // namespace studio
