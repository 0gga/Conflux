#include "features/showcase/ProjectShowcase.h"
#include "features/showcase/ReleaseFilters.h"
#include "platform/CatalogueShowcaseSource.h"
#include "services/ContentCatalogue.h"
#include "services/GalleryImages.h"
#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
using namespace studio;

// Explicitly injected loopback endpoints, never live providers or owner files.
class LoadingHttp final : public QTcpServer {
  public:
    QHash<QString, QByteArray> responses;
    QHash<QString, int> statuses;
    QSet<QString> held;
    QStringList requests;
    QHash<QString, QList<QPointer<QTcpSocket>>> waiting;
    LoadingHttp() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto input = socket->property("input").toByteArray() + socket->readAll();
                    socket->setProperty("input", input);
                    if (!input.contains("\r\n\r\n") || socket->property("answered").toBool())
                        return;
                    socket->setProperty("answered", true);
                    const auto path = QUrl(QString::fromUtf8(input.split(' ').value(1))).path();
                    requests << path;
                    if (held.contains(path))
                        waiting[path] << socket;
                    else
                        respond(socket, path);
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QUrl base() const {
        return QUrl("http://127.0.0.1:" + QString::number(serverPort()) + '/');
    }
    void release(const QString &path) {
        held.remove(path);
        for (const auto &socket : waiting.take(path))
            if (socket)
                respond(socket, path);
    }
    void respond(QTcpSocket *socket, const QString &path) {
        const auto body = responses.value(path, "{}");
        const int status = statuses.value(path, 200);
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }
    void project(const QString &id, const QStringList &images = {}) {
        QJsonArray gallery;
        for (const auto &image : images)
            gallery << QJsonObject{{"url", image}};
        responses["/project/" + id] =
            QJsonDocument(QJsonObject{{"id", id},
                                      {"title", id},
                                      {"project_type", "mod"},
                                      {"body", "# " + id + "\n\nReady before versions or gallery."},
                                      {"gallery", gallery}})
                .toJson();
        responses["/project/" + id + "/version"] =
            QJsonDocument(QJsonArray{QJsonObject{{"id", id + "-version"},
                                                 {"project_id", id},
                                                 {"version_number", "1.0"},
                                                 {"date_published", "2026-10-22T10:00:00Z"},
                                                 {"game_versions", QJsonArray{"1.21.1"}},
                                                 {"loaders", QJsonArray{"neoforge"}}}})
                .toJson();
    }
    void image(const QString &path, QColor color) {
        QImage image(1800, 1000, QImage::Format_RGB32);
        image.fill(color);
        QBuffer buffer(&responses[path]);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }
};
class UnusedIdentity final : public IInstalledModIdentity {
  public:
    InstalledModIdentity inspect(const QVariantMap &, const QVariantMap &) const override {
        return {};
    }
};
class ShowcaseLoadingTests final : public QObject {
    Q_OBJECT
    static QVariantMap seed(QString id) {
        return {{"provider", "modrinth"}, {"id", id}, {"title", id}, {"type", "mod"}};
    }
  private slots:
    void versionCancellationAbortsTransportWithoutCancellingUpdateIdentification() {
        LoadingHttp http;
        http.project("first");
        http.project("second");
        http.held = {"/project/first/version", "/version_files"};
        ContentCatalogue catalogue(nullptr, http.base(), http.base());
        catalogue.setCurseforgeKey({});
        int cancelled = 0, identified = 0, latest = 0;
        catalogue.versions({{"category", "mods"}, {"sourceIds", QVariantMap{{"modrinth", "first"}}}},
                           "1.21.1", "neoforge", [&](auto releases, QString error) {
                               QVERIFY(releases.isEmpty());
                               QVERIFY(error.contains("cancelled"));
                               ++cancelled;
                           });
        catalogue.refresh({QVariantMap{{"sha1", QString(40, 'a')}, {"category", "mods"}}}, "1.21.1",
                          "neoforge", [&](auto, QString error) {
                              QVERIFY2(error.isEmpty(), qPrintable(error));
                              ++identified;
                          });
        QTRY_VERIFY(!http.waiting.value("/project/first/version").isEmpty());
        QTRY_VERIFY(!http.waiting.value("/version_files").isEmpty());
        const auto oldSocket = http.waiting.value("/project/first/version").first();
        const auto indexSocket = http.waiting.value("/version_files").first();
        catalogue.cancelVersions();
        QCOMPARE(cancelled, 1);
        QTRY_VERIFY(!oldSocket || oldSocket->state() == QAbstractSocket::UnconnectedState);
        QVERIFY(indexSocket && indexSocket->state() == QAbstractSocket::ConnectedState);
        QCOMPARE(identified, 0);
        catalogue.versions({{"category", "mods"}, {"sourceIds", QVariantMap{{"modrinth", "second"}}}},
                           "1.21.1", "neoforge", [&](auto releases, QString error) {
                               QVERIFY2(error.isEmpty(), qPrintable(error));
                               QCOMPARE(releases.size(), 1);
                               ++latest;
                           });
        QTRY_COMPARE(latest, 1);
        http.release("/version_files");
        QTRY_COMPARE(identified, 1);
        QCOMPARE(cancelled, 1);
    }
    void cancelledCoalescedReadsCannotConsumeNewPendingWork() {
        LoadingHttp http;
        http.held = {"/search"};
        http.responses["/search"] = "{\"hits\":[],\"total_hits\":0}";
        CatalogueClient client(nullptr, http.base(), http.base());
        client.setReadCacheLifetime(60000);
        CatalogueQuery query;
        query.provider = "modrinth";
        int old = 0, current = 0;
        client.fetch(query, 0, 20, [&](auto) { ++old; });
        client.fetch(query, 0, 20, [&](auto) { ++old; });
        QTRY_COMPARE(http.requests.count("/search"), 1);
        client.cancelPendingReads();
        client.fetch(query, 0, 20, [&](auto page) {
            QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
            ++current;
        });
        QTRY_COMPARE(http.requests.count("/search"), 2);
        http.release("/search");
        QTRY_COMPARE(current, 1);
        QCOMPARE(old, 0);
        client.fetch(query, 0, 20, [&](auto) { ++old; }); // Cached callback is queued, then cancelled.
        client.cancelPendingReads();
        QTest::qWait(20);
        QCOMPARE(old, 0);
        client.fetch(query, 0, 20, [&](auto page) {
            QVERIFY(page.error.isEmpty());
            ++current;
        });
        QTRY_COMPARE(current, 2);
        QCOMPARE(http.requests.count("/search"), 2);
    }
    void reentrantCancellationStopsRemainingCoalescedCallbacks() {
        LoadingHttp http;
        http.responses["/search"] = "{\"hits\":[],\"total_hits\":0}";
        CatalogueClient client(nullptr, http.base(), http.base());
        client.setReadCacheLifetime(60000);
        CatalogueQuery query;
        query.provider = "modrinth";
        int first = 0, obsolete = 0, replacement = 0;
        client.fetch(query, 0, 20, [&](auto) {
            ++first;
            client.cancelPendingReads();
            client.fetch(query, 0, 20, [&](auto) { ++replacement; });
        });
        client.fetch(query, 0, 20, [&](auto) { ++obsolete; });
        QTRY_COMPARE(replacement, 1);
        QCOMPARE(first, 1);
        QCOMPARE(obsolete, 0);
        QCOMPARE(http.requests.count("/search"), 1);
    }
    void descriptionAndVersionsDoNotWaitForGallery() {
        LoadingHttp http;
        const auto imageUrl = http.base().resolved(QUrl("image")).toString();
        http.project("first", {imageUrl});
        http.image("/image", Qt::cyan);
        http.held = {"/project/first/version", "/image"};
        auto source = std::make_shared<CatalogueShowcaseSource>(nullptr, http.base(), http.base(), true);
        source->setCurseforgeKey({});
        ProjectShowcase page(source, std::make_shared<UnusedIdentity>());
        page.openProject({}, seed("first"));
        QTRY_VERIFY(!page.descriptionBlocks().isEmpty());
        QVERIFY(page.busy());
        QVERIFY(page.versions().isEmpty());
        QCOMPARE(http.requests.count("/project/first"), 1); // Resolve and body share parsed JSON.
        http.release("/project/first/version");
        QTRY_VERIFY(!page.busy());
        QCOMPARE(page.versions().size(), 1);
        QVERIFY(page.gallery().isEmpty());
        QVERIFY(page.galleryBusy());
        QTRY_VERIFY(http.requests.contains("/image"));
        http.release("/image");
        QTRY_COMPARE(page.gallery(), QStringList{imageUrl});
        QVERIFY(!page.galleryBusy());
        const auto requests = http.requests;
        page.close();
        page.openProject({}, seed("first"));
        QTRY_VERIFY(!page.busy());
        QTRY_COMPARE(page.gallery(), QStringList{imageUrl});
        QCOMPARE(http.requests, requests); // Full repeat route: zero HTTP reads or image decodes.
    }
    void secondaryProviderResolutionDoesNotBlockPrimaryDescription() {
        LoadingHttp http;
        http.project("primary");
        http.held.insert("/mods/42");
        http.statuses["/mods/42"] = 500;
        auto source = std::make_shared<CatalogueShowcaseSource>(nullptr, http.base(), http.base(), true);
        source->setCurseforgeKey("fixture");
        ProjectShowcase page(source, std::make_shared<UnusedIdentity>());
        auto project = seed("primary");
        project["sourceIds"] = QVariantMap{{"modrinth", "primary"}, {"curseforge", "42"}};
        page.openProject({}, project);
        QTRY_VERIFY(!page.descriptionBlocks().isEmpty());
        QVERIFY(page.busy());
        QVERIFY(page.description().contains("primary"));
        http.release("/mods/42");
        QTRY_VERIFY(!page.busy());
        QVERIFY(!page.notice().isEmpty()); // Failure remains visible beside usable primary content.
        QCOMPARE(page.project().value("provider").toString(), QString("modrinth"));
    }
    void staleNavigationAndGalleryCannotReplaceNewPage() {
        LoadingHttp http;
        http.project("old", {http.base().resolved(QUrl("image")).toString()});
        http.image("/image", Qt::magenta);
        http.project("new");
        http.held = {"/image"};
        auto source = std::make_shared<CatalogueShowcaseSource>(nullptr, http.base(), http.base(), true);
        source->setCurseforgeKey({});
        ProjectShowcase page(source, std::make_shared<UnusedIdentity>());
        page.openProject({}, seed("old"));
        QTRY_VERIFY(http.requests.contains("/image"));
        page.openProject({}, seed("new"));
        http.release("/image");
        QTRY_VERIFY(!page.busy());
        QCOMPARE(page.project().value("id").toString(), QString("new"));
        QVERIFY(page.description().contains("new"));
        QVERIFY(page.gallery().isEmpty());
        page.close();
        QTest::qWait(20);
        QVERIFY(page.project().isEmpty());
    }
    void sharedReadsCacheAndCredentialInvalidation() {
        LoadingHttp http;
        http.responses["/mods/1/files/2/changelog"] = R"({"data":"Changes"})";
        http.held = {"/mods/1/files/2/changelog"};
        CatalogueClient client(nullptr, http.base(), http.base());
        client.setReadCacheLifetime(60000);
        client.setCurseforgeKey("fixture");
        Release release;
        release.provider = "curseforge";
        release.projectId = "1";
        release.id = "2";
        int count = 0;
        auto done = [&](QString text, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(text, QString("Changes"));
            ++count;
        };
        for (int i = 0; i < 20; ++i)
            client.loadChangelog(release, done);
        QTRY_COMPARE(http.requests.size(), 1);
        http.release("/mods/1/files/2/changelog");
        QTRY_COMPARE(count, 20);
        client.loadChangelog(release, done);
        QCOMPARE(count, 20); // Cache callbacks are queued.
        QTRY_COMPARE(count, 21);
        QCOMPARE(http.requests.size(), 1);
        client.setCurseforgeKey("replacement-fixture");
        client.loadChangelog(release, done);
        QTRY_COMPARE(count, 22);
        QCOMPARE(http.requests.size(), 2);
    }
    void expiredErrorsAndNonPresentationReadsAreNotReused() {
        LoadingHttp http;
        const QString path = "/mods/1/files/2/changelog";
        http.responses[path] = R"({"data":"Changes"})";
        CatalogueClient client(nullptr, http.base(), http.base());
        client.setCurseforgeKey("fixture");
        Release release;
        release.provider = "curseforge";
        release.projectId = "1";
        release.id = "2";
        int count = 0;
        QString error;
        auto done = [&](QString, QString e) {
            error = e;
            ++count;
        };
        client.loadChangelog(release, done);
        QTRY_COMPARE(count, 1);
        client.loadChangelog(release, done);
        QTRY_COMPARE(count, 2);
        QCOMPARE(http.requests.size(), 2);
        client.setReadCacheLifetime(20);
        client.loadChangelog(release, done);
        QTRY_COMPARE(count, 3);
        QTest::qWait(30);
        client.loadChangelog(release, done);
        QTRY_COMPARE(count, 4);
        QCOMPARE(http.requests.size(), 4);
        client.setReadCacheLifetime(60000);
        http.statuses[path] = 500;
        client.loadChangelog(release, done);
        QTRY_COMPARE(count, 5);
        QVERIFY(!error.isEmpty());
        http.statuses.remove(path);
        client.loadChangelog(release, done);
        QTRY_COMPARE(count, 6);
        QVERIFY(error.isEmpty());
        QCOMPARE(http.requests.size(), 6);
    }
    void changedCredentialsRejectAnInflightOldResult() {
        LoadingHttp http;
        const QString path = "/mods/1/files/2/changelog";
        http.responses[path] = R"({"data":"Old connection"})";
        http.held.insert(path);
        CatalogueClient client(nullptr, http.base(), http.base());
        client.setReadCacheLifetime(60000);
        client.setCurseforgeKey("fixture");
        Release release;
        release.provider = "curseforge";
        release.projectId = "1";
        release.id = "2";
        bool called = false;
        client.loadChangelog(release, [&](QString text, QString error) {
            QVERIFY(text.isEmpty());
            QVERIFY(error.contains("connection changed"));
            called = true;
        });
        QTRY_COMPARE(http.requests.size(), 1);
        client.setCurseforgeKey("replacement-fixture");
        http.release(path);
        QTRY_VERIFY(called);
    }
    void cachedQueuedReadsAlsoRejectChangedCredentials() {
        LoadingHttp http;
        http.responses["/mods/1/files/2/changelog"] = R"({"data":"Changes"})";
        CatalogueClient client(nullptr, http.base(), http.base());
        client.setReadCacheLifetime(60000);
        client.setCurseforgeKey("fixture");
        Release release;
        release.provider = "curseforge";
        release.projectId = "1";
        release.id = "2";
        bool done = false;
        client.loadChangelog(release, [&](QString, QString error) {
            QVERIFY(error.isEmpty());
            done = true;
        });
        QTRY_VERIFY(done);
        done = false;
        client.loadChangelog(release, [&](QString text, QString error) {
            QVERIFY(text.isEmpty());
            QVERIFY(error.contains("connection changed"));
            done = true;
        });
        client.setCurseforgeKey("replacement-fixture");
        QTRY_VERIFY(done);
    }
    void cacheEvictsRatherThanGrowingWithLargePages() {
        LoadingHttp http;
        const QString large(3 * 1024 * 1024, 'x');
        CatalogueClient client(nullptr, http.base(), http.base());
        client.setReadCacheLifetime(60000);
        client.setCurseforgeKey("fixture");
        int count = 0;
        auto read = [&](int id) {
            Release release;
            release.provider = "curseforge";
            release.projectId = "1";
            release.id = QString::number(id);
            http.responses["/mods/1/files/" + release.id + "/changelog"] =
                QJsonDocument(QJsonObject{{"data", large}}).toJson();
            client.loadChangelog(release, [&](QString text, QString error) {
                QVERIFY(error.isEmpty());
                QCOMPARE(text, large);
                ++count;
            });
        };
        for (int i = 0; i < 4; ++i) {
            read(i);
            QTRY_COMPARE(count, i + 1);
        }
        QCOMPARE(http.requests.size(), 4);
        read(3);
        QTRY_COMPARE(count, 5);
        QCOMPARE(http.requests.size(), 4);
        read(0);
        QTRY_COMPARE(count, 6);
        QCOMPARE(http.requests.size(), 5);
    }
    void malformedJsonIsNotCachedAndLargeJsonRunsAsynchronously() {
        LoadingHttp http;
        const QString path = "/mods/1/files/2/changelog";
        http.responses[path] = "broken";
        CatalogueClient client(nullptr, http.base(), http.base());
        client.setReadCacheLifetime(60000);
        client.setCurseforgeKey("fixture");
        Release release;
        release.provider = "curseforge";
        release.projectId = "1";
        release.id = "2";
        bool done = false;
        client.loadChangelog(release, [&](QString, QString error) {
            QVERIFY(error.contains("JSON"));
            done = true;
        });
        QTRY_VERIFY(done);
        done = false;
        const QString large(2 * 1024 * 1024, 'x');
        http.responses[path] = QJsonDocument(QJsonObject{{"data", large}}).toJson();
        int beats = 0;
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&] { ++beats; });
        timer.start(1);
        client.loadChangelog(release, [&](QString text, QString error) {
            QVERIFY(error.isEmpty());
            QCOMPARE(text, large);
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(beats > 0);
        QCOMPARE(http.requests.size(), 2);
    }
    void galleryDeduplicatesPixelsCachesSuccessAndRetriesFailures() {
        LoadingHttp http;
        http.image("/one", Qt::cyan);
        http.image("/two", Qt::cyan);
        http.responses["/broken"] = "invalid";
        GalleryImages gallery(nullptr, true);
        QSignalSpy ready(&gallery, &GalleryImages::ready);
        const auto one = http.base().resolved(QUrl("one")).toString(),
                   two = http.base().resolved(QUrl("two")).toString(),
                   broken = http.base().resolved(QUrl("broken")).toString();
        gallery.load({one, one, two, broken}, 1);
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.last().at(1).toStringList(), QStringList{one});
        QVERIFY(!ready.last().at(2).toStringList().isEmpty());
        gallery.load({one, two, broken}, 2);
        QTRY_COMPARE(ready.size(), 2);
        QCOMPARE(http.requests.count("/one"), 1);
        QCOMPARE(http.requests.count("/two"), 1);
        QCOMPARE(http.requests.count("/broken"), 2);
        gallery.load({"file:///private.png", "http://example.invalid/unsafe.png"}, 3);
        QCOMPARE(ready.size(), 3);
        QVERIFY(ready.last().at(1).toStringList().isEmpty());
    }
    void galleryConcurrencyIsBoundedAndCancellationIsSilent() {
        LoadingHttp http;
        QStringList urls;
        for (int i = 0; i < 12; ++i) {
            auto path = "/" + QString::number(i);
            http.held.insert(path);
            urls << http.base().resolved(QUrl(path)).toString();
        }
        GalleryImages gallery(nullptr, true);
        QSignalSpy ready(&gallery, &GalleryImages::ready);
        gallery.load(urls, 1);
        QTRY_COMPARE(http.requests.size(), 4);
        gallery.cancel();
        for (int i = 0; i < 12; ++i)
            http.release("/" + QString::number(i));
        QTest::qWait(30);
        QCOMPARE(ready.size(), 0);
        QCOMPARE(http.requests.size(), 4);
    }
    void repeatedReleaseFilterReadsDoNotRescanVersions() {
        QVariantList rows;
        for (int i = 0; i < 4000; ++i) {
            const QVariantMap release{{"id", QString::number(i)},
                                      {"gameVersions", QStringList{"1.21.1"}},
                                      {"loaders", QStringList{"neoforge"}},
                                      {"provider", "modrinth"}};
            rows << QVariantMap{{"label", QString::number(i)}, {"variants", QVariantList{release}}};
        }
        ReleaseFilters filters;
        filters.setContext("large", rows, {}, {});
        QCOMPARE(filters.rows().size(), 4000);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 10000; ++i) {
            QCOMPARE(filters.rows().size(), 4000);
            QCOMPARE(filters.options().value("loaders").toStringList(), QStringList{"neoforge"});
        }
        const auto elapsed = timer.elapsed();
        qInfo() << "10000 reads of 4000-release filter results in" << elapsed << "ms";
        QVERIFY2(elapsed < 1000, "Memoized release-filter reads exceeded the debug-build budget.");
        filters.configure({{"source", "curseforge"}});
        QVERIFY(filters.rows().isEmpty());
        filters.clear();
        QCOMPARE(filters.rows().size(), 4000);
        filters.setContext("empty", {}, {}, {});
        QVERIFY(filters.rows().isEmpty());
        QVERIFY(filters.options().value("versions").toStringList().isEmpty());
    }
    void groupingThousandsOfSharedVersionVariantsIsLinear() {
        auto releases = [](int size) {
            QVector<Release> result;
            result.reserve(size);
            for (int i = 0; i < size; ++i) {
                Release release;
                release.id = QString::number(i);
                release.version = "1.0";
                release.provider = i % 2 ? "modrinth" : "curseforge";
                result << release;
            }
            return result;
        };
        const auto input = releases(8000);
        QElapsedTimer timer;
        timer.start();
        const auto groups = groupReleaseVariants(input);
        const auto elapsed = timer.elapsed();
        QCOMPARE(groups.size(), 1);
        const auto variants = groups.first().toMap().value("variants").toList();
        QCOMPARE(variants.size(), input.size());
        for (int i = 0; i < variants.size(); ++i)
            QCOMPARE(variants[i].toMap().value("provider").toString(),
                     i < 4000 ? QString("modrinth") : QString("curseforge"));
        qInfo() << "8000 shared-version variants grouped in" << elapsed << "ms";
        QVERIFY2(elapsed < 1500, "Release grouping regressed beyond the generous debug-build budget.");
    }
};
QTEST_MAIN(ShowcaseLoadingTests)
#include "ShowcaseLoadingTests.moc"
