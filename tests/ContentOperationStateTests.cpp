#include "ContentUpdateFixtures.h"
#include "features/content/ContentOperationReservations.h"
#include "services/PackRepository.h"
#include <QElapsedTimer>
#include <QFile>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QtTest>
using namespace update_fixture;
class ContentOperationStateTests : public QObject {
    Q_OBJECT
    static QVariantList referenceProjection(QVariantList rows, const QList<ContentOperation> &operations) {
        for (const auto &operation : operations) {
            if (operation.kind == ContentOperation::Remove) {
                for (int i = rows.size() - 1; i >= 0; --i)
                    if (operation.targets.contains(contentRowKey(rows.at(i).toMap())))
                        rows.removeAt(i);
            } else if (operation.kind == ContentOperation::Enable) {
                for (auto &value : rows) {
                    auto item = value.toMap();
                    if (operation.targets.contains(contentRowKey(item))) {
                        item["enabled"] = operation.enabled;
                        item["pending"] = true;
                        value = item;
                    }
                }
            } else {
                for (const auto &value : operation.predictions) {
                    auto item = value.toMap();
                    item["pending"] = true;
                    int found = -1;
                    for (int i = 0; i < rows.size(); ++i)
                        if (contentRowKey(rows.at(i).toMap()) == contentRowKey(item)) {
                            found = i;
                            break;
                        }
                    if (found < 0)
                        rows << item;
                    else
                        rows[found] = item;
                }
            }
        }
        return rows;
    }
  private slots:
    void reservationKeysRetainExactAliasesPathsAndCategories() {
        const auto original = row("first");
        ContentOperation operation;
        operation.targets = {contentRowKey(original)};
        auto prediction = original;
        prediction["relativePath"] = "mods/new-name.jar.disabled";
        operation.predictions = {prediction};
        auto release = releaseFromVariant(original.value("updateRelease").toMap());
        operation.requests = {
            {release, "first", QJsonObject{{"modrinth", "first"}, {"curseforge", "cf-first"}}, "mod"}};
        const auto values = ContentOperationReservations::keys(operation, {original, row("ignored")});
        const QSet<QString> keys(values.cbegin(), values.cend());
        QVERIFY(keys.contains("path:mods/first-old.jar"));
        QVERIFY(keys.contains("path:mods/new-name.jar"));
        QVERIFY(keys.contains("project:mods:modrinth:first"));
        QVERIFY(keys.contains("project:mods:curseforge:cf-first"));
        QVERIFY(!keys.contains("project:mods:modrinth:ignored"));
        operation = {};
        operation.sourceLock = {
            {"provider", "curseforge"}, {"projectId", "42"}, {"relativePath", "resourcepacks/unknown.zip"}};
        const auto source = ContentOperationReservations::keys(operation, {});
        QVERIFY(source.contains("path:resourcepacks/unknown.zip"));
        QVERIFY(source.contains("project:resourcepacks:curseforge:42"));
    }
    void reservationsAreAtomicScopedAndReleasedOnlyByTheirOwner() {
        ContentOperationReservations reservations;
        ContentOperation first;
        first.id = 1;
        first.packPath = "instance-a";
        first.reservations = {"path:mods/shared.jar", "project:mods:modrinth:one"};
        QVERIFY(reservations.acquire(first));
        auto conflict = first;
        conflict.id = 2;
        conflict.reservations = {"path:mods/free.jar", "project:mods:modrinth:one"};
        QVERIFY(!reservations.acquire(conflict));
        auto free = conflict;
        free.id = 3;
        free.reservations = {"path:mods/free.jar"};
        QVERIFY(reservations.acquire(free)); // Rejected acquisition left no partial claim.
        auto other = first;
        other.id = 4;
        other.packPath = "instance-b";
        QVERIFY(reservations.acquire(other));
        reservations.release(conflict); // Rejected/late owner cannot release another command.
        QVERIFY(!reservations.acquire(conflict));
        reservations.release(first);
        reservations.release(free);
        QVERIFY(reservations.acquire(conflict));
        reservations.release(first); // Stale completion cannot release the replacement owner.
        auto replacementConflict = conflict;
        replacementConflict.id = 5;
        QVERIFY(!reservations.acquire(replacementConflict));
        reservations.release(conflict);
        reservations.release(free);
        QVERIFY(reservations.acquire(replacementConflict));
        reservations.release(other);
    }
    void indexedReservationsMatchAnOrderedReferenceAcrossCancellation() {
        ContentOperationReservations reservations;
        QList<ContentOperation> active;
        QRandomGenerator random(0x51c0u);
        for (int i = 0; i < 3000; ++i) {
            if (!active.isEmpty() && random.bounded(3u) == 0) {
                const auto removed = active.takeAt(random.bounded(int(active.size())));
                reservations.release(removed);
                reservations.release(removed);
            }
            ContentOperation operation;
            operation.id = i + 1;
            operation.packPath = "instance-" + QString::number(random.bounded(3u));
            for (int key = 0; key < 3; ++key)
                operation.reservations << "project:mods:modrinth:" + QString::number(random.bounded(70u));
            bool expected = true;
            for (const auto &previous : active)
                if (previous.packPath == operation.packPath)
                    for (const auto &key : operation.reservations)
                        if (previous.reservations.contains(key))
                            expected = false;
            QCOMPARE(reservations.acquire(operation), expected);
            if (expected)
                active << operation;
        }
        for (const auto &operation : active)
            reservations.release(operation);
        for (const auto &operation : active) {
            QVERIFY(reservations.acquire(operation));
            reservations.release(operation);
        }
        ContentOperation invalid;
        QVERIFY(!reservations.acquire(invalid));
    }
    void reservationScopeNormalizesWindowsPaths() {
#ifdef Q_OS_WIN
        ContentOperationReservations reservations;
        ContentOperation first;
        first.id = 1;
        first.packPath = "C:/Instances/Example/";
        first.reservations = {"path:mods/shared.jar"};
        QVERIFY(reservations.acquire(first));
        auto conflict = first;
        conflict.id = 2;
        conflict.packPath = "c:\\instances\\example";
        QVERIFY(!reservations.acquire(conflict));
        reservations.release(first);
        QVERIFY(reservations.acquire(conflict));
#endif
    }
    void thousandsOfReservationClaimsHaveBoundedCpuCost() {
        ContentOperationReservations reservations;
        QList<ContentOperation> operations;
        operations.reserve(12000);
        for (int i = 0; i < 12000; ++i) {
            ContentOperation operation;
            operation.id = i + 1;
            operation.packPath = "large-instance";
            operation.reservations = {"path:mods/" + QString::number(i) + ".jar",
                                      "project:mods:modrinth:" + QString::number(i)};
            operations << operation;
        }
        QElapsedTimer elapsed;
        elapsed.start();
        for (const auto &operation : operations)
            QVERIFY(reservations.acquire(operation));
        for (const auto &operation : operations) {
            auto conflict = operation;
            conflict.id += operations.size();
            QVERIFY(!reservations.acquire(conflict));
        }
        for (const auto &operation : operations)
            reservations.release(operation);
        qInfo() << "12000 reservation acquisition/conflict/release cycles ms" << elapsed.elapsed();
        QVERIFY(elapsed.elapsed() < 1500);
        for (const auto &operation : operations)
            QVERIFY(reservations.acquire(operation));
    }
    void freshIdentificationIsNotReplacedByOldEmptyMetadata() {
        auto old = row("Identity");
        old["sourceIds"] = QVariantMap{};
        old["installedRelease"] = QVariantMap{};
        old["updateAvailable"] = true;
        auto fresh = old;
        const QVariantMap ids{{"modrinth", "Sza3GgEL"}, {"curseforge", "42"}};
        fresh["sourceIds"] = ids;
        fresh["installedRelease"] =
            QVariantMap{{"provider", "modrinth"}, {"projectId", "Sza3GgEL"}, {"id", "wkJ6vtdp"}};
        fresh["provider"] = "modrinth";
        fresh["projectId"] = "Sza3GgEL";
        fresh["title"] = "Fresh project title";
        fresh["updateAvailable"] = false;
        fresh.remove("updateRelease");
        const auto merged = reconcileContentRows({fresh}, {old}, {}, true).first().toMap();
        QCOMPARE(merged.value("title"), fresh.value("title"));
        QCOMPARE(merged.value("sourceIds").toMap(), ids);
        QCOMPARE(merged.value("installedRelease"), fresh.value("installedRelease"));
        QVERIFY(!merged.value("updateAvailable").toBool());
        QVERIFY(!merged.contains("updateRelease"));
        QCOMPARE(contentRowKey(merged), contentRowKey(old));
        auto raw = fresh;
        raw.remove("sourceIds");
        raw.remove("installedRelease");
        raw.remove("updateAvailable");
        QCOMPARE(reconcileContentRows({raw}, {merged}).first().toMap().value("sourceIds").toMap(), ids);
    }
    void predictionsPreserveExistingIdentityAndState_data() {
        QTest::addColumn<QString>("category");
        QTest::addColumn<bool>("managed");
        QTest::addColumn<bool>("enabled");
        for (const auto &category : {"mods", "resourcepacks", "shaderpacks", "datapacks"})
            for (bool managed : {false, true})
                for (bool enabled : {false, true})
                    QTest::newRow(
                        qPrintable(QString("%1-managed%2-enabled%3").arg(category).arg(managed).arg(enabled)))
                        << QString(category) << managed << enabled;
    }
    void predictionsPreserveExistingIdentityAndState() {
        QFETCH(QString, category);
        QFETCH(bool, managed);
        QFETCH(bool, enabled);
        auto old = row("A", category, enabled);
        old["contentKey"] = contentRowKey(old);
        ContentInstallRequest request{releaseFromVariant(old.value("updateRelease").toMap()), "A",
                                      QJsonObject::fromVariantMap(old.value("sourceIds").toMap()),
                                      categoryType(category), false};
        const auto predicted = contentInstallPrediction(request, {{"managed", managed}}, old);
        QCOMPARE(contentRowKey(predicted), contentRowKey(old));
        QCOMPARE(predicted.value("enabled").toBool(), enabled);
        const auto file =
            category == "mods" && managed ? request.release.sha1 + ".jar" : request.release.filename;
        QCOMPARE(predicted.value("relativePath").toString(),
                 category + '/' + file + (enabled ? "" : ".disabled"));
        QCOMPARE(old.value("version").toString(), QString("1.0"));
        QVERIFY(!predicted.value("updateAvailable").toBool());
    }
    void newProjectsNeverShareAnEmptyCategoryKey() {
        QVariantList predictions;
        for (const auto &id : {"A", "B"}) {
            const auto item = row(id);
            ContentInstallRequest request{releaseFromVariant(item.value("updateRelease").toMap()), id,
                                          QJsonObject::fromVariantMap(item.value("sourceIds").toMap()), "mod",
                                          true};
            predictions << contentInstallPrediction(request, {}, {{"title", id}});
        }
        QVERIFY(contentRowKey(predictions[0].toMap()) != contentRowKey(predictions[1].toMap()));
        ContentOperation operation;
        operation.kind = ContentOperation::Install;
        operation.predictions = predictions;
        QCOMPARE(projectContentOperations({}, {operation}).size(), 2);
    }
    void indexedProjectionMatchesOrderedReferenceAcrossMixedIntents() {
        for (quint32 seed = 1; seed <= 200; ++seed) {
            QRandomGenerator random(seed);
            QVariantList rows;
            for (int i = 0; i < 40; ++i)
                rows << row(QString::number(i));
            QList<ContentOperation> operations;
            for (int i = 0; i < 100; ++i) {
                auto item = row(QString::number(random.bounded(60)));
                ContentOperation operation;
                operation.kind = ContentOperation::Kind(random.bounded(4));
                operation.targets = {contentRowKey(item), contentRowKey(item), "missing:key"};
                operation.enabled = random.bounded(2);
                item["version"] = QString::number(i);
                operation.predictions = {item};
                operations << operation;
            }
            const auto projected = projectContentOperations(rows, operations);
            QCOMPARE(projected, referenceProjection(rows, operations));
            QSet<QString> keys;
            for (const auto &value : projected)
                keys.insert(contentRowKey(value.toMap()));
            QCOMPARE(keys.size(), projected.size());
        }
    }
    void resolvedTargetsAreUniqueAndRetainInventoryOrder() {
        const QVariantList rows{row("A"), row("B"), row("C")};
        QCOMPARE(resolveContentTargets(rows, {contentRowKey(rows[2].toMap()), contentRowKey(rows[0].toMap()),
                                              contentRowKey(rows[0].toMap()), "missing:key"}),
                 QVariantList({rows[0], rows[2]}));
    }
    void launcherIdentityOnlyRefreshRetainsVerifiedPresentation() {
        const auto previous = row("Friendly title");
        auto raw = previous;
        raw["title"] = "friendly-title-old.jar";
        raw.remove("installedRelease");
        raw.remove("updateRelease");
        raw.remove("updateAvailable");
        const auto merged = reconcileContentRows({raw}, {previous}).first().toMap();
        QCOMPARE(merged.value("title"), previous.value("title"));
        QCOMPARE(merged.value("sourceIds"), raw.value("sourceIds"));
        QCOMPARE(merged.value("installedRelease"), previous.value("installedRelease"));
    }
    void reconciliationDoesNotAttachOldIdentityToExternallyEditedBytes() {
        const auto old = row("A");
        auto actual = old;
        actual["sha1"] = hash("manual edit");
        actual.remove("sourceIds");
        actual.remove("installedRelease");
        const auto result = reconcileContentRows({actual}, {old}).first().toMap();
        QVERIFY(result.value("sourceIds").toMap().isEmpty());
        QVERIFY(result.value("installedRelease").toMap().isEmpty());
        QCOMPARE(contentRowKey(result), contentRowKey(old));
    }
    void identicalBinariesKeepTheirDistinctPathAndProjectMetadata() {
        auto a = row("A", "resourcepacks"), b = row("B", "resourcepacks");
        b["sha1"] = a.value("sha1");
        auto actualA = a, actualB = b;
        actualA.remove("sourceIds");
        actualB.remove("sourceIds");
        const auto result = reconcileContentRows({actualA, actualB}, {a, b});
        QCOMPARE(result[0].toMap().value("sourceIds"), a.value("sourceIds"));
        QCOMPARE(result[1].toMap().value("sourceIds"), b.value("sourceIds"));
        auto renamed = actualA;
        renamed["relativePath"] = "resourcepacks/renamed.zip";
        const auto ambiguous = reconcileContentRows({renamed}, {a, b}).first().toMap();
        QVERIFY(ambiguous.value("sourceIds").toMap().isEmpty());
    }
    void contentTransactionsDoNotSpawnGitStatusOrHistoryProcesses() {
        QTemporaryDir temp;
        const auto created = PackRepository::create(temp.path(), "Fixture", "1.21.1", "fabric", true);
        QVERIFY(created.ok);
        const auto previousPath = qgetenv("PATH");
        const auto restore = qScopeGuard([previousPath] { qputenv("PATH", previousPath); });
        qputenv("PATH", (temp.path() + "/no-executables").toUtf8());
        const auto read = PackRepository::inspectContent(created.path);
        QVERIFY2(read.ok, qPrintable(read.error));
        QVERIFY(read.status.isEmpty());
        QVERIFY(read.history.isEmpty());
        const auto saved = PackRepository::updateContentReferences(created.path, read.manifest, {}, {});
        QVERIFY2(saved.ok, qPrintable(saved.error));
        const auto applied = PackRepository::applyInstallation(created.path, saved.manifest, {}, temp.path());
        QVERIFY2(applied.ok, qPrintable(applied.error));
        QVERIFY(!PackRepository::inspect(created.path).ok); // Ordinary repository inspection still needs Git.
    }
    void contentInspectionRequiresAValidManifestWithoutRequiringGit() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/pack.json";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"schemaVersion\":1,\"name\":\"Fixture\",\"mods\":[]}");
        file.close();
        QVERIFY(PackRepository::inspectContent(temp.path()).ok); // Valid packs need no Git marker.
        QVERIFY(QDir().mkpath(temp.path() + "/.git"));
        QVERIFY(PackRepository::inspectContent(temp.path()).ok);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("malformed");
        file.close();
        QVERIFY(!PackRepository::inspectContent(temp.path()).ok);
    }
};
QTEST_GUILESS_MAIN(ContentOperationStateTests)
#include "ContentOperationStateTests.moc"
