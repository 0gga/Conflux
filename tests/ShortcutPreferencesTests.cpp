#include "features/preferences/ShortcutPreferences.h"
#include "platform/BrowseSettingsStore.h"
#include <QFileInfo>
#include <QKeySequence>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
using namespace studio;
namespace {
class Store final : public IBrowseSettingsStore {
  public:
    QVariantMap values;
    int writes = 0;
    bool failRead = false, failWrite = false;
    std::function<void()> onSave;
    QVariant value(const QString &key, const QVariant &fallback) const override {
        if (failRead)
            throw std::runtime_error("read fixture");
        return values.value(key, fallback);
    }
    void save(const QString &key, const QVariant &value) override {
        if (failWrite)
            throw std::runtime_error("write fixture");
        values[key] = value;
        ++writes;
        if (onSave)
            onSave();
    }
};
} // namespace
class ShortcutPreferencesTests final : public QObject {
    Q_OBJECT
  private slots:
    void storedBindingSurvivesAnIndependentProcessRestart() {
        QTemporaryDir temp;
        const auto file = temp.path() + "/settings.ini";
        {
            ShortcutPreferences prefs(std::make_shared<BrowseSettingsStore>(file));
            QVERIFY(prefs.setBinding("palette", "Ctrl+J"));
        }
        QVERIFY(QFileInfo(file).size() > 0);
        QProcess child;
#ifdef Q_OS_WIN
        child.setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments *arguments) { arguments->flags |= CREATE_NO_WINDOW; });
