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
        QVERIFY(timer.elapsed() < 250);
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
