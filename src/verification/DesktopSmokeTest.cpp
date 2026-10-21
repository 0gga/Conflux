#include "DesktopSmokeTest.h"
#include "ApplicationController.h"
#include "DesktopSession.h"
#include "theme/Appearance.h"
#include "theme/Theme.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QProcess>
#include <QQmlEngine>
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
            message.contains("Error decoding") || message.contains("recursive rearrange"))
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
        QString externalPath;
        QVariantMap assetProject;
        QString pagedProjectId;
        QElapsedTimer elapsed;
    };
    auto state = std::make_shared<SmokeState>();
    state->elapsed.start();
    QObject::connect(
        timer, &QTimer::timeout, &application,
        [window, folder, timer, state, &application, &controller, &session] {
            if (state->elapsed.elapsed() > 180000 || !window) {
                application.exit(2);
                return;
            }
            auto capture = [window, folder](const QString &name) {
                for (auto *item : window->findChildren<QQuickItem *>()) {
                    const auto bounds = item->property("boundsBehavior");
                    if (bounds.isValid() && bounds.toInt() != 0) {
                        qWarning().noquote()
                            << "Scroll overshoot still enabled in" << name << item->objectName();
                        return false;
                    }
                }
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
                controller.search("Fabric API", "modrinth", "1.21.1", "fabric");
                session.catalogue()->configure(
                    {{"query", "Fabric API"}, {"source", "modrinth"}, {"type", "mod"}});
                session.navigation()->go(
                    {{"page", "discover"}, {"packPath", controller.activePack().value("path")}});
                break;
            case 1:
                if (controller.catalogueBusy() || session.catalogue()->busy())
                    return;
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
                    {{"page", "showcase"},
                     {"packPath", ""},
                     {"project", QVariantMap{{"provider", "modrinth"},
                                             {"id", "zV5r3pPn"},
                                             {"title", "3D Skin Layers"},
                                             {"sourceIds", QVariantMap{{"modrinth", "zV5r3pPn"}}}}}});
                break;
            case 2:
                if (controller.detailBusy() || session.showcase()->busy())
                    return;
                if (session.showcase()->project().value("provider") != "modrinth") {
                    fail("Catalogue showcase failed without an active pack");
                    return;
                }
                if (session.showcase()->description().size() < 100) {
                    fail("Mixed HTML/Markdown 3D Skin Layers description was lost");
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
                if (session.content()->busy() || session.workspace()->busy() || !session.browse()->ready())
                    return;
                session.browse()->install(controller.projects().first().toMap());
                break;
            case 5: {
                if (controller.repositoryBusy() || session.browse()->busy() || session.workspace()->busy())
                    return;
                if (controller.activeMods().size() != 1) {
                    fail("Pin failed: " + controller.notice());
                    return;
                }
                if (!session.browse()->installed(controller.activeMods().first().toMap())) {
                    fail("Newly installed version was not marked installed for its instance");
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
            case 10: {
                state->externalPath = controller.activePack().value("path").toString();
                if (session.workspace()->busy() || session.content()->busy())
                    return;
                int editable = 0;
                for (const auto &row : session.configs()->files())
                    if (row.toMap().value("editable").toBool())
                        ++editable;
                qInfo() << "Real launcher editable config count:" << editable;
                if (!capture("launcher-content")) {
                    fail("Launcher content render failed");
                    return;
                }
                if (auto *workspace = window->findChild<QObject *>("packWarnings"))
                    QMetaObject::invokeMethod(workspace, "open");
                window->setWidth(1080);
                window->setHeight(720);
                break;
            }
            case 11:
                if (auto *popup = window->findChild<QObject *>("packWarnings");
                    popup && popup->property("opened").toBool()) {
                    if (!popup->property("modal").toBool() || !popup->property("activeFocus").toBool()) {
                        fail("Warning popup did not take exclusive focus");
                        return;
                    }
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
                if (!panel ||
                    !QMetaObject::invokeMethod(panel, "showcaseRequested", Q_ARG(QString, state->selectedMod),
                                               Q_ARG(QString, QString("description")))) {
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
                session.navigation()->go(
                    {{"page", "workspace"}, {"packPath", state->externalPath}, {"section", "configs"}});
                break;
            case 29:
                if (session.workspace()->busy() || controller.repositoryBusy() || session.content()->busy())
                    return;
                if (!capture("launcher-configs")) {
                    fail("Launcher config availability render failed");
                    return;
                }
                if (smokeRuntimeError) {
                    fail("QML runtime warnings occurred; inspect smoke.log");
                    return;
                }
                session.navigation()->go({{"page", "discover"}, {"packPath", state->managedPath}});
                session.catalogue()->configure(
                    {{"type", "resourcepack"}, {"query", "Fast Better Grass"}, {"source", "modrinth"}});
                break;
            case 30:
                if (controller.repositoryBusy() || session.catalogue()->busy() ||
                    session.workspace()->busy() || session.browse()->busy())
                    return;
                if (session.catalogue()->projects().isEmpty() || !session.browse()->ready() ||
                    session.browse()->pack().value("path") != state->managedPath) {
                    fail("Resource-pack browse lost its instance context");
                    return;
                }
                state->assetProject = session.catalogue()->projects().first().toMap();
                session.browse()->install(state->assetProject);
                break;
            case 31:
                if (controller.repositoryBusy() || session.browse()->busy() || session.workspace()->busy())
                    return;
                if (!session.browse()->state(state->assetProject).value("installed").toBool() ||
                    controller.activePack().value("assets").toList().isEmpty()) {
                    fail("Verified resource-pack install or installed-project badge failed: " +
                         session.content()->notice());
                    return;
                }
                if (!capture("explore-installed-resource")) {
                    fail("Explore resource-pack render failed");
                    return;
                }
                session.navigation()->go({{"page", "showcase"},
                                          {"packPath", state->managedPath},
                                          {"project", state->assetProject},
                                          {"showcaseTab", "versions"}});
                break;
            case 32:
                if (session.showcase()->busy() || session.browse()->busy())
                    return;
                {
                    bool installed = false;
                    for (const auto &group : session.showcase()->versions())
                        for (const auto &release : group.toMap().value("variants").toList())
                            installed = installed || session.browse()->installed(release.toMap());
                    if (!installed) {
                        fail("Resource-pack version was not marked installed in its instance");
                        return;
                    }
                }
                if (!capture("resource-version-installed")) {
                    fail("Installed resource version render failed");
                    return;
                }
                session.navigation()->go({{"page", "discover"}, {"packPath", state->managedPath}});
                window->setWidth(1360);
                window->setHeight(900);
                break;
            case 33:
                if (!capture("explore-wide")) {
                    fail("Explore wide render failed");
                    return;
                }
                window->setWidth(1080);
                window->setHeight(720);
                break;
            case 34:
                if (!capture("explore-narrow")) {
                    fail("Explore narrow render failed");
                    return;
                }
                session.catalogue()->configure({{"type", "datapack"}, {"query", ""}});
                break;
            case 35:
                if (session.catalogue()->busy())
                    return;
                if (session.catalogue()->projects().isEmpty() ||
                    session.catalogue()->projects().first().toMap().value("type") != "datapack" ||
                    !capture("explore-data-packs")) {
                    fail("Live data-pack search or render failed");
                    return;
                }
                session.catalogue()->configure({{"type", "shader"}});
                break;
            case 36:
                if (session.catalogue()->busy())
                    return;
                if (session.catalogue()->projects().isEmpty() ||
                    session.catalogue()->projects().first().toMap().value("type") != "shader" ||
                    !capture("explore-shaders")) {
                    fail("Live shader search or render failed");
                    return;
                }
                session.catalogue()->configure({{"type", "mod"}, {"sort", "downloads"}, {"size", 10}});
                break;
            case 37:
                if (session.catalogue()->busy())
                    return;
                session.catalogue()->next();
                break;
            case 38:
                if (session.catalogue()->busy())
                    return;
                if (session.catalogue()->page() != 2 || session.catalogue()->projects().isEmpty()) {
                    fail("Live pagination did not reach page two");
                    return;
                }
                state->pagedProjectId =
                    session.catalogue()->projects().first().toMap().value("id").toString();
                session.catalogue()->setScrollPosition(80);
                session.navigation()->go({{"page", "showcase"},
                                          {"packPath", state->managedPath},
                                          {"project", session.catalogue()->projects().first().toMap()}});
                break;
            case 39:
                if (session.showcase()->busy())
                    return;
                session.navigation()->back();
                break;
            case 40:
                if (session.catalogue()->busy())
                    return;
                if (session.catalogue()->page() != 2 ||
                    session.catalogue()->projects().first().toMap().value("id") != state->pagedProjectId ||
                    session.catalogue()->scrollPosition() != 80) {
                    fail("Browse page/scroll state was lost after viewing a project");
                    return;
                }
                if (!capture("browse-restored-page-two")) {
                    fail("Restored browse render failed");
                    return;
                }
                session.catalogue()->hide(session.catalogue()->projects().first().toMap());
                break;
            case 41:
                if (session.catalogue()->busy())
                    return;
                session.catalogue()->configure({{"hidden", true}});
                break;
            case 42:
                if (session.catalogue()->busy())
                    return;
                if (session.catalogue()->projects().size() != 1 ||
                    session.catalogue()->projects().first().toMap().value("id") != state->pagedProjectId) {
                    fail("Hidden catalogue content was not isolated");
                    return;
                }
                if (!capture("browse-hidden-content")) {
                    fail("Hidden content render failed");
                    return;
                }
                session.catalogue()->hide(session.catalogue()->projects().first().toMap(), false);
                break;
            case 43:
                if (session.catalogue()->busy())
                    return;
                if (!session.catalogue()->projects().isEmpty()) {
                    fail("Restored project remained hidden");
                    return;
                }
                session.catalogue()->configure({{"hidden", false}});
                session.navigation()->go({{"page", "settings"}, {"settingsTab", "browse"}});
                break;
            case 44:
                if (!capture("settings-browsing")) {
                    fail("Browsing settings render failed");
                    return;
                }
                if (controller.activeMods().size() != 1 ||
                    controller.activePack().value("assets").toList().size() != 1) {
                    fail("Content visibility changed installed files");
                    return;
                }
                session.navigation()->go({{"page", "discover"}, {"packPath", state->managedPath}});
                session.catalogue()->configure({{"type", "mod"}, {"unlocked", true}});
                break;
            case 45: {
                if (session.catalogue()->busy())
                    return;
                auto *selector = window->findChild<QQuickItem *>("browseInstanceSelector");
                auto *filters = window->findChild<QQuickItem *>("browseFilterPanel");
                auto *versions = window->findChild<QQuickItem *>("browseVersionList");
                auto *title = window->findChild<QQuickItem *>("pageTitle");
                if (!selector || !filters || !versions || !title ||
                    qAbs(selector->width() - filters->width()) > 1 ||
                    qAbs(selector->mapToScene(QPointF{}).x() - filters->mapToScene(QPointF{}).x()) > 1 ||
                    !versions->isVisible() || versions->property("count").toInt() < 20 ||
                    !title->property("text").toString().endsWith(" - Exploring Content")) {
                    fail("Explore top-bar alignment or unlocked scrollable version list failed");
                    return;
                }
                if (!capture("browse-unlocked-versions")) {
                    fail("Unlocked versions render failed");
                    return;
                }
                auto *back = window->findChild<QObject *>("navigationBackButton");
                auto *tip = back ? back->findChild<QObject *>("roundedToolTip") : nullptr;
                auto *background = tip ? tip->property("background").value<QQuickItem *>() : nullptr;
                if (!background || background->property("radius").toInt() < 8) {
                    fail("Tooltip background is not rounded");
                    return;
                }
                tip->setProperty("text", "Back to the previous page");
                tip->setProperty("delay", 0);
                tip->setProperty("showWhen", true);
                break;
            }
            case 46:
                if (auto *tip = window->findChild<QObject *>("navigationBackButton")
                                    ->findChild<QObject *>("roundedToolTip");
                    !tip || !tip->property("opened").toBool())
                    return;
                if (!capture("browse-rounded-tooltip")) {
                    fail("Rounded tooltip render failed");
                    return;
                }
                window->findChild<QObject *>("navigationBackButton")
                    ->findChild<QObject *>("roundedToolTip")
                    ->setProperty("showWhen", false);
                session.navigation()->go({{"page", "settings"}, {"settingsTab", "appearance"}});
                break;
            case 47: {
                if (!capture("appearance-default-preview")) {
                    fail("Appearance preview render failed");
                    return;
                }
                auto *engine = qmlEngine(window);
                auto *appearance =
                    engine->singletonInstance<Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"));
                auto *theme = engine->singletonInstance<Theme *>(qmlTypeId("Conflux.Style", 1, 0, "Theme"));
                if (!appearance || !theme ||
                    !findVisualItem(window->contentItem(), "appearanceLivePreview")) {
                    fail("Appearance editor/preview missing");
                    return;
                }
                appearance->setValue("uiScale", 110);
                appearance->setValue("contentScale", 110);
                appearance->setValue("filterScale", 115);
                appearance->setValue("filterWidth", 256);
                appearance->setValue("artworkSize", 96);
                appearance->setValue("cardPadding", 8);
                theme->setColor("input", "#22252B");
                break;
            }
            case 48:
                if (!capture("appearance-custom-preview")) {
                    fail("Custom appearance render failed");
                    return;
                }
                session.navigation()->go({{"page", "discover"}, {"packPath", state->managedPath}});
                session.catalogue()->configure({{"unlocked", false}});
                break;
            case 49: {
                if (session.catalogue()->busy())
                    return;
                if (!capture("appearance-scaled-content")) {
                    fail("Scaled content render failed");
                    return;
                }
                auto *more = findVisualItem(window->contentItem(), "browseMoreButton");
                if (!more || !QMetaObject::invokeMethod(more, "clicked")) {
                    fail("Scaled card menu missing");
                    return;
                }
                break;
            }
            case 50: {
                if (!capture("appearance-scaled-menu")) {
                    fail("Scaled menu render failed");
                    return;
                }
                auto *engine = qmlEngine(window);
                engine->singletonInstance<Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"))
                    ->reset();
                engine->singletonInstance<Theme *>(qmlTypeId("Conflux.Style", 1, 0, "Theme"))
                    ->discardChanges();
                session.navigation()->go({{"page", "settings"}, {"settingsTab", "appearance"}});
                break;
            }
            case 51:
                if (!capture("appearance-restored")) {
                    fail("Restored appearance render failed");
                    return;
                }
                if (auto *settings = findVisualItem(window->contentItem(), "appearanceSettings"))
                    settings->setProperty("section", "colors");
                else {
                    fail("Tabbed Appearance settings missing");
                    return;
                }
                break;
            case 52: {
                if (!capture("appearance-theme-editor")) {
                    fail("Theme editor render failed");
                    return;
                }
                if (auto *theme = findVisualItem(window->contentItem(), "themeSettings"))
                    theme->setProperty("group", "status");
                break;
            }
            case 53:
                if (!capture("appearance-theme-status-colors")) {
                    fail("Theme status colors render failed");
                    return;
                }
                session.navigation()->go({{"page", "showcase"},
                                          {"packPath", state->managedPath},
                                          {"modPath", state->selectedMod},
                                          {"showcaseTab", "versions"}});
                break;
            case 54: {
                if (session.showcase()->busy())
                    return;
                auto *channel = findVisualItem(window->contentItem(), "versionChannelFilter");
                if (!channel || session.releaseFilters()->rows().isEmpty() ||
                    !session.releaseFilters()->filters().value("loaders").toStringList().contains("fabric") ||
                    session.releaseFilters()->options().value("versions").toStringList().size() < 2 ||
                    !QMetaObject::invokeMethod(channel, "clicked")) {
                    fail("Version channel filter or unfiltered release options missing");
                    return;
                }
                break;
            }
            case 55: {
                if (!capture("showcase-version-filters")) {
                    fail("Release filters render failed");
                    return;
                }
                auto *channel = findVisualItem(window->contentItem(), "versionChannelFilter");
                auto *popup = channel->property("popup").value<QObject *>();
                QMetaObject::invokeMethod(channel, "clicked");
                if (!popup || popup->property("visible").toBool()) {
                    fail("Filter menu did not toggle closed");
                    return;
                }
                session.navigation()->go({{"page", "discover"}, {"packPath", state->managedPath}});
                session.catalogue()->configure({{"type", "mod"}, {"unlocked", false}});
                break;
            }
            case 56: {
                if (session.catalogue()->busy())
                    return;
                auto *chip = findVisualItem(window->contentItem(), "activeGameFilter");
                if (!chip || !chip->property("locked").toBool() || !capture("browse-active-filter-chips")) {
                    fail("Instance-locked active filter chip missing");
                    return;
                }
                session.navigation()->go({{"page", "showcase"},
                                          {"packPath", state->managedPath},
                                          {"project", QVariantMap{{"provider", "modrinth"},
                                                                  {"id", "NNAgCjsB"},
                                                                  {"type", "mod"},
                                                                  {"title", "Entity Culling"}}},
                                          {"showcaseTab", "description"}});
                break;
            }
            case 57: {
                if (session.showcase()->busy())
                    return;
                auto *banner = findVisualItem(window->contentItem(), "descriptionImage");
                if (banner && banner->property("status").toInt() == 2)
                    return;
                if (!banner || banner->property("status").toInt() != 1 ||
                    session.showcase()->descriptionBlocks().isEmpty() ||
                    !capture("entity-culling-description")) {
                    fail("Source description banner or content blocks missing");
                    return;
                }
                auto route = session.navigation()->route();
                route["showcaseTab"] = "gallery";
                session.navigation()->go(route);
                break;
            }
            case 58: {
                if (session.showcase()->gallery().isEmpty() || !capture("entity-culling-gallery")) {
                    fail("Entity Culling gallery missing");
                    return;
                }
                auto *gallery = findVisualItem(window->contentItem(), "showcaseGallery");
                if (!gallery || !QMetaObject::invokeMethod(gallery, "imageRequested", Q_ARG(int, 0))) {
                    fail("Gallery thumbnail action missing");
                    return;
                }
                break;
            }
            case 59: {
                auto *viewer = window->findChild<QObject *>("galleryViewer");
                if (!viewer || !viewer->property("activeFocus").toBool() || !capture("gallery-enlarged")) {
                    fail("Enlarged focused gallery missing");
                    return;
                }
                auto *zoom = findVisualItem(window->contentItem(), "galleryZoomIn");
                if (!zoom || !QMetaObject::invokeMethod(zoom, "clicked")) {
                    fail("Gallery zoom control missing");
                    return;
                }
                break;
            }
            case 60: {
                if (!capture("gallery-enlarged-zoom")) {
                    fail("Gallery zoom render failed");
                    return;
                }
                auto *close = findVisualItem(window->contentItem(), "galleryClose");
                if (!close || !QMetaObject::invokeMethod(close, "clicked")) {
                    fail("Gallery close control missing");
                    return;
                }
                session.navigation()->go(
                    {{"page", "workspace"}, {"packPath", state->externalPath}, {"section", "content"}});
                break;
            }
            case 61: {
                if (session.workspace()->busy() || session.content()->busy())
                    return;
                auto *list = findVisualItem(window->contentItem(), "instanceContentList");
                auto *viewport = findVisualItem(window->contentItem(), "contentListViewport");
                if (!list || !viewport) {
                    fail("Instance table viewport missing");
                    return;
                }
                list->setProperty("contentY", 39);
                const auto point = viewport->mapToScene(QPointF(300, 12));
                QMouseEvent move(QEvent::MouseMove, point, point, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &move);
                break;
            }
            case 62: {
                if (!capture("launcher-content-hover-top")) {
                    fail("Clipped top row hover render failed");
                    return;
                }
                auto *viewport = findVisualItem(window->contentItem(), "contentListViewport");
                const auto point = viewport->mapToScene(QPointF(300, viewport->height() - 8));
                QMouseEvent move(QEvent::MouseMove, point, point, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(window, &move);
                break;
            }
            case 63: {
                if (!capture("launcher-content-hover-bottom")) {
                    fail("Clipped bottom row hover render failed");
                    return;
                }
                auto *list = findVisualItem(window->contentItem(), "instanceContentList");
                list->setProperty("contentY", list->property("contentHeight").toReal() - list->height());
                break;
            }
            case 64: {
                if (!capture("launcher-content-hover-end")) {
                    fail("Final row hover render failed");
                    return;
                }
                session.navigation()->go({{"page", "discover"}, {"packPath", state->externalPath}});
                session.catalogue()->configure({{"type", "mod"},
                                                {"query", "Sodium"},
                                                {"source", "modrinth"},
                                                {"sort", "relevance"},
                                                {"size", 10}});
                break;
            }
            case 65: {
                if (session.catalogue()->busy())
                    return;
                if (session.catalogue()->projects().isEmpty() || !capture("explore-card-statistics")) {
                    fail("Card statistics render failed");
                    return;
                }
                session.navigation()->go({{"page", "settings"}, {"settingsTab", "appearance"}});
                window->setWidth(1360);
                window->setHeight(900);
                break;
            }
            case 66: {
                auto *settings = findVisualItem(window->contentItem(), "appearanceSettings");
                auto *appearance = qmlEngine(window)->singletonInstance<Appearance *>(
                    qmlTypeId("Conflux.Style", 1, 0, "Appearance"));
                if (!settings || !appearance) {
                    fail("Configurable settings missing");
                    return;
                }
                settings->setProperty("section", "interface");
                appearance->setValue("libraryRowHeight", 112);
                appearance->setValue("contentRowHeight", 68);
                appearance->setValue("navigationMode", 1);
                appearance->setValue("reduceMotion", 1);
                appearance->setValue("tooltipDelay", 100);
                appearance->setValue("showFollowers", 0);
                break;
            }
            case 67:
                if (!capture("settings-interface-options")) {
                    fail("Interface settings render failed");
                    return;
                }
                findVisualItem(window->contentItem(), "appearanceSettings")
                    ->setProperty("section", "content");
                break;
            case 68:
                if (!capture("settings-content-options")) {
                    fail("Content settings render failed");
                    return;
                }
                findVisualItem(window->contentItem(), "appearanceSearch")->setProperty("text", "follower");
                break;
            case 69:
                if (!capture("settings-search-result")) {
                    fail("Settings search render failed");
                    return;
                }
                findVisualItem(window->contentItem(), "appearanceSearch")->setProperty("text", "");
                findVisualItem(window->contentItem(), "appearanceSettings")
                    ->setProperty("section", "filters");
                break;
            case 70:
                if (!capture("settings-filter-options")) {
                    fail("Filter settings render failed");
                    return;
                }
                findVisualItem(window->contentItem(), "appearanceSettings")->setProperty("section", "colors");
                findVisualItem(window->contentItem(), "themeSettings")->setProperty("group", "surfaces");
                break;
            case 71:
                if (!capture("settings-color-surfaces")) {
                    fail("Color surfaces render failed");
                    return;
                }
                findVisualItem(window->contentItem(), "themeSettings")->setProperty("group", "status");
                break;
            case 72: {
                if (!capture("settings-color-status")) {
                    fail("Status colors render failed");
                    return;
                }
                auto *settings = findVisualItem(window->contentItem(), "appearanceSettings");
                qmlEngine(window)
                    ->singletonInstance<Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"))
                    ->setValue("previewVisible", 0);
                settings->setProperty("section", "content");
                qmlEngine(window)
                    ->singletonInstance<Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"))
                    ->setValue("uiScale", 150);
                window->setWidth(1080);
                window->setHeight(720);
                break;
            }
            case 73:
                if (!capture("settings-compact-scaled")) {
                    fail("Compact scaled settings render failed");
                    return;
                }
                qmlEngine(window)
                    ->singletonInstance<Appearance *>(qmlTypeId("Conflux.Style", 1, 0, "Appearance"))
                    ->reset();
                window->setWidth(1360);
                window->setHeight(900);
                session.navigation()->go({{"page", "settings"}, {"settingsTab", "browse"}});
                session.browsePreferences()->setDefault("size", 50);
                session.browsePreferences()->setDefault("sort", "updated");
                session.browsePreferences()->setDefault("hideInstalled", true);
                break;
            case 74:
                if (!capture("settings-browsing-defaults")) {
                    fail("Browsing defaults render failed");
                    return;
                }
                findVisualItem(window->contentItem(), "browsingSettings")
                    ->setProperty("section", "compatibility");
                break;
            case 75:
                if (!capture("settings-browsing-compatibility")) {
                    fail("Compatibility settings render failed");
                    return;
                }
                session.browsePreferences()->resetDefaults();
                break;
            case 76:
                session.navigation()->go(
                    {{"page", "discover"},
                     {"author",
                      QVariantMap{{"name", "modmuss50"}, {"ids", QVariantMap{{"modrinth", "modmuss50"}}}}}});
                break;
            case 77: {
                if (session.catalogue()->busy())
                    return;
                if (session.catalogue()->projects().isEmpty() ||
                    session.catalogue()->filters().value("type") != "all" || !capture("author-content")) {
                    fail("Author content retrieval failed");
                    return;
                }
                auto *name = findVisualItem(window->contentItem(), "browseTitle");
                if (!name || !QMetaObject::invokeMethod(name, "clicked")) {
                    fail("Author project name link missing");
                    return;
                }
                break;
            }
            case 78: {
                if (session.showcase()->busy())
                    return;
                if (session.navigation()->route().value("page") != "showcase" ||
                    session.showcase()->project().isEmpty()) {
                    fail("Author project navigation failed");
                    return;
                }
                auto route = session.navigation()->route();
                route["showcaseTab"] = "versions";
                session.navigation()->go(route);
                break;
            }
            case 79:
                if (!capture("author-project-versions")) {
                    fail("Author project version render failed");
                    return;
                }
                session.navigation()->back();
                break;
            case 80:
                if (session.catalogue()->busy())
                    return;
                if (session.navigation()->route().value("author").toMap().value("name") != "modmuss50" ||
                    session.catalogue()->projects().isEmpty()) {
                    fail("Author context restoration failed");
                    return;
                }
                break;
            case 81: {
                if (smokeRuntimeError) {
                    fail("QML runtime warning in Explore content");
                    return;
                }
                qInfo().noquote()
                    << "PASS: authored project retrieval/name links/version tab/history; all-type "
                       "update/selection UI integration; 83% gallery fit baseline and viewer controls; "
                       "tabbed/searchable settings, "
                       "presentation controls, browse defaults and "
                       "compact150% UI; "
                       "contiguous table hover/top/bottom clipping and aligned statistics; "
                       "source images/formatted descriptions, enlarged gallery zoom, reserved scroll "
                       "gutters, stable hover; channel/game/loader version filters, active discovery chips "
                       "and toggle menus; "
                       "candy-red theme, live appearance preview, independent UI/content/filter sizing, "
                       "compact cards and scaled menus; compact contextual top bar, scrollable versions and "
                       "rounded tooltips; provider "
                       "pagination and page/scroll restoration, right filters, stop-at-bounds "
                       "scrolling; instance-scoped browse installs, exact installed-version badges, verified "
                       "live "
                       "ZIP assets, four content types and branded rounded cards; live API/install/Git, "
                       "native installed-mod showcase, content "
                       "selection/filter/version picker, vertical Settings, navigation, visual config saves, "
                       "worlds and warnings renders";
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
