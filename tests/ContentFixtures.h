#pragma once
#include "features/content/ContentPorts.h"
using namespace studio;
class ContentFileFixture final : public IContentFileStore {
  public:
    QVariantList snapshot(const QVariantMap &, const QVariantList &rows) const override {
        return rows;
    }
    ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const override {
        return {true, {}, {}};
    }
    ContentWriteResult setEnabledBatch(const QVariantMap &, const QVariantList &, bool) const override {
        return {true, {}};
    }
    ContentWriteResult remove(const QVariantMap &, const QVariantList &) const override {
        return {true, {}, {}};
    }
    ContentWriteResult importFiles(const QVariantMap &, const QList<QUrl> &, const QString &) const override {
        return {true, {}, {}};
    }
};
class ContentCatalogueFixture final : public IContentCatalogue {
  public:
    int refreshCount = 0;
    QVariantList refreshed;
    void refresh(const QVariantList &rows, const QString &, const QString &,
                 IndexCallback callback) override {
        ++refreshCount;
        refreshed = rows;
        callback(rows, {});
    }
    void versions(const QVariantMap &, const QString &, const QString &, VersionsCallback callback) override {
        callback({}, {});
    }
    void setCurseforgeKey(const QByteArray &) override {}
};
class ContentInstallerFixture final : public IContentInstaller {
  public:
    QVector<ContentInstallRequest> requested;
    Callback completion;
    void install(const QVariantMap &, const QVariantList &, const QVector<ContentInstallRequest> &requests,
                 Callback callback) override {
        requested = requests;
        completion = std::move(callback);
    }
    void cancel() override {
        if (completion) {
            auto done = std::move(completion);
            done({false, "Cancelled"});
        }
    }
    bool cancellable() const override {
        return bool(completion);
    }
};
