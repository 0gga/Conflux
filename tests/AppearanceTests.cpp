#include "Appearance.h"
#include "Theme.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>
#include <QtTest>
#include <limits>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)
using namespace studio;
class AppearanceTests final : public QObject {
    Q_OBJECT
  private slots:
    void customColorsRoundTripWithoutChangingPresets() {
        QTemporaryDir dir;
        Theme theme(nullptr, dir.path());
        QCOMPARE(theme.colors().value("accent").toString(), QString("#FF005E"));
        QFile preset(theme.folder() + "/oled.json");
        QVERIFY(preset.open(QIODevice::ReadOnly));
        const auto before = preset.readAll();
        preset.close();
        QVERIFY(theme.setColor("accent", "#123456"));
        QVERIFY(theme.setColor("background", "#010203"));
        QVERIFY(theme.edited());
        const auto good = theme.colors();
        QVERIFY(!theme.setColor("unknown-role", "#000000"));
        QVERIFY(!theme.setColor("input", "invalid"));
        QCOMPARE(theme.colors(), good);
        QVERIFY(theme.saveCustom("My ../ custom theme"));
        QVERIFY(!theme.edited());
        QVERIFY(theme.file().startsWith("custom-"));
        QCOMPARE(QFileInfo(theme.file()).fileName(), theme.file());
        Theme reopened(nullptr, dir.path());
        QCOMPARE(reopened.colors(), good);
        QCOMPARE(reopened.name(), QString("My ../ custom theme"));
        QVERIFY(reopened.setColor("accent", "#FF005E"));
        reopened.discardChanges();
        QCOMPARE(reopened.colors(), good);
        QVERIFY(!reopened.saveCustom(""));
        QCOMPARE(reopened.colors(), good);
        QVERIFY(preset.open(QIODevice::ReadOnly));
        QCOMPARE(preset.readAll(), before);
        QVERIFY(!QFile::exists(dir.path() + "/custom theme.json"));
    }
    void paletteCodecRequiresEveryRoleAndRoundTripsAlpha() {
        QTemporaryDir dir;
        Theme theme(nullptr, dir.path());
        ThemePalette palette{"Example", theme.colors()};
        palette.colors["input"] = "#CC123456";
        auto decoded = ThemePalette::decode(palette.encode());
        QVERIFY(decoded);
        QCOMPARE(decoded->colors.size(), 14);
        QCOMPARE(decoded->colors.value("input").toString(), QString("#CC123456"));
        auto object = QJsonDocument::fromJson(palette.encode()).object();
        object.remove("status");
        QVERIFY(!ThemePalette::decode(QJsonDocument(object).toJson()));
    }
    void presentationControlsPersistAndResetOnlyTheirOwnScope() {
        QTemporaryDir dir;
        Appearance appearance(nullptr, dir.path());
        QVERIFY(appearance.setValue("navigationMode", 1));
        QVERIFY(appearance.setValue("tooltips", 0));
        QVERIFY(appearance.setValue("previewVisible", 0));
        QVERIFY(appearance.setValue("reduceMotion", 1));
        QVERIFY(appearance.setValue("tooltipDelay", 200));
        QCOMPARE(appearance.values().value("galleryFitPercent").toInt(), 83);
        QVERIFY(appearance.setValue("galleryFitPercent", 90));
        QVERIFY(appearance.setValue("contentRowHeight", 68));
        QVERIFY(appearance.setValue("showFollowers", 0));
        QVERIFY(appearance.setValue("showSummary", 0));
        QVERIFY(!appearance.setValue("navigationMode", 1.5));
        QVERIFY(!appearance.setValue("showAuthor", 0.5));
        QVERIFY(!appearance.setValue("tooltips", 2));
        Appearance restored(nullptr, dir.path());
        QCOMPARE(restored.values(), appearance.values());
        restored.resetValue("showSummary");
        QCOMPARE(restored.values().value("showSummary").toInt(), 1);
        QCOMPARE(restored.values().value("showFollowers").toInt(), 0);
        restored.resetGroup("Interface");
        QCOMPARE(restored.values().value("tooltips").toInt(), 1);
        QCOMPARE(restored.values().value("previewVisible").toInt(), 1);
        QCOMPARE(restored.values().value("galleryFitPercent").toInt(), 83);
        QCOMPARE(restored.values().value("navigationMode").toInt(), 0);
        QCOMPARE(restored.values().value("contentRowHeight").toInt(), 82);
        QCOMPARE(restored.values().value("showFollowers").toInt(), 0);
        restored.resetGroup("Content cards");
        QCOMPARE(restored.values().value("showFollowers").toInt(), 1);
        Appearance finalState(nullptr, dir.path());
        QCOMPARE(finalState.values(), restored.values());
    }
    void scrollingPreferencesPersistValidateAndReset() {
        QTemporaryDir dir;
        Appearance settings(nullptr, dir.path());
        QCOMPARE(settings.values().value("scrollDistance").toInt(), 140);
        QCOMPARE(settings.values().value("scrollDuration").toInt(), 180);
        QCOMPARE(settings.values().value("smoothScrolling").toInt(), 1);
        QVERIFY(settings.setValue("scrollDistance", 240));
        QVERIFY(settings.setValue("scrollDuration", 260));
        QVERIFY(settings.setValue("smoothScrolling", 0));
        QVERIFY(!settings.setValue("scrollDistance", 0));
        QVERIFY(!settings.setValue("scrollDuration", 2000));
        QVERIFY(!settings.setValue("smoothScrolling", 0.5));
        Appearance reopened(nullptr, dir.path());
        QCOMPARE(reopened.values(), settings.values());
        reopened.resetGroup("Interface");
        QCOMPARE(reopened.values().value("scrollDistance").toInt(), 140);
        QCOMPARE(reopened.values().value("scrollDuration").toInt(), 180);
        QCOMPARE(reopened.values().value("smoothScrolling").toInt(), 1);
    }
    void installedOpacityMigratesOnceAndNewChoiceWins() {
        QTemporaryDir dir;
        auto store = std::make_shared<NativeAppearanceStore>(dir.path());
        store->save({{"unavailableOpacity", 65}});
        Appearance migrated(store);
        QCOMPARE(migrated.values().value("installedOpacity").toInt(), 65);
        QVERIFY(!migrated.values().contains("unavailableOpacity"));
        QVERIFY(migrated.setValue("installedOpacity", 75));
        Appearance reopened(store);
        QCOMPARE(reopened.values().value("installedOpacity").toInt(), 75);
        store->save({{"unavailableOpacity", 65}, {"installedOpacity", 80}});
        Appearance both(store);
        QCOMPARE(both.values().value("installedOpacity").toInt(), 80);
    }
    void layoutPreferencesPersistAndRejectUnsafeSizes() {
        QTemporaryDir dir;
        Appearance layout(nullptr, dir.path());
        QCOMPARE(layout.values().value("artworkSize").toInt(), 88);
        QCOMPARE(layout.values().value("installedOpacity").toInt(), 50);
        QVERIFY(layout.setValue("installedOpacity", 65));
        QVERIFY(!layout.setValue("installedOpacity", 10));
        QVERIFY(layout.setValue("uiScale", 125));
        QVERIFY(layout.setValue("contentScale", 120));
        QVERIFY(layout.setValue("filterScale", 85));
        QVERIFY(layout.setValue("filterWidth", 280));
        const auto good = layout.values();
        QVERIFY(!layout.setValue("uiScale", 10));
        QVERIFY(!layout.setValue("unknown", 100));
        QVERIFY(!layout.setValue("filterScale", std::numeric_limits<double>::infinity()));
        QCOMPARE(layout.values(), good);
        Appearance restored(nullptr, dir.path());
        QCOMPARE(restored.values(), good);
        QVERIFY(restored.filterWidthFor(500) <= 200);
        restored.reset();
        QCOMPARE(restored.values().value("uiScale").toInt(), 100);
        QCOMPARE(restored.values().value("filterScale").toInt(), 100);
    }
};
QTEST_GUILESS_MAIN(AppearanceTests)
#include "AppearanceTests.moc"
