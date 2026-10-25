#include "TestPerformance.h"
#include "domain/ContentTypes.h"
#include "features/content/ContentOperationState.h"
#include <QElapsedTimer>
#include <QtTest>
using namespace studio;
class ContentReadBudgetTests final : public QObject {
    Q_OBJECT
    static QVariantList inventory(int count) {
        QVariantList rows;
        rows.reserve(count);
        for (int i = 0; i < count; ++i)
            rows << QVariantMap{
                {"contentKey", "stable:" + QString::number(i).rightJustified(4, '0')},
                {"category", "mods"},
                {"relativePath", "mods/" + QString(200, 'p') + QString::number(i) + ".jar.disabled"}};
        return rows;
    }
  private slots:
    void contentKeysPreserveExplicitValuesAndFallbackRules_data() {
        QTest::addColumn<QVariantMap>("row");
        QTest::addColumn<QString>("expected");
        const QVariantMap base{{"category", "mods"}, {"relativePath", "mods/example.jar"}};
        QTest::newRow("enabled") << base << QString("mods:mods/example.jar");
        auto row = base;
        row["relativePath"] = "mods/example.jar.disabled";
        QTest::newRow("disabled") << row << QString("mods:mods/example.jar");
        row["relativePath"] = "mods/example.jar.DISABLED";
        QTest::newRow("case-preserved") << row << QString("mods:mods/example.jar.DISABLED");
        row["contentKey"] = "opaque:modrinth:AbCd";
        QTest::newRow("opaque") << row << QString("opaque:modrinth:AbCd");
        row["contentKey"] = QStringLiteral("");
        QTest::newRow("explicit-empty") << row << QStringLiteral("");
        row["contentKey"] = QString{};
        QTest::newRow("explicit-null") << row << QString{};
        row["contentKey"] = QVariant{};
        QTest::newRow("invalid-value") << row << QString{};
        row["contentKey"] = 42;
        QTest::newRow("numeric-value") << row << QString("42");
        QTest::newRow("missing-fields") << QVariantMap{} << QString(":");
        row = base;
        row["relativePath"] = QString::fromUtf8("mods/caf\xc3\xa9.jar.disabled");
        QTest::newRow("unicode-path") << row << QString::fromUtf8("mods:mods/caf\xc3\xa9.jar");
    }
    void contentKeysPreserveExplicitValuesAndFallbackRules() {
        QFETCH(QVariantMap, row);
        QFETCH(QString, expected);
        const auto actual = contentRowKey(row);
        QCOMPARE(actual, expected);
        QCOMPARE(actual.isNull(), expected.isNull());
    }
    void typeMappingsKeepRoundTripsAndUnknownFallbacks() {
        const QList<QPair<QString, QString>> types{{"mod", "mods"},
                                                   {"resourcepack", "resourcepacks"},
                                                   {"shader", "shaderpacks"},
                                                   {"datapack", "datapacks"}};
        for (const auto &[type, category] : types) {
            QCOMPARE(contentCategory(type), category);
            QCOMPARE(categoryType(category), type);
            QCOMPARE(categoryType(contentCategory(type)), type);
        }
        QVERIFY(contentCategory("Mod").isEmpty());
        QVERIFY(contentCategory("unknown").isEmpty());
        QCOMPARE(categoryType("unknown"), QString("mod"));
        QCOMPARE(categoryType({}), QString("mod"));
    }
    void labelsKeepBrandingVersionSuffixesAndUnknownSpelling_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("neoforge") << QString("neoforge") << QString("NeoForge");
        QTest::newRow("neoforge-version") << QString("NEOFORGE-21.1.22") << QString("NeoForge 21.1.22");
        QTest::newRow("fabric-version") << QString("Fabric-0.16.0") << QString("Fabric 0.16.0");
        QTest::newRow("forge-version") << QString("forge-52.1.1") << QString("Forge 52.1.1");
        QTest::newRow("quilt-version") << QString("quilt-loader-0.26") << QString("Quilt loader-0.26");
        QTest::newRow("curseforge") << QString("curseforge") << QString("CurseForge");
        QTest::newRow("modrinth") << QString("MODRINTH") << QString("Modrinth");
        QTest::newRow("unknown") << QString("Unknown-1.0") << QString("Unknown-1.0");
    }
    void labelsKeepBrandingVersionSuffixesAndUnknownSpelling() {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(contentLabel(input), expected);
    }
    void targetResolutionRetainsInventoryOrderAndDuplicateRows() {
        const QVariantMap first{{"contentKey", "one"}, {"value", "first"}},
            second{{"contentKey", "two"}, {"value", "second"}},
            duplicate{{"contentKey", "one"}, {"value", "duplicate"}};
        const QVariantList rows{first, second, duplicate};
        QCOMPARE(resolveContentTargets(rows, {"two", "one", "one"}), rows);
        QCOMPARE(resolveContentTargets(rows, {"one", "one"}), (QVariantList{first, duplicate}));
        QVERIFY(resolveContentTargets(rows, {}).isEmpty());
        QVERIFY(resolveContentTargets({}, {"one"}).isEmpty());
    }
    void stableContentKeyReadsHaveBoundedCpuCost() {
        const auto rows = inventory(256);
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 length = 0;
        for (int i = 0; i < 500000; ++i)
            length += contentRowKey(rows[i % rows.size()].toMap()).size();
        const auto milliseconds = elapsed.elapsed();
        QCOMPARE(length, qint64(5500000));
        qInfo() << "500000 stable content-key reads ms" << milliseconds;
        QVERIFY2(test_performance::releaseBudgetMet(milliseconds, 100),
                 "Stable keys calculate unnecessary fallback state.");
    }
    void smallTypeMappingsHaveBoundedCpuCost() {
        const QStringList types{"mod", "resourcepack", "shader", "datapack"},
            categories{"mods", "resourcepacks", "shaderpacks", "datapacks"};
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 length = 0;
        for (int i = 0; i < 400000; ++i) {
            length += contentCategory(types[i % 4]).size();
            length += categoryType(categories[i % 4]).size();
        }
        const auto milliseconds = elapsed.elapsed();
        QCOMPARE(length, qint64(6600000));
        qInfo() << "800000 content type/category reads ms" << milliseconds;
        QVERIFY2(test_performance::releaseBudgetMet(milliseconds, 150),
                 "Type reads reconstruct small mapping containers.");
    }
    void repeatedLabelsHaveBoundedCpuCost() {
        const QStringList inputs{"neoforge", "Fabric-0.16.0", "forge-52.1.1", "quilt-loader-0.26",
                                 "modrinth", "CurseForge",    "Unknown"};
        const QStringList outputs{"NeoForge", "Fabric 0.16.0", "Forge 52.1.1", "Quilt loader-0.26",
                                  "Modrinth", "CurseForge",    "Unknown"};
        qint64 expected = 0;
        for (const auto &output : outputs)
            expected += output.size() * 50000;
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 length = 0;
        for (int i = 0; i < 350000; ++i)
            length += contentLabel(inputs[i % 7]).size();
        const auto milliseconds = elapsed.elapsed();
        QCOMPARE(length, expected);
        qInfo() << "350000 content label reads ms" << milliseconds;
        QVERIFY2(test_performance::releaseBudgetMet(milliseconds, 125),
                 "Label reads reconstruct temporary loader lists/prefixes.");
    }
    void emptyTargetReadsDoNotScanLargeInventories() {
        const auto rows = inventory(5000);
        QElapsedTimer elapsed;
        elapsed.start();
        qsizetype selected = 0;
        for (int i = 0; i < 1000; ++i)
            selected += resolveContentTargets(rows, {}).size();
        const auto milliseconds = elapsed.elapsed();
        QCOMPARE(selected, qsizetype(0));
        qInfo() << "1000 empty target reads over 5000 rows ms" << milliseconds;
        QVERIFY2(test_performance::releaseBudgetMet(milliseconds, 150),
                 "An empty selection scans unrelated content rows.");
    }
};
QTEST_GUILESS_MAIN(ContentReadBudgetTests)
#include "ContentReadBudgetTests.moc"
