#include "DesktopSmokeTest.h"
#include "ApplicationController.h"
#include "DesktopSession.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <atomic>
#include <memory>
namespace studio {
static QString smokeLogPath;
static std::atomic_bool smokeRuntimeError{false};
static QQuickItem *findVisualItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name && root->isVisible())
        return root;
    for (auto *child : root->childItems())
        if (auto *found = findVisualItem(child, name))
            return found;
    return nullptr;
}

DesktopSmokeTest::DesktopSmokeTest(const QStringList &arguments, QObject *parent) : QObject(parent) {
    const auto index = arguments.indexOf("--smoke-test");
    if (index < 0 || arguments.size() <= index + 1)
        return;
    m_storage = std::make_unique<QTemporaryDir>();
    m_output = QDir(arguments.at(index + 1)).absolutePath();
    QDir().mkpath(m_output);
    smokeLogPath = QDir(m_output).filePath("smoke.log");
    QFile log(smokeLogPath);
    if (log.open(QIODevice::WriteOnly))
        log.close();
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        if (message.contains("Binding loop") || message.contains("ReferenceError") ||
            message.contains("TypeError") || message.contains("Cannot assign") ||
            message.contains("Error decoding"))
            smokeRuntimeError = true;
        QFile output(smokeLogPath);
        if (output.open(QIODevice::Append))
            output.write(message.toUtf8() + '\n');
    });
}
void DesktopSmokeTest::start(QGuiApplication &application, QQuickWindow *window,
                             ApplicationController &controller, DesktopSession &session) {

    const auto folder = m_output;
    QDir().mkpath(folder);

    auto *timer = new QTimer(this);
    timer->setInterval(300);
    struct SmokeState {
        int phase = 0;
        QString managedPath;
        QString selectedMod;
        QElapsedTimer elapsed;
    };
    auto state = std::make_shared<SmokeState>();
    state->elapsed.start();
    QObject::connect(
        timer, &QTimer::timeout, &application,
        [window, folder, timer, state, &application, &controller, &session] {
            if (state->elapsed.elapsed() > 120000 || !window) {
                application.exit(2);
                return;
            }
            auto capture = [window, folder](const QString &name) {
                const auto image = window->grabWindow();
                return !image.isNull() && image.save(QDir(folder).filePath(name + ".png"));
            };
            auto fail = [&application, timer](const QString &reason) {
                qWarning().noquote() << reason;
                timer->stop();
                application.exit(3);
            };
            switch (state->phase) {
            case 0:
                if (controller.libraryBusy() || controller.catalogueBusy())
                    return;
                if (!capture("library")) {
                    fail("Library render failed");
                    return;
                }
                session.navigation()->go(
                    {{"page", "discover"}, {"packPath", controller.activePack().value("path")}});
                break;
            case 1:
                if (!capture("discover")) {
                    fail("Discovery render failed");
                    return;
                }
                if (controller.projects().isEmpty()) {
                    fail("Live catalogue has no results");
                    return;
                }
                controller.selectProject(0);
                session.navigation()->go(
                    {{"page", "showcase"}, {"packPath", ""}, {"project", controller.projects().first()}});
                break;
            case 2:
                if (controller.detailBusy() || session.showcase()->busy())
                    return;
                if (session.showcase()->project().value("provider") != "modrinth") {
                    fail("Catalogue showcase failed without an active pack");
                    return;
                }
                if (!capture("project")) {
                    fail("Project render failed");
                    return;
                }
                controller.createPack("UI verification pack", "1.21.1", "fabric");
                break;
            case 3:
                if (controller.repositoryBusy() || controller.catalogueBusy())
                    return;
                if (controller.activePack().isEmpty() || controller.projects().isEmpty()) {
                    fail("Pack creation or filtered search failed: " + controller.notice());
                    return;
                }
                state->managedPath = controller.activePack().value("path").toString();
                controller.selectProject(0);
                break;
            case 4:
                if (controller.detailBusy())
                    return;
                if (controller.releases().isEmpty()) {
                    fail("No compatible release returned");
                    return;
                }
                controller.pinRelease(0);
                break;
            case 5: {
                if (controller.repositoryBusy())
                    return;
                if (controller.activeMods().size() != 1) {
                    fail("Pin failed: " + controller.notice());
                    return;
                }
                const auto path = controller.activePack().value("path").toString();
                for (const auto &pair : QList<QStringList>{{"user.name", "UI verification"},
                                                           {"user.email", "verification@example.invalid"},
                                                           {"core.hooksPath", path + "/no-hooks"}}) {
                    QProcess git;
                    git.setWorkingDirectory(path);
                    git.start("git", {"config", pair.at(0), pair.at(1)});
                    if (!git.waitForFinished(3000) || git.exitCode() != 0) {
                        fail("Could not configure temporary test repository");
                        return;
                    }
                }
                controller.saveChanges("Pin first mod release");
                break;
            }
            case 6: {
                if (controller.repositoryBusy() || session.workspace()->busy())
                    return;
                if (!controller.history().contains("Pin first mod release") ||
                    !controller.gitStatus().isEmpty()) {
                    fail("Git save failed: " + controller.notice());
                    return;
                }
                for (const auto &entry : QList<QPair<QString, QByteArray>>{
                         {"config/fabric-api.toml",
                          "# Conflux visual editor verification\n[general]\n# Enables the "
                          "feature\nenabled = true # keep this comment\n# Range: 1 ~ 32\ndistance = "
                          "12\n# Allowed Values: FAST, FANCY\nquality = \"FANCY\"\nnames = [\"first\", "
                          "\"second\"]\n"},
                         {"logs/latest.log",
                          "[12:00:00] [main/INFO]: Temporary UI verification log\n[12:00:01] "
                          "[main/WARN]: Example warning for display only\n"},
                         {"saves/Verification world/README.txt",
                          "Temporary world folder used for read-only UI verification.\n"}}) {
                    const auto path = QDir(state->managedPath).filePath(entry.first);
                    QDir().mkpath(QFileInfo(path).absolutePath());
                    QFile output(path);
                    if (!output.open(QIODevice::WriteOnly) ||
                        output.write(entry.second) != entry.second.size()) {
                        fail("Could not create temporary UI fixtures");
                        return;
                    }
                }
                session.workspace()->refresh();
                session.logs()->refresh();
                session.worlds()->refresh();
                session.navigation()->go(
                    {{"page", "workspace"}, {"packPath", controller.activePack().value("path")}});
                break;
            }
            case 7:
                if (session.workspace()->busy() || session.content()->busy())
                    return;
                if (!capture("workspace")) {
                    fail("Workspace render failed");
                    return;
                }
                session.navigation()->go(
                    {{"page", "settings"}, {"packPath", controller.activePack().value("path")}});
                break;
            case 8:
                if (!capture("settings")) {
                    fail("Settings render failed");
                    return;
                }
                session.navigation()->go(
                    {{"page", "library"}, {"packPath", controller.activePack().value("path")}});
                break;
            case 9:
                if (!capture("library-with-conflux")) {
                    fail("Unified library render failed");
                    return;
                }
                if (!controller.installations().isEmpty())
                    controller.openLibraryPack(controller.installations()
                                                   .at(controller.installations().size() > 1 ? 1 : 0)
                                                   .toMap()
                                                   .value("path")
                                                   .toString());
                else
                    session.navigation()->go(
                        {{"page", "workspace"}, {"packPath", controller.activePack().value("path")}});
                break;
            case 10:
                if (session.workspace()->busy() || session.content()->busy())
                    return;
                if (!capture("launcher-content")) {
                    fail("Launcher content render failed");
                    return;
                }
                if (auto *workspace = window->findChild<QObject *>("packWarnings"))
                    QMetaObject::invokeMethod(workspace, "open");
                window->setWidth(1080);
                window->setHeight(720);
                break;
            case 11:
                if (auto *popup = window->findChild<QObject *>("packWarnings");
                    popup && popup->property("opened").toBool()) {
                    auto *item = popup->property("contentItem").value<QQuickItem *>();
                    const auto point = item ? item->mapToScene(QPointF{}) : QPointF{-1, -1};
                    if (!item || point.x() < 0 || point.y() < 0 ||
                        point.x() + item->width() > window->width() + 1) {
                        fail("Warning popup escaped the narrow window");
                        return;
                    }
                }
                if (!capture("launcher-content-narrow")) {
                    fail("Narrow content render failed");
                    return;
                }
                window->setWidth(1360);
                window->setHeight(900);
                if (auto *workspace = window->findChild<QObject *>("packWarnings"))
                    QMetaObject::invokeMethod(workspace, "close");
                session.navigation()->go(
                    {{"page", "workspace"}, {"packPath", state->managedPath}, {"section", "configs"}});
                break;
            case 12:
                if (controller.repositoryBusy() || session.workspace()->busy() ||
                    controller.activePack().value("path") != state->managedPath)
                    return;
                if (!capture("configs")) {
                    fail("Config list render failed");
                    return;
                }
                session.navigation()->go({{"page", "workspace"},
                                          {"packPath", state->managedPath},
                                          {"section", "configs"},
                                          {"configPath", "config/fabric-api.toml"}});
                break;
            case 13:
                if (session.editor()->busy())
                    return;
                if (session.editor()->fields().size() != 4) {
                    fail("Visual config fields failed: " + session.editor()->error());
                    return;
                }
                if (!capture("config-editor")) {
                    fail("Config editor render failed");
                    return;
                }
                session.editor()->setField("/general/enabled", false);
                if (session.navigation()->go({{"page", "library"}}) || !session.navigation()->hasPending()) {
                    fail("Unsaved config navigation was not guarded");
                    return;
                }
                session.navigation()->cancelPending();
                if (window->close() || !window->isVisible() || !window->property("closeRequested").toBool()) {
                    fail("Unsaved config window closing was not guarded");
                    return;
                }
                window->setProperty("closeRequested", false);
                if (auto *dialog = window->findChild<QObject *>("unsavedConfigDialog"))
                    QMetaObject::invokeMethod(dialog, "close");
                session.editor()->save();
                break;
            case 14: {
                if (session.editor()->busy())
                    return;
                if (session.editor()->dirty() || !session.editor()->error().isEmpty()) {
                    fail("Temporary config save failed: " + session.editor()->error());
                    return;
                }
                QFile config(QDir(state->managedPath).filePath("config/fabric-api.toml"));
                if (!config.open(QIODevice::ReadOnly) ||
                    !config.readAll().contains("enabled = false # keep this comment")) {
                    fail("Config round trip lost values/comments");
                    return;
                }
                if (!capture("config-saved")) {
                    fail("Saved config render failed");
                    return;
                }
                session.navigation()->go(
                    {{"page", "workspace"}, {"packPath", state->managedPath}, {"section", "files"}});
                break;
            }
            case 15:
                if (session.files()->busy())
                    return;
                if (!capture("files")) {
                    fail("Files render failed");
                    return;
                }
                session.navigation()->go({{"page", "workspace"},
                                          {"packPath", state->managedPath},
                                          {"section", "logs"},
                                          {"logPath", "logs/latest.log"}});
                break;
            case 16:
                if (session.logs()->busy())
                    return;
                if (!session.logs()->text().contains("Temporary UI verification")) {
                    fail("Log view failed");
                    return;
                }
                if (!capture("logs")) {
                    fail("Logs render failed");
                    return;
                }
                session.navigation()->go(
                    {{"page", "workspace"}, {"packPath", state->managedPath}, {"section", "worlds"}});
                break;
            case 17:
                if (session.worlds()->busy())
                    return;
                if (session.worlds()->entries().isEmpty()) {
                    fail("World list fixture missing");
                    return;
                }
                if (!capture("worlds")) {
                    fail("Worlds render failed");
                    return;
                }
                session.navigation()->go({{"page", "settings"}, {"settingsTab", "warnings"}});
                break;
            case 18: {
                if (!capture("warnings")) {
                    fail("Warning settings render failed");
                    return;
                }
                QMouseEvent back(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10), Qt::BackButton,
                                 Qt::BackButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &back);
                if (session.navigation()->route().value("section") != "worlds") {
                    fail("Window mouse Back did not restore the prior pack section");
                    return;
                }
                QMouseEvent forward(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                                    Qt::ForwardButton, Qt::ForwardButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &forward);
                if (session.navigation()->route().value("settingsTab") != "warnings") {
                    fail("Window mouse Forward did not restore warning settings");
                    return;
                }
                session.navigation()->go(
                    {{"page", "workspace"}, {"packPath", state->managedPath}, {"section", "content"}});
                break;
            }
            case 19:
                if (controller.repositoryBusy() || session.workspace()->busy() || session.content()->busy())
                    return;
                if (session.content()->rows().isEmpty()) {
                    fail("Content actions did not load the installed fixture");
                    return;
                }
                state->selectedMod =
                    session.content()->rows().first().toMap().value("relativePath").toString();
                session.content()->select(state->selectedMod, true);
                session.content()->freeze(state->selectedMod, true);
                break;
            case 20:
                if (!capture("content-selected")) {
                    fail("Content selection render failed");
                    return;
                }
                if (auto *popup = window->findChild<QObject *>("contentFilter"))
                    QMetaObject::invokeMethod(popup, "open");
                else {
                    fail("Content filter popup missing");
                    return;
                }
                break;
            case 21:
                if (!capture("content-filter")) {
                    fail("Content filter render failed");
                    return;
                }
                if (auto *popup = window->findChild<QObject *>("contentFilter"))
                    QMetaObject::invokeMethod(popup, "close");
                session.content()->freeze(state->selectedMod, false);
                session.content()->openVersions(state->selectedMod);
                if (auto *popup = window->findChild<QObject *>("versionPicker"))
                    QMetaObject::invokeMethod(popup, "open");
                else {
                    fail("Version picker missing");
                    return;
                }
                break;
            case 22: {
                if (session.content()->versionsBusy())
                    return;
                if (session.content()->versions().isEmpty()) {
                    fail("No compatible switch versions: " + session.content()->versionError());
                    return;
                }
                if (!capture("version-picker")) {
                    fail("Version picker render failed");
                    return;
                }
                if (auto *popup = window->findChild<QObject *>("versionPicker"))
                    QMetaObject::invokeMethod(popup, "close");
                break;
            }
            case 23: {
                auto *panel = findVisualItem(window->contentItem(), "contentPanel");
                if (!panel || !QMetaObject::invokeMethod(panel, "showcaseRequested",
                                                         Q_ARG(QString, state->selectedMod))) {
                    fail("Installed content showcase signal is unavailable");
                    return;
                }
                break;
            }
            case 24:
                if (session.showcase()->busy() || session.workspace()->busy() || session.content()->busy())
                    return;
                if (session.navigation()->route().value("page") != "showcase" ||
                    session.showcase()->project().value("provider") != "modrinth" ||
                    session.showcase()->description().isEmpty()) {
                    fail("Clicking installed content did not load its native verified showcase: " +
                         session.showcase()->notice());
                    return;
                }
                if (!capture("showcase-description")) {
                    fail("Showcase description render failed");
                    return;
                }
                {
                    auto route = session.navigation()->route();
                    route["showcaseTab"] = "gallery";
                    session.navigation()->go(route);
                }
                break;
            case 25:
                if (!capture("showcase-gallery")) {
                    fail("Showcase gallery render failed");
                    return;
                }
                {
                    auto route = session.navigation()->route();
                    route["showcaseTab"] = "versions";
                    session.navigation()->go(route);
                }
                break;
            case 26:
                if (!capture("showcase-versions")) {
                    fail("Showcase version render failed");
                    return;
                }
                session.navigation()->go({{"page", "settings"}, {"settingsTab", "appearance"}});
                break;
            case 27:
                if (!capture("settings-vertical")) {
                    fail("Vertical settings render failed");
                    return;
                }
                window->setWidth(1080);
                window->setHeight(720);
                break;
            case 28:
                if (!capture("settings-vertical-narrow")) {
                    fail("Narrow settings render failed");
                    return;
                }
                if (smokeRuntimeError) {
                    fail("QML runtime warnings occurred; inspect smoke.log");
                    return;
                }
                qInfo("PASS: live API/install/Git, native installed-mod showcase, content "
                      "selection/filter/version picker, vertical Settings, navigation, visual config saves, "
                      "worlds and warnings renders");
                timer->stop();
                application.exit(0);
                return;
            }
            ++state->phase;
        });
    timer->start();
}
} // namespace studio
