#include "DependencyFixtures.h"
#include "platform/ContentMetadataSource.h"
#include "platform/SqliteContentIndex.h"
#include <QFile>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include <algorithm>
using namespace studio;
using namespace dependency_test;
namespace {
QVariantMap actual(const QByteArray &bytes) {
    return {{"relativePath", "mods/example.jar"},
            {"fileName", "example.jar"},
            {"category", "mods"},
            {"sha1", hash(bytes)},
            {"title", "Local title"},
            {"enabled", true}};
}
QVariantMap knownCurseforgeRow(const QByteArray &bytes) {
    auto row = actual(bytes);
    row["sourceIds"] = QVariantMap{{"curseforge", "11"}};
    row["installedRelease"] = QVariantMap{{"provider", "curseforge"},
                                          {"projectId", "11"},
                                          {"id", "101"},
                                          {"sha1", hash(bytes)},
                                          {"version", "1"}};
    return row;
}
QJsonObject curseforgeMetadataProject(const QString &author = "CF Author") {
    return {{"id", 11},
            {"gameId", 432},
            {"classId", 6},
            {"name", "CF fixture"},
            {"links", QJsonObject{{"websiteUrl", "https://www.curseforge.com/minecraft/mc-mods/example"}}},
            {"authors", QJsonArray{QJsonObject{{"id", 7}, {"name", author}}}},
            {"categories", QJsonArray{QJsonObject{{"id", 42}, {"name", "Utility"}}}}};
}
QJsonObject project(const QString &id, const QString &team = "team") {
    return {{"id", id},
            {"title", "Project " + id},
            {"team", team},
            {"project_type", "mod"},
            {"icon_url", "https://example.test/icon.png"},
            {"categories", QJsonArray{"optimization"}},
            {"license", QJsonObject{{"id", "MIT"}}},
            {"client_side", "required"},
            {"server_side", "required"}};
}
QJsonObject member(const QString &name = "Alice", const QString &team = "team") {
    return {{"team_id", team},
            {"role", "Owner"},
            {"accepted", true},
            {"user", QJsonObject{{"id", "user-" + name}, {"username", name}}}};
}
void modrinth(Server &server, const QByteArray &bytes, const QString &id = "example",
              const QString &version = "v1", const QString &name = "Alice") {
    const auto release = server.modrinth(id, version, bytes);
    server.json("/version_files", QJsonObject{{hash(bytes), release}});
    server.json("/projects", QJsonArray{project(id)});
    server.json("/teams", QJsonArray{QJsonArray{member(name)}});
}
} // namespace
class ContentMetadataTests final : public QObject {
    Q_OBJECT
  private slots:
    void coalescedCurseforgeReadersRecheckTheConnectionBeforeEachDelivery_data() {
        QTest::addColumn<QString>("change");
        for (const auto &change : {"replace", "remove", "trim-equivalent", "roundtrip"})
            QTest::newRow(change) << QString(change);
    }
    void coalescedCurseforgeReadersRecheckTheConnectionBeforeEachDelivery() {
        QFETCH(QString, change);
        Server server;
        server.json("/version_files", QJsonObject{});
        server.json("/mods", QJsonObject{{"data", QJsonArray{curseforgeMetadataProject()}}});
        ContentMetadataSource source({}, nullptr, server.base(), server.base());
        source.setCurseforgeKey("fixture-key");
        const auto input = knownCurseforgeRow("verified file");
        QVariantList first, second;
        QString firstError, secondError;
        bool firstDone = false, secondDone = false;
        source.resolve({}, {input}, [&](auto rows, QString error) {
            first = rows;
            firstError = error;
            if (change == "remove")
                source.setCurseforgeKey({});
            else if (change == "trim-equivalent")
                source.setCurseforgeKey("  fixture-key\t");
            else {
                source.setCurseforgeKey("replacement-key");
                if (change == "roundtrip")
                    source.setCurseforgeKey("fixture-key");
            }
            firstDone = true;
        });
        source.resolve({}, {input}, [&](auto rows, QString error) {
            second = rows;
            secondError = error;
            secondDone = true;
        });
        QTRY_VERIFY(firstDone && secondDone);
        QVERIFY(firstError.isEmpty());
        QVERIFY(secondError.isEmpty()); // Optional metadata failures stay quiet and retryable.
        QCOMPARE(first.size(), 1);
        QCOMPARE(second.size(), 1);
        QVERIFY(first.first().toMap().value("metadataResolved").toBool());
        QCOMPARE(first.first().toMap().value("author").toString(), QString("CF Author"));
        const auto later = second.first().toMap();
        const bool sameConnection = change == "trim-equivalent";
        QCOMPARE(later.value("metadataResolved").toBool(), sameConnection);
        QCOMPARE(later.value("author").toString(), sameConnection ? QString("CF Author") : QString{});
        QCOMPARE(later.value("installedRelease"), input.value("installedRelease"));
        QCOMPARE(later.value("sha1"), input.value("sha1"));
        QCOMPARE(server.payloads.value("/mods").size(), 1);
        server.json("/mods", QJsonObject{{"data", QJsonArray{curseforgeMetadataProject("New author")}}});
        source.setCurseforgeKey("replacement-key");
        auto retry = later;
        retry["metadataResolved"] = false;
        bool retried = false;
        source.resolve({}, {retry}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QVERIFY(rows.first().toMap().value("metadataResolved").toBool());
            QCOMPARE(rows.first().toMap().value("author").toString(), QString("New author"));
            retried = true;
        });
        QTRY_VERIFY(retried);
        QCOMPARE(server.payloads.value("/mods").size(), 2);
        QVERIFY(server.requestHeaders.value("/mods").toLower().contains("x-api-key: replacement-key"));
    }
    void curseforgeConnectionChangesDoNotInvalidateCoalescedModrinthReaders() {
        Server server;
        modrinth(server, "file");
        ContentMetadataSource source({}, nullptr, server.base(), server.base());
        source.setCurseforgeKey("fixture-key");
        int completions = 0;
        source.resolve({}, {actual("file")}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows.first().toMap().value("author").toString(), QString("Alice"));
            ++completions;
            source.setCurseforgeKey("replacement-key");
        });
        source.resolve({}, {actual("file")}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows.first().toMap().value("author").toString(), QString("Alice"));
            QVERIFY(rows.first().toMap().value("metadataResolved").toBool());
            ++completions;
        });
        QTRY_COMPARE(completions, 2);
        QCOMPARE(server.payloads.value("/version_files").size(), 1);
        QCOMPARE(server.payloads.value("/projects").size(), 1);
        QCOMPARE(server.payloads.value("/teams").size(), 1);
        QVERIFY(server.payloads.value("/mods").isEmpty());
    }
    void successfulResolutionIsReusedAcrossReadersWithoutNetwork() {
        QTemporaryDir temp;
        Server server;
        const QByteArray bytes = "exact installed file";
        modrinth(server, bytes);
        const auto path = temp.path() + "/index.sqlite3";
        bool done = false;
        QVariantList indexed;
        {
            ContentMetadataSource source(std::make_shared<SqliteContentIndex>(path), nullptr, server.base(),
                                         server.base());
            source.setCurseforgeKey("fixture-key");
            source.resolve({{"path", temp.path()}}, {actual(bytes)}, [&](auto rows, QString error) {
                QVERIFY2(error.isEmpty(), qPrintable(error));
                indexed = rows;
                done = true;
            });
            QTRY_VERIFY(done);
        }
        const auto row = indexed.first().toMap();
        QVERIFY(row.value("metadataResolved").toBool());
        QCOMPARE(row.value("projectId").toString(), "example");
        QCOMPARE(row.value("author").toString(), "Alice");
        QCOMPARE(row.value("authors").toList().size(), 1);
        QCOMPARE(row.value("tags").toStringList(), QStringList{"mr:optimization"});
        QVERIFY(!server.requestHeaders.value("/version_files").toLower().contains("x-api-key"));
        const auto requests = server.requests;
        done = false;
        bool sawCachedAuthor = false;
        ContentMetadataSource reopened(std::make_shared<SqliteContentIndex>(path), nullptr, server.base(),
                                       server.base());
        reopened.resolve(
            {{"path", temp.path()}}, {actual(bytes)},
            [&](auto rows, QString error) {
                QVERIFY(error.isEmpty());
                QCOMPARE(rows.first().toMap().value("author").toString(), "Alice");
                done = true;
            },
            [&](auto rows) { sawCachedAuthor |= rows.first().toMap().value("author") == "Alice"; });
        QTRY_VERIFY(done);
        QVERIFY(sawCachedAuthor);
        QCOMPARE(server.requests, requests);
    }
    void changedFilesReindexAndRefreshTagsWhilePreservingOldVersionIdentity() {
        QTemporaryDir temp;
        Server server;
        const auto path = temp.path() + "/index.sqlite3";
        const QByteArray old = "old", changed = "new";
        modrinth(server, old);
        ContentMetadataSource source(std::make_shared<SqliteContentIndex>(path), nullptr, server.base(),
                                     server.base());
        source.setCurseforgeKey({});
        bool done = false;
        source.resolve({}, {actual(old)}, [&](auto, QString error) {
            QVERIFY(error.isEmpty());
            done = true;
        });
        QTRY_VERIFY(done);
        modrinth(server, changed, "example", "v2", "Bob");
        auto changedProject = project("example");
        changedProject["categories"] = QJsonArray{"new-tag"};
        server.json("/projects", QJsonArray{changedProject});
        server.requests.clear();
        done = false;
        source.resolve({}, {actual(changed)}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows.first().toMap().value("author").toString(), "Bob");
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(server.requests.contains("/version_files"));
        SqliteContentIndex index(path);
        const auto cached = index.lookup({}, {actual(old)}).rows.first().toMap();
        QCOMPARE(cached.value("tags").toStringList(), QStringList{"mr:new-tag"});
        QCOMPARE(cached.value("installedRelease").toMap().value("id").toString(), "v1");
    }
    void newInstalledVersionCannotReuseThePreviousFilesResolvedFlag() {
        QTemporaryDir temp;
        Server server;
        const QByteArray first = "first", second = "second";
        modrinth(server, first);
        ContentMetadataSource source(std::make_shared<SqliteContentIndex>(temp.path() + "/index.sqlite3"),
                                     nullptr, server.base(), server.base());
        source.setCurseforgeKey({});
        bool done = false;
        QVariantMap installed;
        source.resolve({}, {actual(first)}, [&](auto rows, auto) {
            installed = rows.first().toMap();
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(installed.value("metadataResolved").toBool());
        modrinth(server, second, "example", "v2", "Bob");
        installed["sha1"] = hash(second);
        installed["installedRelease"] = modrinthRelease(server.modrinth("example", "v2", second)).toVariant();
        done = false;
        QVariantMap resolved;
        source.resolve({}, {installed}, [&](auto rows, auto) {
            resolved = rows.first().toMap();
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(resolved.value("author").toString(), "Bob");
        QCOMPARE(resolved.value("installedRelease").toMap().value("id").toString(), "v2");
    }
    void failedAuthorsRemainRetryableWithoutReidentifyingTheFile() {
        QTemporaryDir temp;
        Server server;
        const QByteArray bytes = "file";
        modrinth(server, bytes);
        server.statuses["/teams"] = 503;
        ContentMetadataSource source(std::make_shared<SqliteContentIndex>(temp.path() + "/index.sqlite3"),
                                     nullptr, server.base(), server.base());
        source.setCurseforgeKey({});
        bool done = false;
        source.resolve({}, {actual(bytes)}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QVERIFY(rows.first().toMap().value("identityResolved").toBool());
            QVERIFY(!rows.first().toMap().value("metadataResolved").toBool());
            done = true;
        });
        QTRY_VERIFY(done);
        server.statuses.remove("/teams");
        server.requests.clear();
        done = false;
        source.resolve({}, {actual(bytes)}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows.first().toMap().value("author").toString(), "Alice");
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(!server.requests.contains("/version_files"));
        QVERIFY(server.requests.contains("/teams?ids=%5B%22team%22%5D") || server.requests.size() == 2);
    }
    void curseforgeOnlyFilesResolveByFingerprintAndPublishedSha1_data() {
        QTest::addColumn<bool>("knownProject");
        QTest::newRow("unknown-file") << false;
        QTest::newRow("launcher-project-without-exact-release") << true;
    }
    void curseforgeOnlyFilesResolveByFingerprintAndPublishedSha1() {
        QFETCH(bool, knownProject);
        QTemporaryDir temp;
        Server server;
        const QByteArray bytes = "a real exact local fixture";
        QVERIFY(QDir().mkpath(temp.path() + "/mods"));
        QFile file(temp.path() + "/mods/example.jar");
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        server.json("/version_files", QJsonObject{});
        const auto release = server.curseforge(11, 101, bytes);
        server.json(
            "/fingerprints/432",
            QJsonObject{{"data", QJsonObject{{"exactMatches",
                                              QJsonArray{QJsonObject{{"id", 11}, {"file", release}}}}}}});
        const QJsonObject cf{
            {"id", 11},
            {"gameId", 432},
            {"name", "CF fixture"},
            {"classId", 6},
            {"links", QJsonObject{{"websiteUrl", "https://www.curseforge.com/minecraft/mc-mods/example"}}},
            {"authors", QJsonArray{QJsonObject{{"id", 7}, {"name", "CF Author"}}}},
            {"categories", QJsonArray{QJsonObject{{"id", 42}, {"name", "Utility"}}}}};
        server.json("/mods", QJsonObject{{"data", QJsonArray{cf}}});
        ContentMetadataSource source(std::make_shared<SqliteContentIndex>(temp.path() + "/index.sqlite3"),
                                     nullptr, server.base(), server.base());
        source.setCurseforgeKey("fixture-key");
        bool done = false;
        auto input = actual(bytes);
        if (knownProject)
            input["sourceIds"] = QVariantMap{{"curseforge", "11"}};
        source.resolve({{"path", temp.path()}}, {input}, [&](auto rows, QString error) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            const auto row = rows.first().toMap();
            QCOMPARE(row.value("projectId").toString(), "11");
            QCOMPARE(row.value("author").toString(), "CF Author");
            QCOMPARE(row.value("installedRelease").toMap().value("id").toString(), "101");
            QVERIFY(row.value("metadataResolved").toBool());
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(
            server.requestHeaders.value("/fingerprints/432").toLower().contains("x-api-key: fixture-key"));
        QVERIFY(!server.requestHeaders.value("/version_files").toLower().contains("x-api-key"));
    }
    void knownModrinthProjectDoesNotBlockExactCurseForgeReleaseRecovery() {
        QTemporaryDir temp;
        Server server;
        const QByteArray bytes = "cross-provider file";
        QVERIFY(QDir().mkpath(temp.path() + "/mods"));
        QFile file(temp.path() + "/mods/example.jar");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        file.close();
        modrinth(server, bytes);
        server.json("/version_files", QJsonObject{});
        server.json(
            "/fingerprints/432",
            QJsonObject{
                {"data", QJsonObject{{"exactMatches",
                                      QJsonArray{QJsonObject{
                                          {"id", 11}, {"file", server.curseforge(11, 101, bytes)}}}}}}});
        auto input = actual(bytes);
        input["sourceIds"] = QVariantMap{{"modrinth", "example"}};
        input["provider"] = "modrinth";
        input["projectId"] = "example";
        input["identityResolved"] = true;
        input["identitySha1"] = input.value("sha1");
        ContentMetadataSource source(std::make_shared<SqliteContentIndex>(temp.path() + "/index.sqlite3"),
                                     nullptr, server.base(), server.base());
        source.setCurseforgeKey("fixture-key");
        bool done = false;
        source.resolve({{"path", temp.path()}}, {input}, [&](QVariantList rows, QString error) {
            QVERIFY(error.isEmpty());
            const auto item = rows.first().toMap();
            QCOMPARE(item.value("installedRelease").toMap().value("provider").toString(),
                     QString("curseforge"));
            QCOMPARE(item.value("sourceIds").toMap().value("modrinth").toString(), QString("example"));
            QCOMPARE(item.value("sourceIds").toMap().value("curseforge").toString(), QString("11"));
            QCOMPARE(item.value("projectId").toString(), QString("example"));
            done = true;
        });
        QTRY_VERIFY(done);
    }
    void curseforgeMissingAuthorsRemainRetryable() {
        QTemporaryDir temp;
        Server server;
        auto input = actual("file");
        input["sourceIds"] = QVariantMap{{"curseforge", "11"}};
        input["installedRelease"] = curseforgeRelease(server.curseforge(11, 101, "file")).toVariant();
        server.json("/version_files", QJsonObject{});
        QJsonObject project{{"id", 11}, {"gameId", 432}, {"name", "CF project"}};
        server.json("/mods", QJsonObject{{"data", QJsonArray{project}}});
        ContentMetadataSource source(std::make_shared<SqliteContentIndex>(temp.path() + "/index.sqlite3"),
                                     nullptr, server.base(), server.base());
        source.setCurseforgeKey("fixture-key");
        bool done = false;
        source.resolve({}, {input}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QVERIFY(!rows.first().toMap().value("metadataResolved").toBool());
            done = true;
        });
        QTRY_VERIFY(done);
        project["authors"] = QJsonArray{QJsonObject{{"name", "Bob"}, {"id", 7}}};
        server.json("/mods", QJsonObject{{"data", QJsonArray{project}}});
        done = false;
        source.resolve({}, {input}, [&](auto rows, QString error) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(rows.first().toMap().value("author").toString(), "Bob");
            QVERIFY(rows.first().toMap().value("metadataResolved").toBool());
            done = true;
        });
        QTRY_VERIFY(done);
    }
    void failedNewVersionAuthorsCannotBePromotedFromOlderProjectMetadata() {
        QTemporaryDir temp;
        Server server;
        const auto path = temp.path() + "/index.sqlite3";
        modrinth(server, "old");
        ContentMetadataSource source(std::make_shared<SqliteContentIndex>(path), nullptr, server.base(),
                                     server.base());
        source.setCurseforgeKey({});
        bool done = false;
        source.resolve({}, {actual("old")}, [&](auto, auto) { done = true; });
        QTRY_VERIFY(done);
        modrinth(server, "new", "example", "v2", "Bob");
        server.statuses["/teams"] = 503;
        done = false;
        source.resolve({}, {actual("new")}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QVERIFY(!rows.first().toMap().value("metadataResolved").toBool());
            done = true;
        });
        QTRY_VERIFY(done);
        server.statuses.remove("/teams");
        server.requests.clear();
        done = false;
        source.resolve({}, {actual("new")}, [&](auto rows, QString error) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(rows.first().toMap().value("author").toString(), "Bob");
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(!server.requests.contains("/version_files"));
        QVERIFY(std::any_of(server.requests.cbegin(), server.requests.cend(),
                            [](auto path) { return path.startsWith("/teams?"); }));
    }
    void omittedAuthorTeamRemainsRetryable() {
        QTemporaryDir temp;
        Server server;
        modrinth(server, "file");
        server.json("/teams", QJsonArray{});
        ContentMetadataSource source(std::make_shared<SqliteContentIndex>(temp.path() + "/index.sqlite3"),
                                     nullptr, server.base(), server.base());
        source.setCurseforgeKey({});
        bool done = false;
        source.resolve({}, {actual("file")}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QVERIFY(!rows.first().toMap().value("metadataResolved").toBool());
            done = true;
        });
        QTRY_VERIFY(done);
        server.json("/teams", QJsonArray{QJsonArray{member("Bob")}});
        done = false;
        source.resolve({}, {actual("file")}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows.first().toMap().value("author").toString(), "Bob");
            done = true;
        });
        QTRY_VERIFY(done);
    }
    void coalescedReadersCanDestroyTheirSourceDuringCompletion_data() {
        QTest::addColumn<bool>("curseforge");
        QTest::newRow("modrinth") << false;
        QTest::newRow("curseforge") << true;
    }
    void coalescedReadersCanDestroyTheirSourceDuringCompletion() {
        QFETCH(bool, curseforge);
        Server server;
        if (curseforge) {
            server.json("/version_files", QJsonObject{});
            server.json("/mods", QJsonObject{{"data", QJsonArray{curseforgeMetadataProject()}}});
        } else
            modrinth(server, "file");
        auto *source = new ContentMetadataSource({}, nullptr, server.base(), server.base());
        source->setCurseforgeKey(curseforge ? QByteArray("fixture-key") : QByteArray{});
        const auto input = curseforge ? knownCurseforgeRow("file") : actual("file");
        int completions = 0;
        source->resolve({}, {input}, [&](auto rows, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(rows.first().toMap().value("author").toString(),
                     curseforge ? QString("CF Author") : QString("Alice"));
            ++completions;
            delete source;
        });
        source->resolve({}, {input}, [&](auto, auto) { ++completions; });
        QTRY_COMPARE(completions, 1);
        QCOMPARE(server.payloads.value("/version_files").size(), 1);
        QCOMPARE(server.payloads.value("/projects").size(), curseforge ? 0 : 1);
        QCOMPARE(server.payloads.value("/teams").size(), curseforge ? 0 : 1);
        QCOMPARE(server.payloads.value("/mods").size(), curseforge ? 1 : 0);
    }
    void largeInventoriesUseBoundedIdentityAndProjectBatches() {
        Server server;
        QHash<QString, QJsonObject> releases;
        QVariantList rows;
        for (int i = 0; i < 501; ++i) {
            const auto id = QString("project-%1").arg(i);
            const auto bytes = id.toUtf8();
            releases[hash(bytes)] = server.modrinth(id, "v1", bytes);
            rows << actual(bytes);
        }
        server.json("/teams", QJsonArray{QJsonArray{member()}});
        server.requested = [&](const QString &path) {
            if (path == "/version_files") {
                const auto request = QJsonDocument::fromJson(server.payloads[path].last()).object();
                const auto hashes = request.value("hashes").toArray();
                QVERIFY(hashes.size() <= 500);
                QJsonObject response;
                for (const auto &hash : hashes)
                    response[hash.toString()] = releases.value(hash.toString());
                server.json(path, response);
            } else if (path == "/projects") {
                const QUrl url("http://fixture" + server.requests.last());
                const auto ids =
                    QJsonDocument::fromJson(QUrlQuery(url).queryItemValue("ids", QUrl::FullyDecoded).toUtf8())
                        .array();
                QVERIFY(ids.size() <= 100);
                QJsonArray response;
                for (const auto &id : ids)
                    response << project(id.toString());
                server.json(path, response);
            }
        };
        ContentMetadataSource source({}, nullptr, server.base(), server.base());
        source.setCurseforgeKey({});
        bool done = false;
        source.resolve({}, rows, [&](auto indexed, QString error) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(indexed.size(), 501);
            for (const auto &value : indexed) {
                QVERIFY(value.toMap().value("metadataResolved").toBool());
                QCOMPARE(value.toMap().value("author").toString(), "Alice");
            }
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(server.payloads.value("/version_files").size(), 2);
        QCOMPARE(server.payloads.value("/projects").size(), 6);
        QCOMPARE(server.payloads.value("/teams").size(), 1);
    }
};
QTEST_GUILESS_MAIN(ContentMetadataTests)
#include "ContentMetadataTests.moc"