#endif
        child.start(QCoreApplication::applicationFilePath(), {"--verify-shortcut-file", file});
        QVERIFY2(child.waitForStarted(5000), qPrintable(child.errorString()));
        QVERIFY2(child.waitForFinished(10000), qPrintable(child.errorString()));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
    }
    void defaultsPersistOverridesAndKeepUnknownSettings() {
        QTemporaryDir temp;
        const auto file = temp.path() + "/settings.ini";
        auto store = std::make_shared<BrowseSettingsStore>(file);
        store->save("unrelated", "preserved");
        store->save("keyboard/shortcuts", QVariantMap{{"futureCommand", "Alt+F9"}});
        ShortcutPreferences prefs(store);
        QCOMPARE(prefs.bindings().value("palette").toString(), QString("Ctrl+Shift+P"));
        QCOMPARE(prefs.bindings().value("discover").toString(), QString("Ctrl+K"));
        QCOMPARE(prefs.rows().size(), 5);
        QSignalSpy resets(prefs.model(), &QAbstractItemModel::modelReset);
        QVERIFY(prefs.setBinding("palette", "Ctrl+J"));
        QVERIFY(prefs.setBinding("back", {}));
        ShortcutPreferences reopened(std::make_shared<BrowseSettingsStore>(file));
        QCOMPARE(reopened.bindings().value("palette").toString(), QString("Ctrl+J"));
        QVERIFY(reopened.bindings().value("back").toString().isEmpty());
        QVERIFY(reopened.reset("palette"));
        QVERIFY(reopened.resetAll());
        QCOMPARE(store->value("unrelated").toString(), QString("preserved"));
        QCOMPARE(store->value("keyboard/shortcuts").toMap().value("futureCommand").toString(),
                 QString("Alt+F9"));
        QCOMPARE(resets.size(), 0);
    }
    void invalidSequencesAndConflictsNeverChangeBindings_data() {
        QTest::addColumn<QString>("sequence");
        for (const auto &value : QStringList{"A",
                                             "Shift+A",
                                             "Ctrl+C",
                                             "Ctrl+V",
                                             "Ctrl+X",
                                             "Ctrl+A",
                                             "Ctrl+Z",
                                             "Ctrl+Y",
                                             "Ctrl+Shift+Z",
                                             "Ctrl+S",
                                             "Ctrl+Delete",
                                             "Ctrl+Alt+Delete",
                                             "Alt+F4",
                                             "Alt+Tab",
                                             "Alt+Shift+Tab",
                                             "Ctrl+Alt+Tab",
                                             "Ctrl+Alt+Shift+Tab",
                                             "Ctrl+K",
                                             "Ctrl+K, Ctrl+P",
                                             "Escape",
                                             QString(200, 'x')})
            QTest::newRow(qPrintable(value.left(40))) << value;
#ifdef Q_OS_WIN
        QTest::newRow("Windows-key") << QString("Meta+J");
#endif
    }
    void invalidSequencesAndConflictsNeverChangeBindings() {
        QFETCH(QString, sequence);
        auto store = std::make_shared<Store>();
        ShortcutPreferences prefs(store);
        const auto original = prefs.bindings();
        QVERIFY(!prefs.setBinding("palette", sequence));
        QCOMPARE(prefs.bindings(), original);
        QVERIFY(!prefs.error().isEmpty());
        QCOMPARE(store->writes, 0);
        QVERIFY(!prefs.setBinding("unknown", "Ctrl+J"));
        QVERIFY(!prefs.reset("unknown"));
    }
    void swappedPersistedBindingsLoadTogetherAndBadMapsFallBackQuietly() {
        auto store = std::make_shared<Store>();
        store->values["keyboard/shortcuts"] = QVariantMap{{"back", "Alt+Right"}, {"forward", "Alt+Left"}};
        ShortcutPreferences swapped(store);
        QCOMPARE(swapped.bindings().value("back").toString(), QString("Alt+Right"));
        QCOMPARE(swapped.bindings().value("forward").toString(), QString("Alt+Left"));
        store->values["keyboard/shortcuts"] = QVariantMap{{"palette", "Ctrl+K"}};
        ShortcutPreferences duplicate(store);
        QCOMPARE(duplicate.bindings().value("palette").toString(), QString("Ctrl+Shift+P"));
        QVERIFY(duplicate.error().isEmpty());
        store->values["keyboard/shortcuts"] = QVariantMap{{"palette", 42}};
        ShortcutPreferences malformed(store);
        QCOMPARE(malformed.bindings().value("palette").toString(), QString("Ctrl+Shift+P"));
        store->failRead = true;
        ShortcutPreferences unreadable(store);
        QCOMPARE(unreadable.bindings().value("palette").toString(), QString("Ctrl+Shift+P"));
        QCOMPARE(store->writes, 0);
    }
    void recorderCommitsOnPrimaryReleaseAndEscapeCancels() {
        auto store = std::make_shared<Store>();
        ShortcutPreferences prefs(store);
        prefs.beginCapture("unknown");
        QVERIFY(!prefs.capturing());
        prefs.beginCapture("palette");
        prefs.capturePress(Qt::Key_Control, Qt::ControlModifier);
        QVERIFY(prefs.candidate().isEmpty());
        prefs.capturePress(Qt::Key_J, Qt::ControlModifier);
        QVERIFY(!prefs.candidate().isEmpty());
        QCOMPARE(store->writes, 0);
        QVERIFY(!prefs.captureRelease(Qt::Key_Control));
        QVERIFY(prefs.captureRelease(Qt::Key_J));
        QVERIFY(!prefs.capturing());
        QCOMPARE(prefs.bindings().value("palette").toString(), QString("Ctrl+J"));
        QCOMPARE(store->writes, 1);
        QVERIFY(!prefs.captureRelease(Qt::Key_J));
        prefs.beginCapture("palette");
        prefs.capturePress(Qt::Key_L, Qt::ControlModifier);
        prefs.capturePress(Qt::Key_Escape, 0);
        QVERIFY(!prefs.capturing());
        QVERIFY(!prefs.captureRelease(Qt::Key_L));
        QCOMPARE(store->writes, 1);
        prefs.beginCapture("palette");
        prefs.capturePress(Qt::Key_A, 0);
        QVERIFY(!prefs.error().isEmpty());
        QVERIFY(!prefs.captureRelease(Qt::Key_A));
        QVERIFY(prefs.clearCaptured());
        QVERIFY(prefs.bindings().value("palette").toString().isEmpty());
        QVERIFY(!prefs.clearCaptured());
    }
    void conflictsAndWriteFaultStayInTheRecorderAndRemainRetryable() {
        auto store = std::make_shared<Store>();
        ShortcutPreferences prefs(store);
        prefs.beginCapture("palette");
        prefs.capturePress(Qt::Key_K, Qt::ControlModifier);
        QVERIFY(!prefs.captureRelease(Qt::Key_K));
        QVERIFY(prefs.capturing());
        QVERIFY(prefs.error().contains("Explore content"));
        store->failWrite = true;
        prefs.capturePress(Qt::Key_J, Qt::ControlModifier);
        QVERIFY(!prefs.captureRelease(Qt::Key_J));
        QVERIFY(prefs.capturing());
        QCOMPARE(prefs.bindings().value("palette").toString(), QString("Ctrl+Shift+P"));
        store->failWrite = false;
        prefs.capturePress(Qt::Key_J, Qt::ControlModifier);
        QVERIFY(prefs.captureRelease(Qt::Key_J));
        QVERIFY(!prefs.capturing());
    }
    void aNewCaptureStartedBySaveObserversSurvivesTheOldRelease() {
        auto store = std::make_shared<Store>();
        ShortcutPreferences prefs(store);
        prefs.beginCapture("palette");
        prefs.capturePress(Qt::Key_J, Qt::ControlModifier);
        connect(&prefs, &ShortcutPreferences::changed, this, [&] { prefs.beginCapture("forward"); });
        QVERIFY(prefs.captureRelease(Qt::Key_J));
        QVERIFY(prefs.capturing());
        QCOMPARE(prefs.recordingTitle(), QString("Forward"));
        QVERIFY(prefs.candidate().isEmpty());
        QCOMPARE(prefs.bindings().value("palette").toString(), QString("Ctrl+J"));
    }
    void storeCallbacksCanDisposeTheOwner() {
        auto store = std::make_shared<Store>();
        auto prefs = std::make_unique<ShortcutPreferences>(store);
        store->onSave = [&] { prefs.reset(); };
        auto *original = prefs.get();
        QVERIFY(!original->setBinding("palette", "Ctrl+J"));
        QVERIFY(!prefs);
    }
};
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() == 3 && arguments[1] == "--verify-shortcut-file") {
        ShortcutPreferences prefs(std::make_shared<BrowseSettingsStore>(arguments[2]));
        return prefs.bindings().value("palette").toString() == "Ctrl+J" ? 0 : 2;
    }
    ShortcutPreferencesTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ShortcutPreferencesTests.moc"
