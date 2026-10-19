#include "ApplicationController.h"
#include <QDir>
#include <QFontDatabase>
#include <QFile>
#include <QElapsedTimer>
#include <QProcess>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QTemporaryDir>
#include <memory>
#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen")
        qputenv("QT_QPA_FONTDIR", QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath().toUtf8());
#endif
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName("ModpackStudio");
    QCoreApplication::setApplicationName("ModpackStudio");
    QCoreApplication::setApplicationVersion("0.1.0");
    QGuiApplication::setApplicationDisplayName("Conflux");
#ifdef Q_OS_WIN
    // The offscreen Windows plugin does not enumerate installed fonts itself.
    const auto fonts = qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/";
    QFontDatabase::addApplicationFont(fonts + "segoeui.ttf");
    QFontDatabase::addApplicationFont(fonts + "seguisb.ttf");
    QFontDatabase::addApplicationFont(fonts + "segoeuib.ttf");
    QFontDatabase::addApplicationFont(fonts + "seguisym.ttf");
    QGuiApplication::setFont(QFont("Segoe UI"));
#endif
    QQuickStyle::setStyle("Basic");
    const auto arguments = application.arguments();
    const auto smokeIndex = arguments.indexOf("--smoke-test");
    static QString smokeLogPath;
    if (smokeIndex >= 0 && arguments.size() > smokeIndex + 1) {
        QDir().mkpath(arguments.at(smokeIndex + 1));
        smokeLogPath = QDir(arguments.at(smokeIndex + 1)).filePath("smoke.log");
        QFile log(smokeLogPath); if (log.open(QIODevice::WriteOnly)) log.close();
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& message) {
            QFile log(smokeLogPath);
            if (log.open(QIODevice::Append)) log.write(message.toUtf8() + '\n');
        });
    }
    QTemporaryDir smokeStorage;
    studio::ApplicationController controller(nullptr, smokeIndex >= 0 ? smokeStorage.path() + "/packs" : QString{});
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"app", QVariant::fromValue(&controller)}});
    engine.load(QUrl("qrc:/qt/qml/ModpackStudio/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    controller.refreshLibrary();
    if (smokeIndex >= 0 && arguments.size() > smokeIndex + 1) {
        const auto folder = arguments.at(smokeIndex + 1);
        QDir().mkpath(folder);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        auto* timer = new QTimer(&application);
        timer->setInterval(300);
        struct SmokeState { int phase = 0; QElapsedTimer elapsed; };
        auto state = std::make_shared<SmokeState>(); state->elapsed.start();
        QObject::connect(timer, &QTimer::timeout, &application, [window, folder, timer, state, &application, &controller] {
            if (state->elapsed.elapsed() > 45000 || !window) { application.exit(2); return; }
            auto capture = [window, folder](const QString& name) {
                const auto image = window->grabWindow();
                return !image.isNull() && image.save(QDir(folder).filePath(name + ".png"));
            };
            auto fail = [&application, timer](const QString& reason) { qWarning().noquote() << reason; timer->stop(); application.exit(3); };
            switch (state->phase) {
            case 0:
                if (controller.libraryBusy() || controller.catalogueBusy()) return;
                if (!capture("library")) { fail("Library render failed"); return; }
                window->setProperty("page", "discover"); break;
            case 1:
                if (!capture("discover")) { fail("Discovery render failed"); return; }
                if (controller.projects().isEmpty()) { fail("Live catalogue has no results"); return; }
                controller.selectProject(0); break;
            case 2:
                if (controller.detailBusy()) return;
                if (!capture("project")) { fail("Project render failed"); return; }
                controller.createPack("UI verification pack", "1.21.1", "fabric"); break;
            case 3:
                if (controller.repositoryBusy() || controller.catalogueBusy()) return;
                if (controller.activePack().isEmpty() || controller.projects().isEmpty()) { fail("Pack creation or filtered search failed: " + controller.notice()); return; }
                controller.selectProject(0); break;
            case 4:
                if (controller.detailBusy()) return;
                if (controller.releases().isEmpty()) { fail("No compatible release returned"); return; }
                controller.pinRelease(0); break;
            case 5: {
                if (controller.repositoryBusy()) return;
                if (controller.activeMods().size() != 1) { fail("Pin failed: " + controller.notice()); return; }
                const auto path = controller.activePack().value("path").toString();
                for (const auto& pair : QList<QStringList>{{"user.name", "UI verification"}, {"user.email", "verification@example.invalid"}, {"core.hooksPath", path + "/no-hooks"}}) {
                    QProcess git; git.setWorkingDirectory(path); git.start("git", {"config", pair.at(0), pair.at(1)});
                    if (!git.waitForFinished(3000) || git.exitCode() != 0) { fail("Could not configure temporary test repository"); return; }
                }
                controller.saveChanges("Pin first mod release"); break;
            }
            case 6:
                if (controller.repositoryBusy() || controller.contentBusy()) return;
                if (!controller.history().contains("Pin first mod release") || !controller.gitStatus().isEmpty()) { fail("Git save failed: " + controller.notice()); return; }
                window->setProperty("page", "workspace"); break;
            case 7:
                if (!capture("workspace")) { fail("Workspace render failed"); return; }
                window->setProperty("page", "settings"); break;
            case 8:
                if (!capture("settings")) { fail("Settings render failed"); return; }
                window->setProperty("page", "library"); break;
            case 9:
                if (!capture("library-with-conflux")) { fail("Unified library render failed"); return; }
                if (!controller.installations().isEmpty()) controller.openLibraryPack(controller.installations().at(controller.installations().size() > 1 ? 1 : 0).toMap().value("path").toString());
                else window->setProperty("page", "workspace");
                break;
            case 10:
                if (controller.contentBusy()) return;
                if (!capture("launcher-content")) { fail("Launcher content render failed"); return; }
                if (auto* workspace = window->findChild<QObject*>("packWorkspace")) workspace->setProperty("showIssues", true);
                window->setWidth(1080); window->setHeight(720); break;
            case 11:
                if (!capture("launcher-content-narrow")) { fail("Narrow content render failed"); return; }
                qInfo("PASS: live search, project details, temporary pack installation, Git commit, unified library and read-only launcher content renders");
                timer->stop(); application.exit(0); return;
            }
            ++state->phase;
        });
        timer->start();
    }
    return application.exec();
}
