#include "ContentFixtures.h"
#include "DependencyFixtures.h"
#include "features/content/ContentDependencyInstaller.h"
#include "features/content/ContentOperationState.h"
#include "platform/ContentDependencySource.h"
#include "platform/ContentInstaller.h"
#include "services/PackRepository.h"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>
using namespace studio;
using namespace dependency_test;
namespace {
class Files final : public IContentFileStore {
  public:
    QVariantList rows;
    bool reloadFailure = false;
    QVariantList snapshot(const QVariantMap &, const QVariantList &) const override {
        return rows;
    }
    ContentFileState reload(const QVariantMap &pack, const QVariantList &, bool = true) const override {
        return {pack, rows, reloadFailure ? "Fixture reload failure" : QString{}};
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
class Leaf final : public IContentInstaller {
  public:
    std::shared_ptr<Files> files;
    QStringList calls;
    QHash<QString, QString> failures;
    bool throwing = false, duplicate = false, held = false;
    Callback completion;
    explicit Leaf(std::shared_ptr<Files> f) : files(std::move(f)) {}
    void install(const QVariantMap &pack, const QVariantList &,
                 const QVector<ContentInstallRequest> &requests, Callback callback) override {
        if (throwing)
            throw std::runtime_error("Fixture installer exception");
        const auto request = requests.first();
        calls << request.release.projectId;
        if (held) {
            completion = std::move(callback);
            return;
        }
        const auto error = failures.value(request.release.projectId);
        if (error.isEmpty())
            files->rows << contentInstallPrediction(request, pack, {});
        callback({error.isEmpty(), error});
        if (duplicate)
            callback({false, "duplicate callback"});
    }
    void cancel() override {
        if (completion) {
            auto cb = std::move(completion);
            cb({false, "Cancelled"});
        }
    }
    bool cancellable() const override {
        return bool(completion);
    }
};
class Source final : public IContentDependencySource {
  public:
    QHash<QString, ContentInstallRequest> values;
    QStringList calls;
    QHash<QString, QString> failures;
    bool throwing = false, held = false;
    Callback completion;
    void resolve(const QVariantMap &, const QVariantList &, const ContentDependency &reference,
                 const QStringList &, Callback callback) override {
        calls << reference.projectId;
        if (throwing)
            throw std::runtime_error("Fixture dependency exception");
        if (held) {
            completion = std::move(callback);
            return;
        }
        callback(values.value(reference.projectId), failures.value(reference.projectId));
    }
    void cancel() override {
        if (completion) {
            auto cb = std::move(completion);
            cb({}, "Cancelled");
        }
    }
};
ContentInstallRequest selected(const QString &id, const QStringList &dependencies = {}) {
    Release release;
    release.provider = "modrinth";
    release.projectId = id;
    release.id = id + "-v1";
    release.filename = id + ".jar";
    release.sha1 = hash(id.toUtf8());
    release.gameVersions = {"1.21.1"};
    release.loaders = {"fabric"};
    for (const auto &dependency : dependencies)
        release.dependencyRefs << required(dependency);
    return request(release);
}
QVariantMap pack() {
    return {{"path", "test-instance"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
}
QByteArray read(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}
} // namespace
class ContentDependencyTests final : public QObject {
    Q_OBJECT
  private slots:
    void independentFailuresKeepRootAndContinueOtherDependencies_data() {
        QTest::addColumn<QString>("failure");
        for (const auto &failure : {"lookup", "write", "throw", "identity", "reload", "none"})
            QTest::newRow(failure) << QString(failure);
    }
    void independentFailuresKeepRootAndContinueOtherDependencies() {
        QFETCH(QString, failure);
        auto files = std::make_shared<Files>();
        auto leaf = std::make_shared<Leaf>(files);
        auto source = std::make_shared<Source>();
        source->values["bad"] = selected("bad");
        source->values["good"] = selected("good");
        if (failure == "lookup")
            source->failures["bad"] = "Provider unavailable";
        if (failure == "write")
            leaf->failures["bad"] = "Storage unavailable";
        if (failure == "throw")
            source->throwing = true;
        if (failure == "identity")
            source->values["bad"] = selected("unexpected");
        if (failure == "reload")
            files->reloadFailure = true;
        ContentDependencyInstaller installer(leaf, source, files);
        bool done = false;
        ContentWriteResult result;
        installer.install(pack(), {}, {selected("root", {"bad", "good"})}, [&](auto r) {
            result = r;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.error.isEmpty());
        QVERIFY(result.changed);
        QCOMPARE(leaf->calls.first(), "root");
        QVERIFY(!files->rows.isEmpty());
        QCOMPARE(result.warnings.isEmpty(), failure == "none");
        if (failure != "throw" && failure != "reload")
            QVERIFY(leaf->calls.contains("good"));
        if (failure == "reload")
            QVERIFY(source->calls.isEmpty());
    }
    void cyclesDuplicatesAndPinnedConflictsStayBounded() {
        auto files = std::make_shared<Files>();
        auto leaf = std::make_shared<Leaf>(files);
        leaf->duplicate = true;
        auto source = std::make_shared<Source>();
        source->values["a"] = selected("a", {"root", "b", "b"});
        source->values["b"] = selected("b", {"a"});
        ContentDependencyInstaller installer(leaf, source, files);
        bool done = false;
        ContentWriteResult result;
        installer.install(pack(), {}, {selected("root", {"a", "a"})}, [&](auto r) {
            result = r;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(result.ok);
        QVERIFY(result.warnings.isEmpty());
        QCOMPARE(leaf->calls, QStringList({"root", "a", "b"}));
        QCOMPARE(source->calls, QStringList({"a", "b"}));
    }
    void disabledDependenciesAreNeverEnabledEvenWhenVersionDiffers() {
        auto files = std::make_shared<Files>();
        auto old = contentInstallPrediction(selected("lib"), pack(), {});
        old["enabled"] = false;
        old["relativePath"] = "mods/lib.jar.disabled";
        files->rows = {old};
        auto leaf = std::make_shared<Leaf>(files);
        auto source = std::make_shared<Source>();
        auto root = selected("root");
        root.release.dependencyRefs = {required("lib", "different")};
        ContentDependencyInstaller installer(leaf, source, files);
        bool done = false;
        installer.install(pack(), files->rows, {root}, [&](auto r) {
            QVERIFY(r.ok);
            QVERIFY(r.warnings.isEmpty());
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(leaf->calls, QStringList{"root"});
        QVERIFY(source->calls.isEmpty());
        QVERIFY(!files->rows.first().toMap().value("enabled").toBool());
    }
    void cancellationAfterRootKeepsSuccessAndIgnoresLateCallbacks() {
        auto files = std::make_shared<Files>();
        auto leaf = std::make_shared<Leaf>(files);
        auto source = std::make_shared<Source>();
        source->held = true;
        ContentDependencyInstaller installer(leaf, source, files);
        int callbacks = 0;
        installer.install(pack(), {}, {selected("root", {"a"})}, [&](auto result) {
            QVERIFY(result.ok);
            QVERIFY(!result.warnings.isEmpty());
            ++callbacks;
        });
        QTRY_VERIFY(bool(source->completion));
        auto stale = source->completion;
        installer.cancel();
        QTRY_COMPARE(callbacks, 1);
        stale(selected("a"), {});
        QTest::qWait(10);
        QCOMPARE(callbacks, 1);
        QCOMPARE(leaf->calls, QStringList{"root"});
    }
    void rootFailureDoesNotAttemptDependenciesAndInstallerCanBeReused() {
        auto files = std::make_shared<Files>();
        auto leaf = std::make_shared<Leaf>(files);
        leaf->failures["root"] = "Root transfer failed";
        auto source = std::make_shared<Source>();
        ContentDependencyInstaller installer(leaf, source, files);
        bool done = false;
        installer.install(pack(), {}, {selected("root", {"a"})}, [&](auto result) {
            QVERIFY(!result.ok);
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(source->calls.isEmpty());
        done = false;
        leaf->failures.clear();
        installer.install(pack(), {}, {selected("root")}, [&](auto result) {
            QVERIFY(result.ok);
            done = true;
        });
        QTRY_VERIFY(done);
    }
    void shallowUpdatesBypassDependencyCoordination() {
        auto files = std::make_shared<Files>();
        auto leaf = std::make_shared<Leaf>(files);
        auto source = std::make_shared<Source>();
        ContentDependencyInstaller installer(leaf, source, files);
        auto update = selected("root", {"a"});
        update.resolveDependencies = false;
        bool done = false;
        installer.install(pack(), {}, {update}, [&](auto r) {
            QVERIFY(r.ok);
            done = true;
        });
        QVERIFY(done);
        QVERIFY(source->calls.isEmpty());
    }
    void exactCrossPlatformFilesAreRetainedWithoutDuplicateInstallation() {
        auto files = std::make_shared<Files>();
        auto dependency = selected("lib");
        auto existing = contentInstallPrediction(dependency, pack(), {});
        existing["sourceIds"] = QVariantMap{{"curseforge", "12"}};
        existing["installedRelease"] = QVariantMap{};
        existing["relativePath"] = "mods/from-other-platform.jar";
        files->rows = {existing};
        auto leaf = std::make_shared<Leaf>(files);
        auto source = std::make_shared<Source>();
        source->values["lib"] = dependency;
        ContentDependencyInstaller installer(leaf, source, files);
        bool done = false;
        installer.install(pack(), files->rows, {selected("root", {"lib"})}, [&](auto result) {
            QVERIFY(result.ok);
            QVERIFY(result.warnings.isEmpty());
            bool merged = false;
            for (const auto &value : result.installedPredictions) {
                const auto ids = value.toMap().value("sourceIds").toMap();
                merged |= ids.value("curseforge") == "12" && ids.value("modrinth") == "lib";
            }
            QVERIFY(merged);
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(leaf->calls, QStringList{"root"});
        QCOMPARE(files->rows.size(), 2);
    }
    void shallowInstallerExceptionsDoNotLeaveTheCoordinatorBusy() {
        auto files = std::make_shared<Files>();
        auto leaf = std::make_shared<Leaf>(files);
        auto source = std::make_shared<Source>();
        ContentDependencyInstaller installer(leaf, source, files);
        auto update = selected("root");
        update.resolveDependencies = false;
        leaf->throwing = true;
        bool done = false;
        installer.install(pack(), {}, {update}, [&](auto result) {
            QVERIFY(!result.ok);
            done = true;
        });
        QVERIFY(done);
        leaf->throwing = false;
        done = false;
        installer.install(pack(), {}, {update}, [&](auto result) {
            QVERIFY(result.ok);
            done = true;
        });
        QVERIFY(done);
    }
    void excessiveGraphsAreBoundedAndYieldToTheEventLoop() {
        auto files = std::make_shared<Files>();
        auto leaf = std::make_shared<Leaf>(files);
        auto source = std::make_shared<Source>();
        QStringList dependencies;
        for (int i = 0; i < 1000; ++i) {
            const auto id = QString("lib%1").arg(i);
            dependencies << id;
            source->values[id] = selected(id);
        }
        ContentDependencyInstaller installer(leaf, source, files);
        bool done = false;
        int ticks = 0;
        QTimer timer;
        timer.setInterval(1);
        connect(&timer, &QTimer::timeout, this, [&] { ++ticks; });
        timer.start();
        QElapsedTimer elapsed;
        elapsed.start();
        installer.install(pack(), {}, {selected("root", dependencies)}, [&](auto result) {
            QVERIFY(result.ok);
            QVERIFY(!result.warnings.isEmpty());
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        qInfo() << "1000 declared / 256 bounded dependency attempts ms" << elapsed.elapsed()
                << "event loop ticks" << ticks;
        QCOMPARE(source->calls.size(), 256);
        QCOMPARE(leaf->calls.size(), 257);
        QVERIFY(ticks > 1);
        QVERIFY(elapsed.elapsed() < 2500);
    }
    void realTransactionsIsolateDependencyFailures_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<QString>("failure");
        QTest::addColumn<bool>("managed");
        for (const auto &provider : {"modrinth", "curseforge"})
            for (const auto &failure :
                 {"none", "lookup", "download", "hash", "commit", "disabled", "locked", "bridge"})
                for (bool managed : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-managed%3").arg(provider, failure).arg(managed)))
                        << QString(provider) << QString(failure) << managed;
    }
    void realTransactionsIsolateDependencyFailures() {
        QFETCH(QString, provider);
        QFETCH(QString, failure);
        QFETCH(bool, managed);
        QTemporaryDir temp;
        Server server;
        QVariantMap target{
            {"path", temp.path() + "/instance"}, {"minecraft", "1.21.1"}, {"loader", "fabric"}};
        if (failure == "bridge")
            target["loader"] = "neoforge";
        if (managed) {
            const auto created =
                PackRepository::create(temp.path(), "Fixture", "1.21.1", target.value("loader").toString());
            QVERIFY2(created.ok, qPrintable(created.error));
            target = created.manifest.toVariantMap();
            target["path"] = created.path;
            target["managed"] = true;
        }
        const auto path = target.value("path").toString();
        QVERIFY(QDir().mkpath(path));
        const auto rootBytes = archive(temp.path() + "/root.jar", "root");
        const auto badBytes = archive(temp.path() + "/bad.jar", "bad");
        const auto goodBytes = archive(temp.path() + "/good.jar", "good");
        QVERIFY(!rootBytes.isEmpty());
        QVERIFY(!badBytes.isEmpty());
        QVERIFY(!goodBytes.isEmpty());
        Release root, bad, good;
        if (provider == "modrinth") {
            root = modrinthRelease(
                server.modrinth("root", "root-v1", rootBytes, {required("bad"), required("good")}));
            bad = modrinthRelease(server.modrinth("bad", "bad-v1", badBytes));
            good = modrinthRelease(server.modrinth("good", "good-v1", goodBytes));
        } else {
            root = curseforgeRelease(server.curseforge(11, 101, rootBytes,
                                                       {QJsonObject{{"modId", 12}, {"relationType", 3}},
                                                        QJsonObject{{"modId", 13}, {"relationType", 3}}}));
            bad = curseforgeRelease(server.curseforge(12, 102, badBytes));
            good = curseforgeRelease(server.curseforge(13, 103, goodBytes));
        }
        const auto installedPath = [&](const Release &r) {
            return path + "/mods/" + (managed ? r.sha1 + ".jar" : r.filename);
        };
        if (failure == "lookup")
            server.statuses[provider == "modrinth" ? "/project/bad/version" : "/mods/12"] = 503;
        if (failure == "download")
            server.statuses[QUrl(bad.url).path()] = 503;
        if (failure == "hash")
            server.bodies[QUrl(bad.url).path()] = "corrupt";
        if (failure == "commit")
            QVERIFY(QDir().mkpath(installedPath(bad)));
        auto locks = std::make_shared<ContentLockRegistry>();
        auto files = std::make_shared<NativeContentStore>(nullptr, temp.path() + "/backups", locks);
        QVariantList rows;
        QString disabled;
        const auto retainedBytes =
            failure == "locked" ? archive(temp.path() + "/old.jar", "old_dependency") : badBytes;
        if (failure == "disabled" || failure == "locked") {
            disabled = path + (failure == "disabled" ? "/mods/kept.jar.disabled" : "/mods/kept.jar");
            QVERIFY(QDir().mkpath(QFileInfo(disabled).absolutePath()));
            QFile file(disabled);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(retainedBytes), retainedBytes.size());
            file.close();
            auto row = contentInstallPrediction(request(bad), target, {});
            row["relativePath"] = failure == "disabled" ? "mods/kept.jar.disabled" : "mods/kept.jar";
            row["enabled"] = failure == "locked";
            row["locked"] = failure == "locked";
            if (failure == "locked") {
                auto incompatible = row.value("installedRelease").toMap();
                row["sha1"] = hash(retainedBytes);
                incompatible["sha1"] = hash(retainedBytes);
                incompatible["id"] = "older";
                incompatible["gameVersions"] = QStringList{"1.20.1"};
                row["installedRelease"] = incompatible;
            }
            rows << row;
        }
        if (failure == "bridge") {
            const auto bytes = archive(temp.path() + "/connector.jar", "connector");
            QVERIFY(QDir().mkpath(path + "/mods"));
            QFile file(path + "/mods/connector.jar");
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
            file.close();
            rows = files->snapshot(target, {QVariantMap{{"relativePath", "mods/connector.jar"},
                                                        {"category", "mods"},
                                                        {"modId", "connector"}}});
        }
        if (failure == "commit")
            rows = files->reload(target, {}).rows;
        locks->setRows(path, rows);
        auto leaf = std::make_shared<ContentInstaller>(files, nullptr, true, server.base(), server.base());
        auto source = std::make_shared<ContentDependencySource>(nullptr, server.base(), server.base());
        leaf->setCurseforgeKey("fixture-key");
        source->setCurseforgeKey("fixture-key");
        ContentDependencyInstaller installer(leaf, source, files);
        bool rootCommittedBeforeDependencies = false;
        server.requested = [&](const QString &endpoint) {
            if (endpoint == "/project/bad/version" || endpoint == "/mods/12" ||
                endpoint == "/project/good/version" || endpoint == "/mods/13")
                rootCommittedBeforeDependencies = read(installedPath(root)) == rootBytes;
        };
        bool done = false;
        ContentWriteResult result;
        installer.install(target, rows, {request(root)}, [&](auto r) {
            result = r;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY2(read(installedPath(root)) == rootBytes, qPrintable(result.warnings.join('\n')));
        QCOMPARE(read(installedPath(good)), goodBytes);
        QVERIFY(rootCommittedBeforeDependencies);
        QCOMPARE(result.warnings.isEmpty(),
                 failure == "none" || failure == "disabled" || failure == "bridge");
        if (failure == "none")
            QCOMPARE(read(installedPath(bad)), badBytes);
        if (failure == "disabled" || failure == "locked") {
            QCOMPARE(read(disabled), retainedBytes);
            QVERIFY(!QFileInfo::exists(installedPath(bad)));
        }
        if (managed) {
            const auto inspected = PackRepository::inspectContent(path);
            QVERIFY(inspected.ok);
            QVERIFY(inspected.manifest.value("mods").toArray().size() >= 2);
        }
    }
};
QTEST_GUILESS_MAIN(ContentDependencyTests)
#include "ContentDependencyTests.moc"
