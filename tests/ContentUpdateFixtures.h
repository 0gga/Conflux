#pragma once
#include "ContentFixtures.h"
#include "domain/ContentTypes.h"
#include "features/content/ContentActions.h"
#include "features/preferences/ContentPreferences.h"
#include <QCryptographicHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <stdexcept>

namespace update_fixture {
inline QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
}
inline QVariantMap row(const QString &id, const QString &category = "mods", bool enabled = true) {
    const auto extension = category == "mods" ? ".jar" : ".zip";
    Release old;
    old.provider = "modrinth";
    old.projectId = id;
    old.id = id + "-old";
    old.version = "1.0";
    old.filename = id + "-old" + extension;
    old.sha1 = hash((id + "-old").toUtf8());
    old.gameVersions = {"1.21.1"};
    old.loaders = {"fabric"};
    auto next = old;
    next.id = id + "-new";
    next.version = "2.0";
    next.filename = id + "-new" + extension;
    next.sha1 = hash((id + "-new").toUtf8());
    return {{"title", id},
            {"category", category},
            {"fileName", old.filename},
            {"relativePath", category + '/' + old.filename + (enabled ? "" : ".disabled")},
            {"enabled", enabled},
            {"sha1", old.sha1},
            {"version", old.version},
            {"provider", old.provider},
            {"projectId", id},
            {"sourceIds", QVariantMap{{"modrinth", id}, {"curseforge", "cf-" + id}}},
            {"installedRelease", old.toVariant()},
            {"updateRelease", next.toVariant()},
            {"updateAvailable", true}};
}
class Files final : public IContentFileStore {
  public:
    mutable QMutex mutex;
    QHash<QString, QVariantList> contents;
    mutable std::atomic<int> reads{0};
    mutable std::atomic<int> readFailures{0};
    mutable std::atomic<bool> throwReads{false};
    void set(const QString &path, QVariantList rows) {
        QMutexLocker lock(&mutex);
        contents[path] = std::move(rows);
    }
    QVariantList get(const QString &path) const {
        QMutexLocker lock(&mutex);
        return contents.value(path);
    }
    void mutate(const QString &path, const QString &id, const std::function<void(QVariantMap &)> &edit) {
        QMutexLocker lock(&mutex);
        for (auto &value : contents[path]) {
            auto item = value.toMap();
            if (item.value("projectId") == id) {
                edit(item);
                value = item;
            }
        }
    }
    void commit(const QVariantMap &pack, const QVector<ContentInstallRequest> &requests) {
        QMutexLocker lock(&mutex);
        auto &rows = contents[pack.value("path").toString()];
        for (const auto &request : requests)
            for (auto &value : rows)
                if (value.toMap().value("projectId") == request.release.projectId)
                    value = contentInstallPrediction(request, pack, value.toMap());
    }
    QVariantList snapshot(const QVariantMap &pack, const QVariantList &) const override {
        return get(pack.value("path").toString());
    }
    ContentFileState reload(const QVariantMap &pack, const QVariantList &, bool = true) const override {
        ++reads;
        auto failures = readFailures.load();
        while (failures != 0) {
            if (failures < 0 || readFailures.compare_exchange_weak(failures, failures - 1)) {
                if (throwReads)
                    throw std::runtime_error("Fixture inventory unavailable");
                return {pack, {}, "Fixture inventory unavailable"};
            }
        }
        return {pack, get(pack.value("path").toString()), {}};
    }
    ContentWriteResult setEnabled(const QVariantMap &, const QVariantMap &, bool) const override {
        return {};
    }
    ContentWriteResult remove(const QVariantMap &, const QVariantList &) const override {
        return {};
    }
    ContentWriteResult importFiles(const QVariantMap &, const QList<QUrl> &, const QString &) const override {
        return {};
    }
};
class Installer final : public QObject, public IContentInstaller {
  public:
    std::shared_ptr<Files> files;
    QVector<QVector<ContentInstallRequest>> calls;
    QVector<ContentFileState> inputs;
    QHash<QString, int> failures; // 1: failed result, 2: exception, 3: uncertain write
    Callback completion;
    QVariantMap pack;
    QVector<ContentInstallRequest> requests;
    int active = 0, peak = 0;
    bool automatic = true, duplicateCallback = false;
    std::function<void(const QString &)> afterCommit;
    explicit Installer(std::shared_ptr<Files> store) : files(std::move(store)) {}
    void install(const QVariantMap &target, const QVariantList &rows,
                 const QVector<ContentInstallRequest> &batch, Callback done) override {
        calls << batch;
        inputs << ContentFileState{target, rows, {}};
        pack = target;
        requests = batch;
        peak = qMax(peak, ++active);
        if (failures.value(batch.first().release.projectId) == 2) {
            --active;
            throw std::runtime_error("Fixture installer exception");
        }
        completion = std::move(done);
        if (automatic)
            QTimer::singleShot(0, this, [this] { finish(); });
    }
    void finish(bool cancel = false) {
        if (!completion)
            return;
        const auto id = requests.first().release.projectId;
        const auto failure = failures.value(id);
        if (!cancel && !failure) {
            files->commit(pack, requests);
            if (afterCommit)
                afterCommit(id);
        }
        auto done = std::move(completion);
        --active;
        ContentWriteResult result{!cancel && !failure, cancel    ? "Cancelled"
                                                       : failure ? "Fixture download failed"
                                                                 : ""};
        result.changed = failure == 3 || result.ok;
        done(result);
        if (duplicateCallback)
            done(result);
    }
    void cancel() override {
        finish(true);
    }
    bool cancellable() const override {
        return bool(completion);
    }
};
struct Harness {
    QTemporaryDir temp;
    std::shared_ptr<Files> files = std::make_shared<Files>();
    std::shared_ptr<Installer> installer = std::make_shared<Installer>(files);
    std::shared_ptr<ContentCatalogueFixture> catalogue = std::make_shared<ContentCatalogueFixture>();
    std::shared_ptr<SettingsContentPreferences> preferences =
        std::make_shared<SettingsContentPreferences>(temp.path() + "/prefs.ini");
    ContentActions content{files, catalogue, installer, preferences};
    QVariantMap pack{
        {"path", temp.path()}, {"title", "Fixture"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
    void set(QVariantList rows) {
        files->set(pack.value("path").toString(), rows);
        content.setContext(pack, rows);
    }
};
inline QVariantMap byId(const QVariantList &rows, const QString &id) {
    for (const auto &value : rows)
        if (value.toMap().value("projectId") == id)
            return value.toMap();
    return {};
}
inline quint64 taskId(ContentActions &content, const QString &title) {
    for (const auto &value : content.operations()->entries())
        if (value.toMap().value("title") == title)
            return value.toMap().value("id").toULongLong();
    return 0;
}
} // namespace update_fixture
