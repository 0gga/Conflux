#include "features/content/ContentUpdateIntent.h"
#include <QtTest>
using namespace studio;
class ContentUpdateIntentTests final : public QObject {
    Q_OBJECT
    static QVariantMap candidate(const QString &category = "mods", const QString &provider = "modrinth",
                                 bool enabled = true) {
        const auto extension = category == "mods" ? ".jar" : ".zip";
        Release previous;
        previous.provider = provider;
        previous.projectId = provider == "modrinth" ? "mr-exact" : "42";
        previous.id = "old-release";
        previous.version = "1.0";
        previous.filename = "old-name" + QString(extension);
        previous.sha1 = QString(40, 'a');
        previous.gameVersions = {"1.21", "1.21.1"};
        previous.loaders = {"neoforge", "forge"};
        auto next = previous;
        next.id = "new-release";
        next.version = "2.0";
        next.filename = "new-name" + QString(extension);
        next.sha1 = QString(40, 'b');
        next.url = "https://example.invalid/verified-file";
        return {{"title", "Exact project"},
                {"category", category},
                {"contentKey", "stable:original"},
                {"relativePath", category + '/' + previous.filename + (enabled ? "" : ".disabled")},
                {"sha1", previous.sha1},
                {"enabled", enabled},
                {"sourceIds", QVariantMap{{"modrinth", "mr-exact"}, {"curseforge", "42"}}},
                {"installedRelease", previous.toVariant()},
                {"updateRelease", next.toVariant()},
                {"updateAvailable", true},
                {"opaque", QVariantMap{{"retain", true}}}};
    }
  private slots:
    void existingContentIntentKeepsScopeIdentityAndDisabledState_data() {
        QTest::addColumn<QString>("category");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("provider");
        QTest::addColumn<bool>("managed");
        QTest::addColumn<bool>("enabled");
        const QList<QPair<QString, QString>> types{{"mods", "mod"},
                                                   {"resourcepacks", "resourcepack"},
                                                   {"shaderpacks", "shader"},
                                                   {"datapacks", "datapack"}};
        for (const auto &[category, type] : types)
            for (const auto &provider : {"modrinth", "curseforge"})
                for (bool managed : {false, true})
                    for (bool enabled : {false, true})
                        QTest::newRow(qPrintable(
                            QString("%1-%2-%3-%4").arg(category, provider).arg(managed).arg(enabled)))
                            << category << type << QString(provider) << managed << enabled;
    }
    void existingContentIntentKeepsScopeIdentityAndDisabledState() {
        QFETCH(QString, category);
        QFETCH(QString, type);
        QFETCH(QString, provider);
        QFETCH(bool, managed);
        QFETCH(bool, enabled);
        const QVariantMap pack{{"path", "captured-original"}, {"managed", managed}, {"minecraft", "1.21.1"}};
        const auto row = candidate(category, provider, enabled);
        const auto intent = planContentUpdate(pack, row);
        QVERIFY(intent.has_value());
        QCOMPARE(intent->kind, ContentOperation::Install);
        QCOMPARE(intent->packPath, QString("captured-original"));
        QCOMPARE(intent->targets, QStringList{"stable:original"});
        QCOMPARE(intent->requests.size(), 1);
        QCOMPARE(intent->predictions.size(), 1);
        QCOMPARE(intent->id, quint64(0)); // The coordinator assigns IDs and expected hashes.
        QVERIFY(intent->expectedHashes.isEmpty());
        const auto &request = intent->requests.first();
        QCOMPARE(request.contentType, type);
        QCOMPARE(request.installedPath, row.value("relativePath").toString());
        QCOMPARE(request.sourceIds, QJsonObject::fromVariantMap(row.value("sourceIds").toMap()));
        QCOMPARE(request.release.toVariant(), row.value("updateRelease").toMap());
        QCOMPARE(request.release.gameVersions, (QStringList{"1.21", "1.21.1"}));
        QCOMPARE(request.release.loaders, (QStringList{"neoforge", "forge"}));
        QVERIFY(!request.resolveDependencies);
        const auto prediction = intent->predictions.first().toMap();
        const auto filename =
            managed && category == "mods" ? request.release.sha1 + ".jar" : request.release.filename;
        QCOMPARE(prediction.value("relativePath").toString(),
                 category + '/' + filename + (enabled ? "" : ".disabled"));
        QCOMPARE(prediction.value("contentKey"), row.value("contentKey"));
        QCOMPARE(prediction.value("enabled").toBool(), enabled);
        QCOMPARE(prediction.value("sourceIds"), row.value("sourceIds"));
        QCOMPARE(prediction.value("opaque"), row.value("opaque"));
        QVERIFY(!prediction.value("updateAvailable").toBool());
        QCOMPARE(row.value("sha1").toString(), QString(40, 'a'));
    }
    void everyEligibilityFlagCombinationIsRespected_data() {
        QTest::addColumn<int>("mask");
        for (int mask = 0; mask < 16; ++mask)
            QTest::newRow(qPrintable(QString::number(mask))) << mask;
    }
    void everyEligibilityFlagCombinationIsRespected() {
        QFETCH(int, mask);
        auto row = candidate();
        row["updateAvailable"] = bool(mask & 1);
        row["locked"] = bool(mask & 2);
        row["frozen"] = bool(mask & 4);
        row["pending"] = bool(mask & 8);
        QCOMPARE(contentUpdateEligible(row), mask == 1);
        QCOMPARE(planContentUpdate({{"path", "original"}}, row).has_value(), mask == 1);
    }
};
QTEST_GUILESS_MAIN(ContentUpdateIntentTests)
#include "ContentUpdateIntentTests.moc"
