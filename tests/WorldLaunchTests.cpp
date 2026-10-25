#include "platform/WorldLaunchAdapters.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
using namespace studio;
class LaunchFixture final : public ILaunchUrlOpener {
  public:
    QUrl requested;
    bool registered = true, openResult = true;
    int attempts = 0;
    bool available(const QString &) const override {
        return registered;
    }
    bool open(const QUrl &url) override {
        ++attempts;
        requested = url;
        return openResult;
    }
};
class WorldLaunchTests final : public QObject {
    Q_OBJECT
    static void write(const QString &path, const QByteArray &bytes) {
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
    }
  private slots:
    void packHandoffChecksProviderInstanceHandlerAndOpenResult_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<QString>("instance");
        QTest::addColumn<bool>("registered");
        QTest::addColumn<bool>("openResult");
        QTest::addColumn<int>("attempts");
        QTest::newRow("unsupported") << QString("curseforge") << QString("id") << true << true << 0;
        QTest::newRow("missing-instance") << QString("modrinth") << QString{} << true << true << 0;
        QTest::newRow("missing-handler") << QString("modrinth") << QString("id") << false << true << 0;
        QTest::newRow("open-failed") << QString("modrinth") << QString("id") << true << false << 1;
        QTest::newRow("open-ok") << QString("modrinth") << QString("id") << true << true << 1;
    }
    void packHandoffChecksProviderInstanceHandlerAndOpenResult() {
        QFETCH(QString, provider);
        QFETCH(QString, instance);
        QFETCH(bool, registered);
        QFETCH(bool, openResult);
        QFETCH(int, attempts);
        auto opener = std::make_shared<LaunchFixture>();
        opener->registered = registered;
        opener->openResult = openResult;
        WorldLaunch launch({std::make_shared<ModrinthWorldLaunchAdapter>()}, opener,
                           std::make_shared<NativePackFileStore>());
        const QVariantMap pack{
            {"provider", provider}, {"launcherInstanceId", instance}, {"gameVersion", "1.21.1"}};
        QSignalSpy changes(&launch, &WorldLaunch::changed);
        launch.setPack(pack);
        launch.setPack(pack);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(launch.packAvailable(), attempts == 1);
        launch.playPack();
        QCOMPARE(opener->attempts, attempts);
        QVERIFY(!launch.status().isEmpty());
        if (attempts) {
            QCOMPARE(opener->requested.scheme(), QString("modrinth"));
            QVERIFY(opener->requested.query().isEmpty());
            QCOMPARE(launch.status().startsWith("Launch request sent"), openResult);
        } else {
            launch.play("saves/world");
            QCOMPARE(opener->attempts, 0);
            QVERIFY(!launch.unavailableReason().isEmpty());
        }
    }
    void unavailableWorldPolicyNeverOpensALauncher() {
        auto opener = std::make_shared<LaunchFixture>();
        WorldLaunch launch({std::make_shared<ModrinthWorldLaunchAdapter>()}, opener,
                           std::make_shared<NativePackFileStore>());
        launch.setPack({{"provider", "modrinth"}, {"minecraft", "1.21.1"}});
        QVERIFY(launch.unavailableReason().contains("instance ID"));
        launch.play("saves/world");
        QCOMPARE(opener->attempts, 0);
        QVERIFY(launch.status().contains("instance ID"));
        opener->registered = false;
        launch.setPack({{"provider", "modrinth"}, {"minecraft", "1.21.1"}, {"launcherInstanceId", "id"}});
        QVERIFY(launch.unavailableReason().contains("register"));
        launch.play("saves/world");
        QCOMPARE(opener->attempts, 0);
    }
    void worldOpenFailureRemainsVisibleAndNeverWritesMetadata() {
        QTemporaryDir temp;
        const QByteArray bytes(8192, 'x');
        const auto filePath = temp.path() + "/saves/world/level.dat";
        write(filePath, bytes);
        auto opener = std::make_shared<LaunchFixture>();
        opener->openResult = false;
        WorldLaunch launch({std::make_shared<ModrinthWorldLaunchAdapter>()}, opener,
                           std::make_shared<NativePackFileStore>());
        launch.setPack({{"path", temp.path()},
                        {"provider", "modrinth"},
                        {"minecraft", "1.21.1"},
                        {"launcherInstanceId", "id"}});
        launch.play("saves/world");
        QCOMPARE(opener->attempts, 1);
        QVERIFY(launch.status().contains("Could not send"));
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
    }
    void worldLaunchValidatesFolderAndEncodesExactTarget() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString name = QString::fromUtf8("C++ & world ? # \xc3\xa9");
        // Windows does not allow ? in folder names; URL encoding is tested separately below.
        const QString folder = QString::fromUtf8("C++ & world \xc3\xa9");
        write(temp.path() + "/saves/" + folder + "/level.dat", "world fixture");
        auto opener = std::make_shared<LaunchFixture>();
        auto adapter = std::make_shared<ModrinthWorldLaunchAdapter>();
        WorldLaunch launch({adapter}, opener, std::make_shared<NativePackFileStore>());
        QVariantMap pack{{"path", temp.path()},
                         {"provider", "modrinth"},
                         {"minecraft", "1.21.1"},
                         {"launcherInstanceId", "local:example/id"}};
        launch.setPack(pack);
        QVERIFY(launch.available());
        launch.play("saves/" + folder);
        QCOMPARE(opener->requested.scheme(), QString("modrinth"));
        QCOMPARE(QUrlQuery(opener->requested).queryItemValue("singleplayer_world", QUrl::FullyDecoded),
                 folder);
        QVERIFY(opener->requested.toEncoded().contains("local%3Aexample%2Fid"));
        const auto special = adapter->worldUrl(pack, name);
        QCOMPARE(QUrlQuery(special).queryItemValue("singleplayer_world", QUrl::FullyDecoded), name);
        opener->requested = QUrl{};
        launch.play("saves/../elsewhere");
        QVERIFY(opener->requested.isEmpty());
        write(temp.path() + "/saves/empty/README.txt", "not a world");
        launch.play("saves/empty");
        QVERIFY(opener->requested.isEmpty());
        pack["minecraft"] = "1.19.4";
        launch.setPack(pack);
        QVERIFY(!launch.available());
        pack["minecraft"] = "unknown";
        launch.setPack(pack);
        QVERIFY(!launch.available());
        pack["provider"] = "curseforge";
        launch.setPack(pack);
        QVERIFY(!launch.available());
    }
    void worldLaunchUsesBoundedReadForOrdinarySizedMetadata_data() {
        QTest::addColumn<int>("bytes");
        for (const int size : {0, 1, 16, 17, 4096, 2 * 1024 * 1024})
            QTest::newRow(qPrintable(QString::number(size))) << size;
    }
    void worldLaunchUsesBoundedReadForOrdinarySizedMetadata() {
        QFETCH(int, bytes);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto levelPath = temp.path() + "/saves/world/level.dat";
        const QByteArray metadata(bytes, 'x');
        write(levelPath, metadata);
        auto opener = std::make_shared<LaunchFixture>();
        auto files = std::make_shared<NativePackFileStore>();
        // A full-file read with a 16-byte limit rejects a larger file. The launch
        // check only needs evidence of readable bytes, never the entire payload.
        const auto probe = files->read(temp.path(), "saves/world/level.dat", 16, true);
        QVERIFY(probe.error.isEmpty());
        QCOMPARE(probe.bytes.size(), qMin(bytes, 16));
        WorldLaunch launch({std::make_shared<ModrinthWorldLaunchAdapter>()}, opener, files);
        launch.setPack({{"path", temp.path()},
                        {"provider", "modrinth"},
                        {"minecraft", "1.21.1"},
                        {"launcherInstanceId", "verified-test-instance"}});
        launch.play("saves/world");
        QCOMPARE(!opener->requested.isEmpty(), bytes > 0);
        QFile unchanged(levelPath);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), metadata);
    }
};
QTEST_GUILESS_MAIN(WorldLaunchTests)
#include "WorldLaunchTests.moc"
