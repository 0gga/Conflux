#include "domain/Catalogue.h"
#include "domain/ReleaseStages.h"
#include "features/preferences/PackReleasePolicy.h"
#include "platform/BrowseSettingsStore.h"
#include "services/ContentCatalogue.h"
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)
using namespace studio;
class ReleasePolicyTests final : public QObject {
    Q_OBJECT
    static QQuickItem *item(QQuickItem *root, const QString &name) {
        if (root->objectName() == name && root->isVisible())
            return root;
        for (auto *child : root->childItems())
            if (auto *found = item(child, name))
                return found;
        return nullptr;
    }
  private slots:
    void initTestCase() {
        QQuickStyle::setStyle("Basic");
#ifdef Q_OS_WIN
        const auto fonts = qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/";
        for (const auto &file : {"segoeui.ttf", "seguisb.ttf", "segoeuib.ttf", "seguisym.ttf"})
            QVERIFY(QFontDatabase::addApplicationFont(fonts + file) >= 0);
        QGuiApplication::setFont(QFont("Segoe UI"));
#endif
    }
    void providerStagesAndEverySelection_data() {
        QTest::addColumn<int>("mask");
        for (int mask = 0; mask < 8; ++mask)
            QTest::newRow(qPrintable(QString::number(mask))) << mask;
    }
    void providerStagesAndEverySelection() {
        QFETCH(int, mask);
        QStringList selection;
        const auto stages = releaseStages();
        for (int i = 0; i < 3; ++i)
            if (mask & (1 << i))
                selection << stages[i];
        for (int i = 0; i < 3; ++i) {
            const auto mr = modrinthRelease({{"version_type", stages[i]}});
            const auto cf = curseforgeRelease({{"releaseType", i + 1}});
            QCOMPARE(mr.channel, cf.channel);
            QCOMPARE(releaseStageAllowed(mr.channel, selection), mask == 0 || bool(mask & (1 << i)));
            for (const auto &installed : stages) {
                const auto expected =
                    selection.isEmpty() ? stages.mid(0, stages.indexOf(installed) + 1) : selection;
                QCOMPARE(updateReleaseStages(installed, selection), expected);
            }
        }
        QVERIFY(!releaseStageAllowed("snapshot", selection));
        QVERIFY(!releaseStageAllowed("unknown", selection));
        QCOMPARE(modrinthRelease({}).channel, QString("unknown"));
        QCOMPARE(curseforgeRelease({{"releaseType", 4}}).channel, QString("unknown"));
    }
    void preferencesPersistPerInstanceAndRejectUnknownStages() {
        QTemporaryDir temp;
        auto store = std::make_shared<BrowseSettingsStore>(temp.path() + "/prefs.ini");
        PackReleasePolicy policy(store);
        QSignalSpy changes(&policy, &PackReleasePolicy::changed);
        QCOMPARE(policy.channels("pack-a"), QStringList{});
        policy.setChannels("pack-a", {" BETA ", "release", "beta"});
        QCOMPARE(policy.channels("pack-a"), (QStringList{"release", "beta"}));
        QCOMPARE(policy.channels("pack-b"), QStringList{});
        policy.setChannels("pack-a", {"release", "beta"});
        policy.setChannels("pack-a", {"snapshot"});
        policy.setChannels("", {"alpha"});
        QCOMPARE(changes.size(), 1);
        {
            PackReleasePolicy reopened(std::make_shared<BrowseSettingsStore>(temp.path() + "/prefs.ini"));
            QCOMPARE(reopened.channels("pack-a"), (QStringList{"release", "beta"}));
            QCOMPARE(reopened.channels("pack-b"), QStringList{});
        }
#ifdef Q_OS_WIN
        policy.setChannels("C:/Instances/Pack/", {"alpha"});
        QCOMPARE(policy.channels("c:/instances/pack"), QStringList{"alpha"});
#endif
        policy.setChannels("pack-a", {});
        QCOMPARE(policy.channels("pack-a"), QStringList{});
        QVERIFY(policy.allows("pack-a", "beta"));
        QVERIFY(!policy.allows("pack-a", "archived"));
    }
    void repeatedReadsHaveNoWritesOrSignals() {
        QTemporaryDir temp;
        PackReleasePolicy policy(std::make_shared<BrowseSettingsStore>(temp.path() + "/prefs.ini"));
        policy.setChannels("pack", {"release", "beta"});
        QSignalSpy changes(&policy, &PackReleasePolicy::changed);
        QElapsedTimer elapsed;
        elapsed.start();
        for (int i = 0; i < 100000; ++i)
            QVERIFY(policy.allows("pack", "beta"));
        qInfo() << "100000 cached release policy reads ms" << elapsed.elapsed();
        QVERIFY(elapsed.elapsed() < 1500);
        QCOMPARE(changes.size(), 0);
    }
    void explicitStagesBatchMixedInstalledChannelsInOneUpdateRequest() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QJsonObject installed, latest;
        QVariantList rows;
        const auto stages = releaseStages();
        for (int i = 0; i < 3; ++i) {
            const auto hash = QString(40, QChar('a' + i));
            const auto project = QString("p%1").arg(i);
            auto version =
                QJsonObject{{"project_id", project},
                            {"id", project + "-old"},
                            {"version_number", "1"},
                            {"version_type", stages[i]},
                            {"date_published", "2026-10-20T10:00:00Z"},
                            {"game_versions", QJsonArray{"1.21.1"}},
                            {"loaders", QJsonArray{"fabric"}},
                            {"files", QJsonArray{QJsonObject{{"filename", project + ".jar"},
                                                             {"primary", true},
                                                             {"url", "https://example.invalid/mod.jar"},
                                                             {"size", 100},
                                                             {"hashes", QJsonObject{{"sha1", hash}}}}}}};
            installed[hash] = version;
            version["id"] = project + "-new";
            version["version_number"] = "2";
            version["date_published"] = "2026-10-21T10:00:00Z";
            auto file = version.value("files").toArray().first().toObject();
            file["hashes"] = QJsonObject{{"sha1", QString(40, QChar('d' + i))}};
            version["files"] = QJsonArray{file};
            latest[hash] = version;
            rows << QVariantMap{{"category", "mods"}, {"sha1", hash}};
        }
        int requests = 0, updateRequests = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const auto bytes = socket->property("input").toByteArray() + socket->readAll();
                socket->setProperty("input", bytes);
                const int split = bytes.indexOf("\r\n\r\n");
                if (split < 0 || socket->property("answered").toBool())
                    return;
                int size = 0;
                for (const auto &line : bytes.left(split).split('\n'))
                    if (line.toLower().startsWith("content-length:"))
                        size = line.mid(15).trimmed().toInt();
                if (bytes.size() < split + 4 + size)
                    return;
                socket->setProperty("answered", true);
                ++requests;
                const auto path = bytes.split(' ').value(1);
                QJsonDocument reply(QJsonArray{});
                if (path == "/version_files")
                    reply = QJsonDocument(installed);
                if (path == "/version_files/update") {
                    ++updateRequests;
                    const auto query = QJsonDocument::fromJson(bytes.mid(split + 4)).object();
                    QCOMPARE(query.value("hashes").toArray().size(), 3);
                    QCOMPARE(query.value("version_types").toArray(), QJsonArray::fromStringList(stages));
                    reply = QJsonDocument(latest);
                }
                const auto body = reply.toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        ContentCatalogue catalogue(nullptr,
                                   QUrl("http://127.0.0.1:" + QString::number(server.serverPort()) + "/"));
        catalogue.setCurseforgeKey({});
        bool done = false;
        catalogue.refreshWithChannels(rows, "1.21.1", "fabric", stages,
                                      [&](QVariantList result, QString error) {
                                          QVERIFY2(error.isEmpty(), qPrintable(error));
                                          QCOMPARE(result.size(), 3);
                                          for (const auto &row : result)
                                              QVERIFY(row.toMap().value("updateAvailable").toBool());
                                          done = true;
                                      });
        QTRY_VERIFY(done);
        QCOMPARE(updateRequests, 1);
        QCOMPARE(requests, 3); // Identification, one batched update, and project metadata.
    }
    void pickerUsesOneSharedSelectionAndFitsItsPopup() {
#ifndef Q_MOC_RUN
        QTemporaryDir temp;
        PackReleasePolicy policy(std::make_shared<BrowseSettingsStore>(temp.path() + "/prefs.ini"));
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(import QtQuick
import "components"
import Conflux.Style
Item { required property var policyModel; Rectangle { anchors.fill:parent; color:Theme.colors.background } ReleaseStagePicker { x:20;y:20; policy:policyModel;packPath:"pack" } }
)QML",
                          QUrl::fromLocalFile(QStringLiteral(CONFLUX_QML_SOURCE_DIR) + "/StageFixture.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QQuickItem> root(qobject_cast<QQuickItem *>(
            component.createWithInitialProperties({{"policyModel", QVariant::fromValue(&policy)}})));
        QVERIFY(root);
        QQuickWindow window;
        window.resize(480, 340);
        root->setParentItem(window.contentItem());
        root->setSize(window.size());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *picker = item(root.get(), "packReleaseStages");
        QVERIFY(picker);
        auto click = [&window](QQuickItem *control) {
            QTest::mouseClick(
                &window, Qt::LeftButton, Qt::NoModifier,
                control->mapToScene(QPointF(control->width() / 2, control->height() / 2)).toPoint());
        };
        click(picker);
        QTRY_VERIFY(item(window.contentItem(), "packReleaseStage-beta"));
        auto *beta = item(window.contentItem(), "packReleaseStage-beta");
        auto *release = item(window.contentItem(), "packReleaseStage-release");
        QCOMPARE(QQmlProperty(qvariant_cast<QQuickItem *>(beta->property("background")), "border.width")
                     .read()
                     .toInt(),
                 0);
        QVERIFY(beta->width() >= 200);
        click(beta);
        QCOMPARE(policy.channels("pack"), QStringList{"beta"});
        QCOMPARE(picker->property("text").toString(), QString("Stages: Beta"));
        click(release);
        QCOMPARE(policy.channels("pack"), (QStringList{"release", "beta"}));
        QTest::qWait(80); // Let the font/layout scene render before the visual artifact.
        QVERIFY(window.grabWindow().save(QStringLiteral(CONFLUX_QML_SOURCE_DIR) +
                                         "/../artifacts/release-policy-picker.png"));
        click(beta);
        QCOMPARE(policy.channels("pack"), QStringList{"release"});
        click(release); // Last explicit stage cannot disappear accidentally.
        QCOMPARE(policy.channels("pack"), QStringList{"release"});
        click(item(window.contentItem(), "releaseStageAuto"));
        QCOMPARE(policy.channels("pack"), QStringList{});
        QCOMPARE(picker->property("text").toString(), QString("Stages: Auto"));
#endif
    }
};
QTEST_MAIN(ReleasePolicyTests)
#include "ReleasePolicyTests.moc"
