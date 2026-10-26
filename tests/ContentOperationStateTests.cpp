#include "ContentUpdateFixtures.h"
#include "TestPerformance.h"
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
        QVERIFY(test_performance::releaseBudgetMet(elapsed.elapsed(), 1500));
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
    void exactFileRefreshRetainsVerifiedModrinthPriority_data() {
        QTest::addColumn<QString>("category");
        QTest::addColumn<bool>("indexed");
        for (const auto &category : {"mods", "resourcepacks", "shaderpacks", "datapacks"})
            for (bool indexed : {false, true})
                QTest::newRow(qPrintable(QString(category) + QString::number(indexed)))
                    << QString(category) << indexed;
    }
    void exactFileRefreshRetainsVerifiedModrinthPriority() {
        QFETCH(QString, category);
        QFETCH(bool, indexed);
        auto previous = row("MR", category, false);
        previous["sourceIds"] = QVariantMap{{"modrinth", "MR"}};
        previous["title"] = "Modrinth title";
        previous["author"] = "Modrinth author";
        previous["projectUrl"] = "https://modrinth.com/mod/MR";
        previous["icon"] = "modrinth-icon";
        auto actual = previous;
        auto cf = releaseFromVariant(actual.value("installedRelease").toMap());
        cf.provider = "curseforge";
        cf.projectId = "22";
        cf.id = "222";
        cf.version = "CF edition";
        actual["sourceIds"] = QVariantMap{{"curseforge", "22"}};
        actual["provider"] = "curseforge";
        actual["projectId"] = "22";
        actual["installedRelease"] = cf.toVariant();
        actual["version"] = cf.version;
        actual["title"] = "CurseForge title";
        actual["author"] = "CurseForge author";
        actual["projectUrl"] = "https://www.curseforge.com/minecraft/mc-mods/example";
        actual["icon"] = "curseforge-icon";
        actual["updateAvailable"] = false;
        actual.remove("updateRelease");
        actual["metadataResolved"] = true;
        actual["metadataSha1"] = actual.value("sha1");
        const auto merged = reconcileContentRows({actual}, {previous}, {}, indexed).first().toMap();
        QCOMPARE(merged.value("sourceIds").toMap(), QVariantMap({{"modrinth", "MR"}, {"curseforge", "22"}}));
        for (const auto &field : {"provider", "projectId", "title", "author", "projectUrl", "icon",
                                  "installedRelease", "version"})
            QCOMPARE(merged.value(field), previous.value(field));
        for (const auto &field : {"relativePath", "fileName", "sha1", "enabled"})
            QCOMPARE(merged.value(field), actual.value(field));
        QVERIFY(!merged.value("updateAvailable").toBool());
        QVERIFY(!merged.contains("updateRelease"));
        QVERIFY(!merged.value("metadataResolved").toBool());
        QVERIFY(!merged.contains("metadataSha1"));
    }
    void exactFileConflictsDoNotBorrowForeignMetadata_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<bool>("indexed");
        QTest::addColumn<bool>("renamed");
        for (const auto &provider : {QString("modrinth"), QString("curseforge")})
            for (bool indexed : {false, true})
                for (bool renamed : {false, true})
                    QTest::newRow(qPrintable(provider + QString::number(indexed) + QString::number(renamed)))
                        << provider << indexed << renamed;
    }
    void exactFileConflictsDoNotBorrowForeignMetadata() {
        QFETCH(QString, provider);
        QFETCH(bool, indexed);
        QFETCH(bool, renamed);
        const auto other = provider == "modrinth" ? "curseforge" : "modrinth";
        auto old = row("old");
        old["sourceIds"] = QVariantMap{{provider, "old"}, {other, "unrelated-alias"}};
        old["provider"] = provider;
        auto oldRelease = releaseFromVariant(old.value("installedRelease").toMap());
        oldRelease.provider = provider;
        old["installedRelease"] = oldRelease.toVariant();
        old["author"] = "Old author";
        auto actual = row("new");
        actual["sha1"] = old.value("sha1");
        actual["sourceIds"] = QVariantMap{{provider, "new"}};
        actual["provider"] = provider;
        auto newRelease = releaseFromVariant(actual.value("installedRelease").toMap());
        newRelease.provider = provider;
        newRelease.sha1 = oldRelease.sha1;
        actual["installedRelease"] = newRelease.toVariant();
        actual.remove("author");
        if (!renamed)
            actual["relativePath"] = old.value("relativePath");
        const auto merged = reconcileContentRows({actual}, {old}, {}, indexed).first().toMap();
        QCOMPARE(merged.value("sourceIds"), actual.value("sourceIds"));
        QCOMPARE(merged.value("title"), actual.value("title"));
        QCOMPARE(merged.value("installedRelease"), actual.value("installedRelease"));
        QVERIFY(!merged.contains("author"));
        QCOMPARE(contentRowKey(merged), contentRowKey(actual));
    }
    void retainedPredictionsRequireExactProof_data() {
        QTest::addColumn<QString>("fault");
        for (const auto &fault :
             {"none", "hash-case", "empty-title", "hash-newline", "category", "path", "invalid-hash",
              "different-hash", "missing-project", "missing-version", "unsupported-provider"})
            QTest::newRow(fault) << QString(fault);
    }
    void retainedPredictionsRequireExactProof() {
        QFETCH(QString, fault);
        auto actual = row("MR");
        actual["sourceIds"] = QVariantMap{{"curseforge", "22"}};
        actual.remove("installedRelease");
        auto release = releaseFromVariant(row("MR").value("installedRelease").toMap());
        if (fault == "hash-case")
            release.sha1 = release.sha1.toUpper();
        if (fault == "hash-newline")
            actual["sha1"] = release.sha1 = release.sha1 + '\n';
        if (fault == "category")
            actual["category"] = "resourcepacks";
        if (fault == "path")
            actual["relativePath"] = "";
        if (fault == "invalid-hash")
            actual["sha1"] = release.sha1 = QString(40, 'z');
        if (fault == "different-hash")
            release.sha1 = hash("foreign bytes");
        if (fault == "missing-project")
            release.projectId.clear();
        if (fault == "missing-version")
            release.id.clear();
        if (fault == "unsupported-provider")
            release.provider = "unknown";
        const auto before = actual;
        const auto metadata = contentRetainedPrediction(
            {release, fault == "empty-title" ? QString{} : QString("MR"), {{"modrinth", "MR"}}, "mod"},
            actual);
        QCOMPARE(actual, before);
        if (fault != "none" && fault != "hash-case" && fault != "empty-title") {
            QVERIFY(metadata.isEmpty());
            return;
        }
        QCOMPARE(metadata.value("relativePath"), actual.value("relativePath"));
        QCOMPARE(metadata.value("fileName"), actual.value("fileName"));
        QCOMPARE(metadata.value("title"), actual.value("title"));
        QCOMPARE(metadata.value("sourceIds").toMap(),
                 QVariantMap({{"modrinth", "MR"}, {"curseforge", "22"}}));
    }
    void freshModrinthPresentationIsNotDowngradedToCachedCurseforge_data() {
        QTest::addColumn<bool>("indexed");
        QTest::newRow("raw") << false;
        QTest::newRow("indexed") << true;
    }
    void freshModrinthPresentationIsNotDowngradedToCachedCurseforge() {
        QFETCH(bool, indexed);
        auto actual = row("MR");
        actual["sourceIds"] = QVariantMap{{"modrinth", "MR"}};
        actual["title"] = "Fresh Modrinth";
        actual["author"] = "Fresh author";
        auto previous = actual;
        auto cf = releaseFromVariant(previous.value("installedRelease").toMap());
        cf.provider = "curseforge";
        cf.projectId = "22";
        cf.id = "222";
        previous["installedRelease"] = cf.toVariant();
        previous["provider"] = "curseforge";
        previous["projectId"] = "22";
        previous["sourceIds"] = QVariantMap{{"curseforge", "22"}};
        previous["title"] = "Cached CurseForge";
        previous["author"] = "Cached author";
        const auto merged = reconcileContentRows({actual}, {previous}, {}, indexed).first().toMap();
        for (const auto &field : {"provider", "projectId", "title", "author", "installedRelease", "version"})
            QCOMPARE(merged.value(field), actual.value(field));
    }
    void retainedCompletionUsesItsOwnProjectFilters_data() {
        QTest::addColumn<bool>("emptyTags");
        QTest::addColumn<bool>("indexed");
        for (bool empty : {false, true})
            for (bool indexed : {false, true})
                QTest::newRow(qPrintable(QString::number(empty) + QString::number(indexed)))
                    << empty << indexed;
    }
    void retainedCompletionUsesItsOwnProjectFilters() {
        QFETCH(bool, emptyTags);
        QFETCH(bool, indexed);
        auto previous = row("MR");
        previous["sourceIds"] = QVariantMap{{"modrinth", "MR"}};
        previous["metadataResolved"] = true;
        previous["metadataSha1"] = previous.value("sha1");
        previous["tags"] = emptyTags ? QVariantList{} : QVariantList{"mr-tag"};
        previous["categories"] = emptyTags ? QStringList{} : QStringList{"mr-category"};
        previous["environment"] = "client";
        previous["openSource"] = false;
        auto actual = previous;
        auto cf = releaseFromVariant(actual.value("installedRelease").toMap());
        cf.provider = "curseforge";
        cf.projectId = "22";
        cf.id = "222";
        actual["sourceIds"] = QVariantMap{{"curseforge", "22"}};
        actual["installedRelease"] = cf.toVariant();
        actual["provider"] = "curseforge";
        actual["projectId"] = "22";
        actual["tags"] = QVariantList{"cf-tag"};
        actual["categories"] = QStringList{"cf-category"};
        actual["environment"] = "server";
        actual["openSource"] = true;
        const auto merged = reconcileContentRows({actual}, {previous}, {}, indexed).first().toMap();
        QVERIFY(merged.value("metadataResolved").toBool());
        for (const auto &field : {"tags", "categories", "environment", "openSource"})
            QCOMPARE(merged.value(field), previous.value(field));
    }
    void malformedCachedReleaseCannotReplaceVerifiedSourceRelease_data() {
        QTest::addColumn<QString>("fault");
        for (const auto &fault :
             {"hash", "missing-hash", "missing-version", "foreign-project", "invalid-hash"})
            QTest::newRow(fault) << QString(fault);
    }
    void malformedCachedReleaseCannotReplaceVerifiedSourceRelease() {
        QFETCH(QString, fault);
        auto previous = row("MR");
        previous["sourceIds"] = QVariantMap{{"modrinth", "MR"}};
        auto actual = previous;
        auto cf = releaseFromVariant(actual.value("installedRelease").toMap());
        cf.provider = "curseforge";
        cf.projectId = "22";
        cf.id = "222";
        actual["sourceIds"] = QVariantMap{{"curseforge", "22"}};
        actual["provider"] = "curseforge";
        actual["projectId"] = "22";
        actual["installedRelease"] = cf.toVariant();
        auto bad = previous.value("installedRelease").toMap();
        if (fault == "hash")
            bad["sha1"] = hash("different bytes");
        if (fault == "missing-hash")
            bad.remove("sha1");
        if (fault == "missing-version")
            bad.remove("id");
        if (fault == "foreign-project")
            bad["projectId"] = "another-project";
        if (fault == "invalid-hash") {
            previous["sha1"] = actual["sha1"] = QString(40, 'z');
            bad["sha1"] = QString(40, 'z');
        }
        previous["installedRelease"] = bad;
        const auto merged = reconcileContentRows({actual}, {previous}, {}, true).first().toMap();
        QCOMPARE(merged.value("provider"), actual.value("provider"));
        QCOMPARE(merged.value("installedRelease"), actual.value("installedRelease"));
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
