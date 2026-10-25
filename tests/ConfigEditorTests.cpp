#include "features/config/ConfigEditor.h"
#include <QMutex>
#include <QPointer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThreadPool>
#include <QtTest>
#include <atomic>
#include <stdexcept>
using namespace studio;
namespace {
enum class Failure { None, Result, StandardException, UnknownException };
struct Pause {
    QSemaphore entered, proceed;
};
class Repository final : public IConfigRepository {
  public:
    struct Write {
        ConfigDocument document;
        QVariantMap changes;
    };
    mutable QMutex mutex;
    mutable QList<Write> writes;
    mutable QHash<QString, bool> values;
    mutable std::atomic<int> loads{0};
    mutable QHash<QString, Failure> loadFailures;
    Failure saveFailure = Failure::None;
    QHash<QString, std::shared_ptr<Pause>> pauses;
    bool failReloadAfterSave = false;
    bool withBackup = true;
    void failLoad(const QString &path, Failure failure) {
        QMutexLocker lock(&mutex);
        loadFailures[path] = failure;
    }
    void failSave(Failure failure) {
        QMutexLocker lock(&mutex);
        saveFailure = failure;
    }
    QList<Write> attempts() const {
        QMutexLocker lock(&mutex);
        return writes;
    }
    static void raise(Failure failure) {
        if (failure == Failure::StandardException)
            throw std::runtime_error("repository fixture");
        if (failure == Failure::UnknownException)
            throw 7;
    }
    ConfigDocument load(const ConfigTarget &target) const override {
        ++loads;
        Failure failure;
        bool value;
        std::shared_ptr<Pause> pause;
        {
            QMutexLocker lock(&mutex);
            failure = loadFailures.value(target.relativePath);
            value = values.value(target.relativePath, true);
            pause = pauses.value(target.relativePath);
        }
        if (pause) {
            pause->entered.release();
            if (!pause->proceed.tryAcquire(1, 5000))
                return {target, {}, {{}, "Fixture read was not released."}};
        }
        raise(failure);
        ConfigDocument document;
        document.target = target;
        if (failure == Failure::Result)
            document.decoded.error = "Could not read fixture config.";
        else {
            document.original = value ? "enabled=true" : "enabled=false";
            document.decoded.fields = {QVariantMap{{"id", "/enabled"}, {"value", value}, {"type", "bool"}}};
        }
        return document;
    }
    ConfigSaveResult save(const ConfigDocument &document, const QVariantMap &changes) const override {
        Failure failure;
        {
            QMutexLocker lock(&mutex);
            writes << Write{document, changes};
            failure = saveFailure;
        }
        raise(failure);
        if (failure == Failure::Result)
            return {false, "Fixture config changed outside Conflux."};
        {
            QMutexLocker lock(&mutex);
            if (changes.contains("/enabled"))
                values[document.target.relativePath] = changes.value("/enabled").toBool();
            if (failReloadAfterSave)
                loadFailures[document.target.relativePath] = Failure::Result;
        }
        return {true, {}, withBackup ? "fixture-backup" : QString{}};
    }
};
ConfigTarget target(const QString &name) {
    return {"fixture-instance", "config/" + name + ".toml", name, name, {}, "Fixture owner"};
}
} // namespace
class ConfigEditorTests final : public QObject {
    Q_OBJECT
  private slots:
    void invalidAndUnchangedEditsDoNotCreateSaveCommands() {
        auto repository = std::make_shared<Repository>();
        ConfigEditor editor(repository);
        editor.close();
        editor.save();
        editor.setField("/missing", true);
        QVERIFY(repository->attempts().isEmpty());
        QVERIFY(!editor.fieldValue("/missing").isValid());
        editor.open(target("A"));
        QTRY_VERIFY(!editor.busy());
        QCOMPARE(editor.title(), QString("A"));
        QCOMPARE(editor.association(), QString("Fixture owner"));
        QVERIFY(editor.icon().isEmpty());
        QCOMPARE(editor.unsupported(), 0);
        editor.open(target("A"));
        editor.setField("/missing", false);
        QVERIFY(!editor.dirty());
        editor.setField("/enabled", false);
        QVERIFY(editor.dirty());
        editor.reload();
        QCOMPARE(repository->loads.load(), 1);
        QCOMPARE(editor.fieldValue("/enabled"), QVariant(false));
        editor.setField("/enabled", true);
        QVERIFY(!editor.dirty());
        editor.save();
        QVERIFY(repository->attempts().isEmpty());
        editor.setField("/enabled", false);
        editor.discard();
        QVERIFY(!editor.dirty());
        QCOMPARE(editor.fieldValue("/enabled"), QVariant(true));
        editor.close();
        editor.close();
        QVERIFY(editor.path().isEmpty());
    }
    void busyReadsIgnoreDuplicateActionsAndEdits() {
        auto repository = std::make_shared<Repository>();
        auto pause = std::make_shared<Pause>();
        repository->pauses[target("A").relativePath] = pause;
        const auto release = qScopeGuard([pause] { pause->proceed.release(); });
        ConfigEditor editor(repository);
        editor.open(target("A"));
        QTRY_COMPARE(pause->entered.available(), 1);
        editor.open(target("A"));
        editor.reload();
        editor.save();
        editor.setField("/enabled", false);
        editor.discard();
        QVERIFY(editor.busy());
        QVERIFY(!editor.dirty());
        QVERIFY(repository->attempts().isEmpty());
        QCOMPARE(repository->loads.load(), 1);
        pause->proceed.release();
        QTRY_VERIFY(!editor.busy());
        QCOMPARE(editor.fieldValue("/enabled"), QVariant(true));
    }
    void acceptedSaveIsNotDuplicatedOrChangedByBusyActions_data() {
        QTest::addColumn<bool>("backup");
        QTest::newRow("with-backup") << true;
        QTest::newRow("without-backup") << false;
    }
    void acceptedSaveIsNotDuplicatedOrChangedByBusyActions() {
        QFETCH(bool, backup);
        auto repository = std::make_shared<Repository>();
        repository->withBackup = backup;
        ConfigEditor editor(repository);
        editor.open(target("A"));
        QTRY_VERIFY(!editor.busy());
        editor.setField("/enabled", false);
        editor.save();
        editor.save();
        editor.reload();
        editor.setField("/enabled", true);
        editor.discard();
        QTRY_VERIFY(!editor.busy());
        QCOMPARE(repository->attempts().size(), 1);
        QCOMPARE(editor.fieldValue("/enabled"), QVariant(false));
        QCOMPARE(editor.status().contains("backup"), backup);
        QVERIFY(editor.status().startsWith("Saved"));
    }
    void reentrantFieldObserversRetainTheirNewContext_data() {
        QTest::addColumn<bool>("close");
        QTest::newRow("close") << true;
        QTest::newRow("discard") << false;
    }
    void reentrantFieldObserversRetainTheirNewContext() {
        QFETCH(bool, close);
        auto repository = std::make_shared<Repository>();
        ConfigEditor editor(repository);
        editor.open(target("A"));
        QTRY_VERIFY(!editor.busy());
        editor.setField("/enabled", false);
        bool redirected = false;
        connect(&editor, &ConfigEditor::fieldsChanged, this, [&] {
            if (!redirected) {
                redirected = true;
                editor.open(target("B"));
            }
        });
        if (close)
            editor.close();
        else
            editor.discard();
        QTRY_VERIFY(redirected && !editor.busy());
        QCOMPARE(editor.path(), target("B").relativePath);
        QVERIFY(!editor.dirty());
    }
    void acceptedSaveKeepsItsDocumentWhenPublicationOpensAnotherFile() {
        auto repository = std::make_shared<Repository>();
        ConfigEditor editor(repository);
        editor.open(target("A"));
        QTRY_VERIFY(!editor.busy());
        editor.setField("/enabled", false);
        bool redirected = false;
        connect(&editor, &ConfigEditor::changed, this, [&] {
            if (!redirected && editor.busy() && editor.dirty()) {
                redirected = true;
                editor.open(target("B"));
            }
        });
        editor.save();
        QTRY_COMPARE(repository->attempts().size(), 1);
        QCOMPARE(repository->attempts().first().document.target.relativePath, target("A").relativePath);
        QCOMPARE(repository->attempts().first().changes, (QVariantMap{{"/enabled", false}}));
        QTRY_VERIFY(!editor.busy());
        QCOMPARE(editor.path(), target("B").relativePath);
        QVERIFY(editor.status().isEmpty());
    }
    void successfulSaveCannotPublishItsStatusIntoAnotherFile() {
        auto repository = std::make_shared<Repository>();
        ConfigEditor editor(repository);
        editor.open(target("A"));
        QTRY_VERIFY(!editor.busy());
        editor.setField("/enabled", false);
        QSignalSpy saved(&editor, &ConfigEditor::saved);
        bool redirected = false;
        connect(&editor, &ConfigEditor::fieldsChanged, this, [&] {
            if (!redirected && editor.busy() && !editor.dirty() &&
                editor.path() == target("A").relativePath) {
                redirected = true;
                editor.open(target("B"));
            }
        });
        editor.save();
        QTRY_VERIFY(redirected && !editor.busy());
        QCOMPARE(editor.path(), target("B").relativePath);
        QVERIFY(editor.status().isEmpty());
        QCOMPARE(saved.size(), 0);
    }
    void acceptedSaveSurvivesOwnerDisposalDuringPublication() {
        auto repository = std::make_shared<Repository>();
        QPointer<ConfigEditor> editor = new ConfigEditor(repository);
        editor->open(target("A"));
        QTRY_VERIFY(!editor->busy());
        editor->setField("/enabled", false);
        connect(editor, &ConfigEditor::changed, this, [&] {
            if (editor->busy() && editor->dirty())
                delete editor.data();
        });
        editor->save();
        QVERIFY(editor.isNull());
        QTRY_COMPARE(repository->attempts().size(), 1);
        QCOMPARE(repository->attempts().first().document.target.relativePath, target("A").relativePath);
    }
    void lateReadCannotReplaceAnotherFileOrClosedEditor_data() {
        QTest::addColumn<bool>("close");
        QTest::newRow("another-file") << false;
        QTest::newRow("closed") << true;
    }
    void lateReadCannotReplaceAnotherFileOrClosedEditor() {
        QFETCH(bool, close);
        auto repository = std::make_shared<Repository>();
        auto pause = std::make_shared<Pause>();
        repository->pauses[target("A").relativePath] = pause;
        const auto release = qScopeGuard([pause] { pause->proceed.release(); });
        ConfigEditor editor(repository);
        editor.open(target("A"));
        QTRY_COMPARE(pause->entered.available(), 1);
        pause->entered.acquire();
        if (close)
            editor.close();
        else {
            editor.open(target("B"));
            QTRY_VERIFY(!editor.busy());
        }
        pause->proceed.release();
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::processEvents();
        QVERIFY(!editor.busy());
        QCOMPARE(editor.path(), close ? QString{} : target("B").relativePath);
        QVERIFY(editor.error().isEmpty());
    }
    void readFailureIsScopedAndRetryable_data() {
        failures();
    }
    void readFailureIsScopedAndRetryable() {
        QFETCH(int, failure);
        auto repository = std::make_shared<Repository>();
        repository->failLoad(target("A").relativePath, Failure(failure));
        ConfigEditor editor(repository);
        editor.open(target("A"));
        QTRY_VERIFY(!editor.busy());
        QCOMPARE(editor.path(), target("A").relativePath);
        QVERIFY(!editor.error().isEmpty());
        QVERIFY(editor.fields().isEmpty());
        repository->failLoad(target("A").relativePath, Failure::None);
        editor.reload();
        QTRY_VERIFY(!editor.busy());
        QVERIFY(editor.error().isEmpty());
        QCOMPARE(editor.fieldValue("/enabled"), QVariant(true));
    }
    void saveFailurePreservesEditsAndCanBeRetried_data() {
        failures();
    }
    void saveFailurePreservesEditsAndCanBeRetried() {
        QFETCH(int, failure);
        auto repository = std::make_shared<Repository>();
        repository->failSave(Failure(failure));
        ConfigEditor editor(repository);
        editor.open(target("A"));
        QTRY_VERIFY(!editor.busy());
        editor.setField("/enabled", false);
        QSignalSpy saved(&editor, &ConfigEditor::saved);
        editor.save();
        QTRY_VERIFY(!editor.busy());
        QVERIFY(editor.dirty());
        QCOMPARE(editor.fieldValue("/enabled"), QVariant(false));
        QVERIFY(!editor.error().isEmpty());
        QCOMPARE(saved.size(), 0);
        repository->failSave(Failure::None);
        editor.save();
        QTRY_COMPARE(saved.size(), 1);
        QTRY_VERIFY(!editor.busy());
        QVERIFY(!editor.dirty());
        QVERIFY(editor.error().isEmpty());
        QCOMPARE(editor.fieldValue("/enabled"), QVariant(false));
        QCOMPARE(repository->attempts().size(), 2);
    }
    void successfulWriteStillNotifiesWhenReloadFails() {
        auto repository = std::make_shared<Repository>();
        ConfigEditor editor(repository);
        editor.open(target("A"));
        QTRY_VERIFY(!editor.busy());
        editor.setField("/enabled", false);
        repository->failReloadAfterSave = true;
        QSignalSpy saved(&editor, &ConfigEditor::saved);
        editor.save();
        QTRY_COMPARE(saved.size(), 1);
        QTRY_VERIFY(!editor.busy());
        QVERIFY(!editor.dirty());
        QVERIFY(!editor.error().isEmpty());
        QVERIFY(editor.status().contains("Saved"));
        QCOMPARE(repository->attempts().size(), 1);
    }
    void savedObserverMayCloseWhileTheReloadIsPending() {
        auto repository = std::make_shared<Repository>();
        ConfigEditor editor(repository);
        editor.open(target("A"));
        QTRY_VERIFY(!editor.busy());
        editor.setField("/enabled", false);
        connect(&editor, &ConfigEditor::saved, &editor, &ConfigEditor::close);
        editor.save();
        QTRY_VERIFY(editor.path().isEmpty());
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::processEvents();
        QVERIFY(editor.path().isEmpty());
        QVERIFY(!editor.busy());
        QVERIFY(!editor.dirty());
        QVERIFY(editor.status().isEmpty());
    }
    void destructionDuringFieldsPublicationIsSafe_data() {
        QTest::addColumn<QString>("operation");
        for (const auto &name : {"open", "read-complete", "close", "discard"})
            QTest::newRow(name) << QString(name);
    }
    void destructionDuringFieldsPublicationIsSafe() {
        QFETCH(QString, operation);
        auto repository = std::make_shared<Repository>();
        QPointer<ConfigEditor> editor = new ConfigEditor(repository);
        if (operation != "open") {
            editor->open(target("A"));
            QTRY_VERIFY(!editor->busy());
        }
        if (operation == "read-complete") {
            connect(editor, &ConfigEditor::fieldsChanged, this, [&] {
                if (editor && !editor->busy())
                    delete editor.data();
            });
            editor->reload();
            QTRY_VERIFY(editor.isNull());
        } else {
            connect(editor, &ConfigEditor::fieldsChanged, this, [&] { delete editor.data(); });
            if (operation == "open")
                editor->open(target("A"));
            else if (operation == "close")
                editor->close();
            else
                editor->discard();
            QVERIFY(editor.isNull());
        }
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
        QCoreApplication::processEvents();
    }

  private:
    static void failures() {
        QTest::addColumn<int>("failure");
        QTest::newRow("ordinary-error") << int(Failure::Result);
        QTest::newRow("standard-exception") << int(Failure::StandardException);
        QTest::newRow("unknown-exception") << int(Failure::UnknownException);
    }
};
QTEST_GUILESS_MAIN(ConfigEditorTests)
#include "ConfigEditorTests.moc"
