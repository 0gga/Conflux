#include "features/preferences/ContentPreferences.h"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>
using namespace studio;
class ContentPreferenceTests final : public QObject {
    Q_OBJECT
    static QString legacyKey(const QString &pack, const QString &identity, const QString &section) {
        const auto digest =
            QCryptographicHash::hash((pack + '\n' + identity).toUtf8(), QCryptographicHash::Sha256).toHex();
        return "content/" + section + '/' + QString::fromLatin1(digest);
    }
  private slots:
    void legacyStoredKeysAndExplicitFalseRemainCompatible_data() {
        QTest::addColumn<QString>("pack");
        QTest::addColumn<QString>("identity");
        QTest::newRow("provider") << QString("C:/Instances/Example") << QString("modrinth:exact-id");
        QTest::newRow("unicode-path")
            << QString::fromUtf8("C:/Instances/Caf\xc3\xa9") << QString("curseforge:42");
        QTest::newRow("path-fallback")
            << QString("C:/Instances/Example/") << QString("path:mods/exact-name.jar");
    }
    void legacyStoredKeysAndExplicitFalseRemainCompatible() {
        QFETCH(QString, pack);
        QFETCH(QString, identity);
        QTemporaryDir temp;
        const auto path = temp.path() + "/preferences.ini";
        QSettings saved(path, QSettings::IniFormat);
        saved.setValue(legacyKey(pack, identity, "frozen"), false);
        saved.setValue(legacyKey(pack, identity, "locked"), true);
        saved.setValue(legacyKey(pack, identity, "updated"), qint64(123456));
        saved.sync();
        SettingsContentPreferences preferences(path);
        QVERIFY(preferences.hasFrozenPreference(pack, identity));
        QVERIFY(!preferences.frozen(pack, identity));
        QVERIFY(preferences.locked(pack, identity));
        QCOMPARE(preferences.updatedAt(pack, identity), qint64(123456));
        preferences.setFrozen(pack, identity, true);
        preferences.setLocked(pack, identity, false);
        preferences.setUpdatedAt(pack, identity, 654321);
        saved.sync();
        QVERIFY(saved.value(legacyKey(pack, identity, "frozen")).toBool());
        QVERIFY(!saved.value(legacyKey(pack, identity, "locked")).toBool());
        QCOMPARE(saved.value(legacyKey(pack, identity, "updated")).toLongLong(), qint64(654321));
        QCOMPARE(saved.allKeys().size(), 3);
    }
    void writesStayVisibleAcrossReadersWithoutCachingPreferenceValues() {
        QTemporaryDir temp;
        const auto path = temp.path() + "/preferences.ini";
        SettingsContentPreferences first(path), second(path);
        QVERIFY(!first.locked("pack", "modrinth:one"));
        QVERIFY(!first.hasFrozenPreference("pack", "modrinth:one"));
        second.setLocked("pack", "modrinth:one", true);
        second.setFrozen("pack", "modrinth:one", false);
        second.setUpdatedAt("pack", "modrinth:one", 123);
        QVERIFY(first.locked("pack", "modrinth:one"));
        QVERIFY(first.hasFrozenPreference("pack", "modrinth:one"));
        QVERIFY(!first.frozen("pack", "modrinth:one"));
        QCOMPARE(first.updatedAt("pack", "modrinth:one"), qint64(123));
        first.setLocked("pack", "modrinth:one", false);
        first.setFrozen("pack", "modrinth:one", true);
        first.setUpdatedAt("pack", "modrinth:one", 456);
        QVERIFY(!second.locked("pack", "modrinth:one"));
        QVERIFY(second.frozen("pack", "modrinth:one"));
        QCOMPARE(second.updatedAt("pack", "modrinth:one"), qint64(456));
    }
    void exactInstanceAndProviderIdentitiesRemainIndependent() {
        QTemporaryDir temp;
        SettingsContentPreferences preferences(temp.path() + "/preferences.ini");
        preferences.setLocked("pack-a", "modrinth:42", true);
        QVERIFY(preferences.locked("pack-a", "modrinth:42"));
        QVERIFY(!preferences.locked("pack-a", "curseforge:42"));
        QVERIFY(!preferences.locked("pack-b", "modrinth:42"));
        QVERIFY(!preferences.locked("pack-a", "path:mods/42.jar"));
        preferences.setFrozen("pack-a", "curseforge:42", true);
        QVERIFY(!preferences.frozen("pack-a", "modrinth:42"));
    }
    void manyIdentitiesPreserveValuesAcrossReuseAndEviction() {
        QTemporaryDir temp;
        SettingsContentPreferences preferences(temp.path() + "/preferences.ini");
        for (int i = 0; i < 6000; ++i) {
            const auto identity = "modrinth:" + QString::number(i);
            preferences.setLocked("large-pack", identity, i % 3 == 0);
            preferences.setFrozen("large-pack", identity, i % 2 == 0);
            preferences.setUpdatedAt("large-pack", identity, i * 1000);
        }
        for (int i = 5999; i >= 0; --i) {
            const auto identity = "modrinth:" + QString::number(i);
            QCOMPARE(preferences.locked("large-pack", identity), i % 3 == 0);
            QCOMPARE(preferences.frozen("large-pack", identity), i % 2 == 0);
            QVERIFY(preferences.hasFrozenPreference("large-pack", identity));
            QCOMPARE(preferences.updatedAt("large-pack", identity), qint64(i * 1000));
        }
    }
    void oversizedIdentityKeysBypassReuseWithoutChangingValues() {
        QTemporaryDir temp;
        SettingsContentPreferences preferences(temp.path() + "/preferences.ini");
        const QString oversizedIdentity = "modrinth:" + QString(600000, 'p');
        preferences.setLocked("pack", oversizedIdentity, true);
        preferences.setFrozen("pack", oversizedIdentity, false);
        preferences.setUpdatedAt("pack", oversizedIdentity, 321);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(preferences.locked("pack", oversizedIdentity));
            QVERIFY(preferences.hasFrozenPreference("pack", oversizedIdentity));
            QVERIFY(!preferences.frozen("pack", oversizedIdentity));
            QCOMPARE(preferences.updatedAt("pack", oversizedIdentity), qint64(321));
        }
        QVERIFY(!preferences.locked("pack", "modrinth:other"));
    }
    void repeatedIdentityReadsHaveBoundedCpuCost() {
        QTemporaryDir temp;
        SettingsContentPreferences preferences(temp.path() + "/preferences.ini");
        const QString pack = "C:/Instances/" + QString(128, 'p'), identity = "modrinth:verified-project-id";
        preferences.setFrozen(pack, identity, true);
        preferences.setLocked(pack, identity, true);
        preferences.setUpdatedAt(pack, identity, 123);
        QElapsedTimer elapsed;
        elapsed.start();
        int frozen = 0, locked = 0, explicitPreferences = 0;
        qint64 updated = 0;
        for (int i = 0; i < 200000; ++i) {
            frozen += preferences.frozen(pack, identity);
            locked += preferences.locked(pack, identity);
            explicitPreferences += preferences.hasFrozenPreference(pack, identity);
            updated += preferences.updatedAt(pack, identity);
        }
        const auto milliseconds = elapsed.elapsed();
        QCOMPARE(frozen, 200000);
        QCOMPARE(locked, 200000);
        QCOMPARE(explicitPreferences, 200000);
        QCOMPARE(updated, qint64(24600000));
        qInfo() << "800000 preference reads for one exact identity ms" << milliseconds;
        QVERIFY2(milliseconds < 800, "Preference reads repeat excessive identity-key work.");
    }
};
QTEST_GUILESS_MAIN(ContentPreferenceTests)
#include "ContentPreferenceTests.moc"
