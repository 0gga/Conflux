#include "TestPerformance.h"
#include "domain/Catalogue.h"
#include "platform/CurseforgeFingerprint.h"
#include "platform/SqliteContentIndex.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtConcurrent>
#include <QtEndian>
#include <QtTest>
using namespace studio;
namespace {
QString hash(const QString &name) {
    return QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha1).toHex());
}
QVariantMap row(const QString &name, const QString &version = "1") {
    const auto sha = hash(name + ':' + version);
    Release release;
    release.provider = "modrinth";
    release.projectId = name;
    release.id = name + version;
    release.sha1 = sha;
    release.version = version;
    release.gameVersions = {"1.21.1"};
    release.loaders = {"fabric"};
    return {{"relativePath", "mods/" + name + ".jar"},
            {"sha1", sha},
            {"category", "mods"},
            {"provider", "modrinth"},
            {"projectId", name},
            {"sourceIds", QVariantMap{{"modrinth", name}}},
            {"installedRelease", release.toVariant()},
            {"title", "Project " + name},
            {"author", "Author " + name},
            {"authors", QVariantList{QVariantMap{{"name", "Author " + name},
                                                 {"ids", QVariantMap{{"modrinth", "author-" + name}}}}}},
            {"projectUrl", "https://modrinth.com/mod/" + name},
            {"tags", QStringList{"mr:optimization", "mr:utility"}},
            {"metadataResolved", true}};
}
QVariantMap raw(QVariantMap value) {
    for (const auto &field : {"provider", "projectId", "sourceIds", "installedRelease", "title", "author",
                              "authors", "projectUrl", "tags", "metadataResolved"})
        value.remove(field);
    return value;
}
QVariantList sql(const QString &path, const QString &statement) {
    QVariantList values;
    const auto name = QUuid::createUuid().toString();
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec(statement))
                while (query.next())
                    values << query.value(0);
        }
    }
    QSqlDatabase::removeDatabase(name);
    return values;
}
} // namespace
class ContentIndexTests final : public QObject {
    Q_OBJECT
  private slots:
    void persistentMetadataSurvivesASecondProcessAndRetainsCurrentFileState() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/content.sqlite3";
        auto item = row("example");
        item["enabled"] = true;
        item["locked"] = false;
        item["pending"] = true;
        item["credential"] = "must-never-be-persisted";
        {
            SqliteContentIndex index(path);
            QVERIFY(index.save({{"path", "instance-a"}}, {item}).isEmpty());
        }
        auto current = raw(item);
        current["enabled"] = false;
        current["locked"] = true;
        current["relativePath"] = "mods/renamed.jar.disabled";
        current["pending"] = false;
        SqliteContentIndex reopened(path);
        const auto result = reopened.lookup({{"path", "instance-b"}}, {current});
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const auto restored = result.rows.first().toMap();
        QCOMPARE(restored.value("author").toString(), "Author example");
        QCOMPARE(restored.value("sourceIds"), item.value("sourceIds"));
        QCOMPARE(restored.value("relativePath").toString(), "mods/renamed.jar.disabled");
        QVERIFY(!restored.value("enabled").toBool());
        QVERIFY(restored.value("locked").toBool());
        QVERIFY(!restored.value("pending").toBool());
        const auto stored = sql(path, "SELECT metadata FROM content_file").first().toByteArray();
        QVERIFY(!stored.contains("must-never-be-persisted"));
        for (const auto &field : {"enabled", "locked", "pending", "relativePath"})
            QVERIFY(!QJsonDocument::fromJson(stored).object().contains(field));
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--index-probe", path});
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(10000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
    }
    void changedHashesOrConflictingKnownIdentitiesDoNotBorrowMetadata() {
        QTemporaryDir temp;
        SqliteContentIndex index(temp.path() + "/index.sqlite3");
        const auto original = row("example");
        QVERIFY(index.save({}, {original}).isEmpty());
        auto changed = raw(original);
        changed["sha1"] = hash("different");
        auto conflict = raw(original);
        conflict["sourceIds"] = QVariantMap{{"modrinth", "other"}};
        const auto result = index.lookup({}, {changed, conflict});
        for (const auto &value : result.rows)
            QVERIFY(!value.toMap().value("metadataResolved").toBool());
    }
    void verifiedCurrentReleaseWinsOverSharedFileMetadata() {
        QTemporaryDir temp;
        SqliteContentIndex index(temp.path() + "/index.sqlite3");
        auto cached = row("example");
        QVERIFY(index.save({}, {cached}).isEmpty());
        auto current = raw(cached);
        auto release = cached.value("installedRelease").toMap();
        release["id"] = "separate-forge-release";
        release["version"] = "1-forge";
        release["loaders"] = QStringList{"forge"};
        current["installedRelease"] = release;
        current["version"] = "1-forge";
        current["sourceIds"] = cached.value("sourceIds");
        const auto restored = index.lookup({}, {current}).rows.first().toMap();
        QCOMPARE(restored.value("installedRelease").toMap(), release);
        QCOMPARE(restored.value("version").toString(), "1-forge");
        QCOMPARE(restored.value("author").toString(), "Author example");
        QVERIFY(restored.value("metadataResolved").toBool());
    }
    void updatedProjectTagsAndAuthorsReachOlderExactFileRecords() {
        QTemporaryDir temp;
        SqliteContentIndex index(temp.path() + "/index.sqlite3");
        const auto original = row("example");
        QVERIFY(index.save({{"path", "first"}}, {original}).isEmpty());
        auto newer = row("example", "2");
        newer["tags"] = QStringList{"mr:new-tag"};
        newer["author"] = "New author";
        QVERIFY(index.save({{"path", "second"}}, {newer}).isEmpty());
        const auto restored = index.lookup({}, {raw(original)}).rows.first().toMap();
        QCOMPARE(restored.value("author").toString(), "New author");
        QCOMPARE(restored.value("tags").toStringList(), QStringList{"mr:new-tag"});
        QCOMPARE(restored.value("installedRelease").toMap().value("id").toString(), "example1");
        QCOMPARE(sql(temp.path() + "/index.sqlite3", "SELECT tag FROM project_tag"),
                 QVariantList{QString("mr:new-tag")});
        QCOMPARE(sql(temp.path() + "/index.sqlite3", "SELECT count(*) FROM instance_file").first().toInt(),
                 2);
        QVERIFY(sql(temp.path() + "/index.sqlite3",
                    "SELECT name FROM sqlite_master WHERE type='index' AND name='content_tags'")
                    .size() == 1);
    }
    void failedBatchRollsBackFileProjectTagAndInstanceChanges() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/index.sqlite3";
        SqliteContentIndex index(path);
        const auto original = row("example");
        QVERIFY(index.save({{"path", "a"}}, {original}).isEmpty());
        auto changed = original;
        changed["author"] = "Not committed";
        changed["tags"] = QStringList{"not-committed"};
        auto invalid = row("invalid");
        invalid["sha1"] = hash("mismatched-bytes");
        QVERIFY(!index.save({{"path", "a"}}, {changed, invalid}).isEmpty());
        const auto restored = index.lookup({}, {raw(original)}).rows.first().toMap();
        QCOMPARE(restored.value("author"), original.value("author"));
        QCOMPARE(restored.value("tags"), original.value("tags"));
        QCOMPARE(sql(path, "SELECT count(*) FROM content_file").first().toInt(), 1);
        QCOMPARE(sql(path, "SELECT count(*) FROM instance_file").first().toInt(), 1);
    }
    void unrecognizedDatabaseSchemasRemainUntouchedAndVisible() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/index.sqlite3";
        sql(path, "CREATE TABLE unrelated(value TEXT)");
        sql(path, "INSERT INTO unrelated VALUES('retain')");
        SqliteContentIndex index(path);
        QVERIFY(!index.save({}, {row("example")}).isEmpty());
        QVERIFY(!index.lookup({}, {}).error.isEmpty());
        QCOMPARE(sql(path, "SELECT value FROM unrelated"), QVariantList{QString("retain")});
    }
    void unresolvedFilesAreIndexedForMembershipWithoutInventingProjectMetadata() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/index.sqlite3";
        SqliteContentIndex index(path);
        const auto unknown = raw(row("unpublished"));
        QVERIFY(index.save({{"path", "a"}}, {unknown}).isEmpty());
        QVERIFY(!index.lookup({}, {unknown}).rows.first().toMap().value("metadataResolved").toBool());
        QCOMPARE(sql(path, "SELECT count(*) FROM content_file").first().toInt(), 0);
        QCOMPARE(sql(path, "SELECT count(*) FROM instance_file").first().toInt(), 1);
    }
    void identifiedFilesRetainExactIdentityWhileFailedAuthorsRemainRetryable() {
        QTemporaryDir temp;
        SqliteContentIndex index(temp.path() + "/index.sqlite3");
        auto partial = row("example");
        partial["identityResolved"] = true;
        partial["metadataResolved"] = false;
        partial.remove("author");
        partial.remove("authors");
        QVERIFY(index.save({}, {partial}).isEmpty());
        auto restored = index.lookup({}, {raw(partial)}).rows.first().toMap();
        QVERIFY(restored.value("identityResolved").toBool());
        QVERIFY(!restored.value("metadataResolved").toBool());
        QCOMPARE(restored.value("sourceIds"), partial.value("sourceIds"));
        QVERIFY(index.save({}, {row("example", "2")}).isEmpty());
        restored = index.lookup({}, {raw(partial)}).rows.first().toMap();
        QVERIFY(restored.value("metadataResolved").toBool());
        QCOMPARE(restored.value("author").toString(), "Author example");
    }
    void fingerprintsPersistEvenWhenNoProviderMatchExistsAndNeverEraseKnownMetadata() {
        QTemporaryDir temp;
        SqliteContentIndex index(temp.path() + "/index.sqlite3");
        const auto original = row("example");
        auto unmatched = raw(original);
        unmatched["curseforgeFingerprint"] = QVariant::fromValue(quint64(3456789012));
        QVERIFY(index.save({}, {unmatched}).isEmpty());
        auto cached = index.lookup({}, {raw(original)}).rows.first().toMap();
        QCOMPARE(cached.value("curseforgeFingerprint").toULongLong(), quint64(3456789012));
        QVERIFY(!cached.value("metadataResolved").toBool());
        QVERIFY(index.save({}, {original}).isEmpty());
        QVERIFY(index.save({}, {unmatched}).isEmpty());
        cached = index.lookup({}, {raw(original)}).rows.first().toMap();
        QVERIFY(cached.value("metadataResolved").toBool());
        QCOMPARE(cached.value("author"), original.value("author"));
        QCOMPARE(cached.value("curseforgeFingerprint").toULongLong(), quint64(3456789012));
    }
    void streamingFingerprintsMatchTheIndependentWholeBufferReference_data() {
        QTest::addColumn<QByteArray>("bytes");
        for (int size : {0, 1, 2, 3, 4, 5, 255, 262143, 262145}) {
            QByteArray bytes;
            for (int i = 0; i < size; ++i)
                bytes += char((i * 47) % 256);
            QTest::newRow(qPrintable(QString::number(size))) << bytes;
        }
        QTest::newRow("whitespace") << QByteArray("a b\tc\nd\re f");
    }
    void streamingFingerprintsMatchTheIndependentWholeBufferReference() {
        QFETCH(QByteArray, bytes);
        QByteArray normalized;
        for (const auto byte : bytes)
            if (byte != 9 && byte != 10 && byte != 13 && byte != 32)
                normalized += byte;
        constexpr quint32 multiplier = 0x5bd1e995;
        quint32 expected = 1 ^ quint32(normalized.size());
        qsizetype position = 0;
        while (position + 4 <= normalized.size()) {
            auto block = qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(normalized.constData() + position));
            block *= multiplier;
            block ^= block >> 24;
            block *= multiplier;
            expected = (expected * multiplier) ^ block;
            position += 4;
        }
        const auto tail = normalized.size() - position;
        for (qsizetype i = 0; i < tail; ++i)
            expected ^= quint32(quint8(normalized[position + i])) << (8 * i);
        if (tail)
            expected *= multiplier;
        expected ^= expected >> 13;
        expected *= multiplier;
        expected ^= expected >> 15;
        QTemporaryDir temp;
        const auto path = temp.path() + "/fixture.jar";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        const auto sha1 =
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex());
        const auto fingerprint = curseforgeFingerprint(path, sha1);
        QVERIFY2(fingerprint.error.isEmpty(), qPrintable(fingerprint.error));
        QCOMPARE(fingerprint.value, expected);
        QVERIFY(!curseforgeFingerprint(path, QString(40, '0')).error.isEmpty());
    }
    void staleSnapshotJobsCannotReplaceNewerInstanceMembership() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/index.sqlite3";
        SqliteContentIndex index(path);
        const QVariantMap pack{{"path", "a"}};
        const auto first = index.beginSnapshot(pack), second = index.beginSnapshot(pack);
        const auto newer = raw(row("newer")), older = raw(row("older"));
        QVERIFY(index.lookup(pack, {newer}, second).error.isEmpty());
        QVERIFY(index.lookup(pack, {older}, first).error.isEmpty());
        QCOMPARE(sql(path, "SELECT sha1 FROM instance_file"), QVariantList{newer.value("sha1")});
    }
    void largePersistentLookupsHaveBoundedLocalCost() {
        QTemporaryDir temp;
        SqliteContentIndex index(temp.path() + "/index.sqlite3");
        QVariantList known, actual;
        for (int i = 0; i < 3000; ++i) {
            const auto item = row(QString::number(i));
            known << item;
            actual << raw(item);
        }
        QVERIFY(index.save({{"path", "large"}}, known).isEmpty());
        QElapsedTimer elapsed;
        elapsed.start();
        const auto result = index.lookup({}, actual);
        const auto milliseconds = elapsed.elapsed();
        qInfo() << "3000 persisted exact metadata lookups ms" << milliseconds;
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.rows.size(), 3000);
        for (const auto &value : result.rows)
            QVERIFY(value.toMap().value("metadataResolved").toBool());
        QVERIFY(test_performance::releaseBudgetMet(milliseconds, 750));
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == "--index-probe") {
        SqliteContentIndex index(app.arguments().value(2));
        const auto result = index.lookup({}, {raw(row("example"))});
        return result.error.isEmpty() && result.rows.first().toMap().value("author") == "Author example" ? 0
                                                                                                         : 2;
    }
    ContentIndexTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "ContentIndexTests.moc"
