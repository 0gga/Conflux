#include "DesktopSmokeTest.h"
#include "ApplicationController.h"
#include "DesktopSession.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QProcess>
#include <QQuickWindow>
#include <QTimer>
#include <atomic>
#include <memory>
namespace studio {
static QString smokeLogPath;
static std::atomic_bool smokeRuntimeError{false};
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
        QElapsedTimer elapsed;
    };
    auto state = std::make_shared<SmokeState>();
    state->elapsed.start();
    QObject::connect(
        timer, &QTimer::timeout, &application,
        [window, folder, timer, state, &application, &controller, &session] {
            if (state->elapsed.elapsed() > 65000 || !window) {
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
                break;
            case 2:
                if (controller.detailBusy())
                    return;
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
                if (session.workspace()->busy())
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
                if (session.workspace()->busy())
                    return;
                if (!capture("launcher-content")) {
                    fail("Launcher content render failed");
                    return;
                }
                if (auto *workspace = window->findChild<QObject *>("packWorkspace"))
                    workspace->setProperty("showIssues", true);
                window->setWidth(1080);
                window->setHeight(720);
                break;
            case 11:
                if (!capture("launcher-content-narrow")) {
                    fail("Narrow content render failed");
                    return;
                }
                window->setWidth(1360);
                window->setHeight(900);
                if (auto *workspace = window->findChild<QObject *>("packWorkspace"))
                    workspace->setProperty("showIssues", false);
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
                if (window->close() || !window->isVisible() ||
                    !window->property("closeRequested").toBool()) {
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
                if (smokeRuntimeError) {
                    fail("QML runtime warnings occurred; inspect smoke.log");
                    return;
                }
                qInfo("PASS: live API/install/Git, navigation, pack sections, identified config icons, "
                      "visual TOML editing with preserved comments/backups, logs, worlds and warnings "
                      "renders");
                timer->stop();
                application.exit(0);
                return;
            }
            }
            ++state->phase;
        });
    timer->start();
}
} // namespace studio
