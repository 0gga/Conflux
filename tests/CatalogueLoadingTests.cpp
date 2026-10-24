#include "DependencyFixtures.h"
#include "services/CatalogueClient.h"
#include <QUrlQuery>
#include <QtTest>
using namespace studio;
using namespace dependency_test;
class CatalogueLoadingTests final : public QObject {
    Q_OBJECT
  private slots:
    void dependencySelectionKeepsExactCompatibilityAndPolicy_data() {
        QTest::addColumn<QString>("fault");
        for (const auto &fault :
             {"none", "game", "loader", "channel", "project", "version", "http", "json", "identity"})
            QTest::newRow(fault) << QString(fault);
    }
    void dependencySelectionKeepsExactCompatibilityAndPolicy() {
        QFETCH(QString, fault);
        Server server;
        auto value = server.modrinth("lib", "v1", "fixture bytes");
        if (fault == "game")
            value["game_versions"] = QJsonArray{"1.20.1"};
        if (fault == "loader")
            value["loaders"] = QJsonArray{"neoforge"};
        if (fault == "channel")
            value["version_type"] = "beta";
        if (fault == "project")
            value["project_id"] = "other";
        if (fault == "version")
            value["id"] = "other";
        server.json("/version/v1", value);
        if (fault == "http")
            server.statuses["/version/v1"] = 503;
        if (fault == "json")
            server.bodies["/version/v1"] = "{broken";
        CatalogueClient client(nullptr, server.base(), server.base());
        bool done = false;
        Release selected;
        QString error;
        client.modrinthDependency(fault == "identity" ? "../lib" : "lib", "v1", "1.21.1", {"fabric"}, {},
                                  [&](Release release, QString failure) {
                                      selected = release;
                                      error = failure;
                                      done = true;
                                  });
        QTRY_VERIFY(done);
        QCOMPARE(error.isEmpty(), fault == "none");
        if (fault == "none")
            QCOMPARE(selected.id, "v1");
        if (fault == "identity")
            QVERIFY(server.requests.isEmpty());
    }
    void projectLookupFiltersLocallyAndPrefersStableWithoutExtraProjectRequests() {
        Server server;
        auto stable = server.modrinth("lib", "stable", "bytes");
        auto beta = stable;
        beta["id"] = "beta";
        beta["version_type"] = "beta";
        beta["date_published"] = "2026-10-25T10:00:00Z";
        auto other = stable;
        other["project_id"] = "wrong";
        other["id"] = "wrong";
        auto wrongLoader = stable;
        wrongLoader["loaders"] = QJsonArray{"neoforge"};
        server.json("/project/lib/version", QJsonArray{beta, other, wrongLoader, stable});
        CatalogueClient client(nullptr, server.base(), server.base());
        bool done = false;
        client.modrinthDependency("lib", {}, "1.21.1", {"fabric"}, {"release", "beta"},
                                  [&](Release release, QString error) {
                                      QVERIFY2(error.isEmpty(), qPrintable(error));
                                      QCOMPARE(release.id, "stable");
                                      done = true;
                                  });
        QTRY_VERIFY(done);
        QCOMPARE(server.requests.size(), 1);
        const QUrlQuery query(QUrl(server.requests.first()));
        QVERIFY(query.queryItemValue("game_versions").contains("1.21.1"));
        QVERIFY(query.queryItemValue("loaders").contains("fabric"));
    }
    void versionOnlyLookupAndCancellation() {
        Server server;
        server.modrinth("lib", "v1", "bytes");
        CatalogueClient client(nullptr, server.base(), server.base());
        bool done = false;
        client.modrinthDependency({}, "v1", "1.21.1", {"fabric"}, {}, [&](Release release, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(release.projectId, "lib");
            done = true;
        });
        QTRY_VERIFY(done);
        int callbacks = 0;
        client.modrinthDependency({}, "v1", "1.21.1", {"fabric"}, {}, [&](auto, auto) { ++callbacks; });
        client.cancelPendingReads();
        QTest::qWait(30);
        QCOMPARE(callbacks, 0);
    }
};
QTEST_GUILESS_MAIN(CatalogueLoadingTests)
#include "CatalogueLoadingTests.moc"
