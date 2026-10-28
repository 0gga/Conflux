#include "TestPerformance.h"
#include "features/content/ContentDependencyGraph.h"
#include "features/content/ContentOperationState.h"
#include "features/content/ModMetadataReader.h"
#include <QElapsedTimer>
#include <QtTest>
using namespace studio;
namespace {
QVariantMap node(QString id, QStringList children = {}) {
    QVariantList dependencies;
    for (const auto &child : children)
        dependencies << QVariantMap{{"id", child}, {"required", true}};
    return {{"modId", id},
            {"title", id},
            {"category", "mods"},
            {"sha1", id},
            {"relativePath", "mods/" + id + ".jar"},
            {"enabled", true},
            {"runtimeDependencies", dependencies}};
}
QString key(const QString &id) {
    return contentRowKey(node(id));
}
} // namespace
class ContentDependencyGraphTests final : public QObject {
    Q_OBJECT
  private slots:
    void missingRequirementsUseVerifiedProviderNamespaces_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<bool>("versionOnly");
        QTest::newRow("modrinth-project") << QString("modrinth") << false;
        QTest::newRow("modrinth-version") << QString("modrinth") << true;
        QTest::newRow("curseforge-project") << QString("curseforge") << false;
    }
    void missingRequirementsUseVerifiedProviderNamespaces() {
        QFETCH(QString, provider);
        QFETCH(bool, versionOnly);
        auto root = node("Root"), child = node("Child");
        root["sha1"] = QString(40, 'a');
        child["sha1"] = QString(40, 'b');
        Release release;
        release.provider = provider;
        release.projectId = "root";
        release.id = "root-version";
        release.sha1 = root.value("sha1").toString();
        const QJsonObject ref = provider == "curseforge"
                                    ? QJsonObject{{"modId", 22}, {"relationType", 3}}
                                    : QJsonObject{{"project_id", versionOnly ? "" : "22"},
                                                  {"version_id", versionOnly ? "child-version" : ""},
                                                  {"dependency_type", "required"}};
        release.dependencyRefs = {ref, ref};
        root["installedRelease"] = release.toVariant();
        auto missing = ContentDependencyGraph({root}).missingRequirements();
        QCOMPARE(missing.size(), 1);
        QCOMPARE(missing.first().toMap().value("reference").toMap().value("provider").toString(), provider);
        child["sourceIds"] = QVariantMap{{provider == "modrinth" ? "curseforge" : "modrinth", "22"}};
        QCOMPARE(ContentDependencyGraph({root, child}).missingRequirements().size(), 1);
        if (versionOnly) {
            Release pin;
            pin.provider = provider;
            pin.projectId = "22";
            pin.id = "child-version";
            pin.sha1 = child.value("sha1").toString();
            child["installedRelease"] = pin.toVariant();
        } else
            child["sourceIds"] = QVariantMap{{provider, "22"}};
        child["enabled"] = false;
        const ContentDependencyGraph installed({root, child});
        QVERIFY(installed.missingRequirements().isEmpty());
        QCOMPARE(installed.disabledRequirements().size(), 1);
        root["enabled"] = false;
        QVERIFY(ContentDependencyGraph({root}).missingRequirements().isEmpty());
        root["enabled"] = true;
        root["sha1"] = QString(40, 'c');
        QVERIFY(ContentDependencyGraph({root}).missingRequirements().isEmpty());
        root["sha1"] = release.sha1;
        auto duplicate = root;
        duplicate["relativePath"] = "mods/duplicate.jar";
        QVERIFY(ContentDependencyGraph({root, duplicate}).missingRequirements().isEmpty());
        release.dependencyRefs = {provider == "curseforge"
                                      ? QJsonObject{{"modId", 22}, {"relationType", 2}}
                                      : QJsonObject{{"project_id", "22"}, {"dependency_type", "optional"}}};
        root["installedRelease"] = release.toVariant();
        QVERIFY(ContentDependencyGraph({root}).missingRequirements().isEmpty());
    }
    void disabledRequirementsOnlyIncludeEnabledRequiredParents() {
        auto root = node("Root", {"Child"}), other = node("Other", {"Child"}), child = node("Child");
        child["enabled"] = false;
        auto relations = ContentDependencyGraph({root, other, child}).disabledRequirements();
        QCOMPARE(relations.size(), 1);
        QCOMPARE(relations.first().toMap().value("requiredBy").toStringList(),
                 QStringList({"Other", "Root"}));
        root["enabled"] = false;
        other["runtimeDependencies"] = QVariantList{QVariantMap{{"id", "Child"}, {"required", false}}};
        QVERIFY(ContentDependencyGraph({root, other, child}).disabledRequirements().isEmpty());
        root["enabled"] = true;
        child["enabled"] = true;
        QVERIFY(ContentDependencyGraph({root, child}).disabledRequirements().isEmpty());
        child["enabled"] = false;
        auto duplicate = child;
        duplicate["relativePath"] = "mods/duplicate.jar";
        QVERIFY(ContentDependencyGraph({root, child, duplicate}).disabledRequirements().isEmpty());
    }
    void sharedLockedAndTransitiveDependenciesAreKept() {
        auto a = node("A", {"B"}), b = node("B", {"C"}), c = node("C"), d = node("D", {"B"});
        ContentDependencyGraph before({a, b, c, d});
        QCOMPARE(before.descendants({key("A")}), QStringList({key("B"), key("C")}));
        ContentDependencyGraph current({b, c, d});
        QVERIFY(current.unused({key("B"), key("C")}).isEmpty());
        QCOMPARE(current.relations(0).value("requiredBy").toStringList(), QStringList{"D"});
        ContentDependencyGraph orphan({b, c});
        QCOMPARE(orphan.unused({key("B"), key("C")}).size(), 2);
        QVERIFY(orphan.individuallyUnused(key("B")));
        QVERIFY(!orphan.individuallyUnused(key("C")));
        b["locked"] = true;
        QVERIFY(ContentDependencyGraph({b, c}).unused({key("B"), key("C")}).isEmpty());
    }
    void cyclesDisabledParentsAndOptionalRelations() {
        auto a = node("A", {"B"}), b = node("B", {"C"}), c = node("C", {"B"});
        QCOMPARE(ContentDependencyGraph({a, b, c}).descendants({key("A")}).size(), 2);
        QCOMPARE(ContentDependencyGraph({b, c}).unused({key("B"), key("C")}).size(), 2);
        a["enabled"] = false;
        QVERIFY(ContentDependencyGraph({a, b, c}).unused({key("B"), key("C")}).isEmpty());
        a["runtimeDependencies"] = QVariantList{QVariantMap{{"id", "B"}, {"required", false}}};
        const auto relation = ContentDependencyGraph({a, b}).relations(1);
        QVERIFY(relation.value("requiredBy").toStringList().isEmpty());
        QCOMPARE(relation.value("optionalFor").toStringList(), QStringList{"A"});
    }
    void providerRelationsUseExactVerifiedProjectAndVersionIdentities_data() {
        QTest::addColumn<QString>("provider");
        QTest::addColumn<bool>("byVersion");
        QTest::newRow("modrinth-project") << QString("modrinth") << false;
        QTest::newRow("modrinth-version") << QString("modrinth") << true;
        QTest::newRow("curseforge") << QString("curseforge") << false;
    }
    void providerRelationsUseExactVerifiedProjectAndVersionIdentities() {
        QFETCH(QString, provider);
        QFETCH(bool, byVersion);
        auto a = node("A"), b = node("B");
        Release root, target;
        root.provider = target.provider = provider;
        root.projectId = "1";
        target.projectId = "2";
        root.id = "root";
        target.id = "target";
        root.sha1 = "A";
        target.sha1 = "B";
        root.dependencyRefs = provider == "curseforge"
                                  ? QJsonArray{QJsonObject{{"modId", 2}, {"relationType", 3}}}
                                  : QJsonArray{QJsonObject{{"project_id", byVersion ? "" : "2"},
                                                           {"version_id", byVersion ? "target" : ""},
                                                           {"dependency_type", "required"}}};
        a["installedRelease"] = root.toVariant();
        b["installedRelease"] = target.toVariant();
        b["sourceIds"] = QVariantMap{{provider, "2"}};
        QCOMPARE(ContentDependencyGraph({a, b}).descendants({key("A")}), QStringList{key("B")});
        a["sha1"] = "changed";
        QVERIFY(ContentDependencyGraph({a, b}).descendants({key("A")}).isEmpty());
    }
    void runtimeMetadataReadsDeclaredEdges_data() {
        QTest::addColumn<QString>("filename");
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("fabric")
            << QString("fabric.mod.json")
            << QByteArray(
                   R"({"id":"root","version":"1","depends":{"child":"*"},"suggests":{"extra":"*"},"provides":["alias"]})");
        QTest::newRow("quilt")
            << QString("quilt.mod.json")
            << QByteArray(
                   R"({"quilt_loader":{"id":"root","version":"1","depends":[{"id":"child"},{"id":"extra","optional":true}],"provides":[{"id":"alias"}]}})");
        QTest::newRow("forge")
            << QString("META-INF/mods.toml")
            << QByteArray("[[mods]]\nmodId=\"root\"\n[[dependencies.root]]\nmodId=\"child\"\nmandatory="
                          "true\n[[dependencies.root]]\nmodId=\"extra\"\nmandatory=false\n");
        QTest::newRow("neoforge")
            << QString("META-INF/neoforge.mods.toml")
            << QByteArray("[[mods]]\nmodId=\"root\"\n[[dependencies.root]]\nmodId=\"child\"\ntype="
                          "\"required\"\n[[dependencies.root]]\nmodId=\"extra\"\ntype=\"optional\"\n[["
                          "dependencies.root]]\nmodId=\"bad\"\ntype=\"incompatible\"\n");
    }
    void runtimeMetadataReadsDeclaredEdges() {
        QFETCH(QString, filename);
        QFETCH(QByteArray, bytes);
        struct Archive final : IArchiveReader {
            QMap<QString, QByteArray> entries;
            QMap<QString, QByteArray> readEntries(const QString &, const QStringList &,
                                                  qsizetype) const override {
                return entries;
            }
        };
        auto archive = std::make_shared<Archive>();
        archive->entries[filename] = bytes;
        const auto metadata = ModMetadataReader(archive).read("fixture.jar");
        QCOMPARE(metadata.value("modId").toString(), QString("root"));
        const auto refs = metadata.value("runtimeDependencies").toList();
        QCOMPARE(refs.size(), 2);
        QCOMPARE(refs[0].toMap().value("id").toString(), QString("child"));
        QVERIFY(refs[0].toMap().value("required").toBool());
        QCOMPARE(refs[1].toMap().value("id").toString(), QString("extra"));
        QVERIFY(!refs[1].toMap().value("required").toBool());
    }
    void largeGraphPreparationAndTraversalAreBounded() {
        QVariantList rows;
        QStringList candidates;
        for (int i = 0; i < 5000; ++i) {
            const auto id = QString::number(i);
            rows << node(id, i < 4999 ? QStringList{QString::number(i + 1)} : QStringList{});
            if (i)
                candidates << key(id);
        }
        QElapsedTimer timer;
        timer.start();
        ContentDependencyGraph graph(rows);
        QCOMPARE(graph.descendants({key("0")}).size(), 4999);
        QCOMPARE(graph.unused(candidates).size(), 0);
        qInfo() << "5000-node graph prepare/traverse ms" << timer.elapsed();
        QVERIFY(test_performance::releaseBudgetMet(timer.elapsed(), 250));
    }
    void ambiguousRuntimeIdentitiesAreRetainedInsteadOfGuessed() {
        auto root = node("Root", {"dup"}), first = node("dup"), second = node("dup");
        second["relativePath"] = "mods/second.jar";
        ContentDependencyGraph graph({root, first, second});
        QVERIFY(graph.descendants({key("Root")}).isEmpty());
        QVERIFY(graph.unused({key("dup"), contentRowKey(second)}).isEmpty());
    }
};
QTEST_GUILESS_MAIN(ContentDependencyGraphTests)
#include "ContentDependencyGraphTests.moc"
