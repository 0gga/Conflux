#include "ApplicationController.h"
#include "DesktopSession.h"
#include "platform/WindowFrame.h"
#include "verification/DesktopSmokeTest.h"
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(Conflux_StylePlugin)

int main(int argc, char *argv[]) {
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
    studio::DesktopSmokeTest verification(application.arguments());
    if (verification.enabled() && verification.storagePath().isEmpty())
        return 2;
    if (verification.enabled())
        qputenv("CONFLUX_STYLE_SETTINGS_DIR", (verification.storagePath() + "/appearance").toUtf8());
    studio::ApplicationController controller(
        nullptr, verification.enabled() ? verification.storagePath() + "/packs" : QString{});
    studio::DesktopSession session(
        nullptr, verification.enabled() ? verification.storagePath() + "/settings.ini" : QString{},
        verification.enabled() ? verification.storagePath() + "/backups" : QString{});
    QObject::connect(&controller, &studio::ApplicationController::stateChanged, &session,
                     [&controller, &session] { session.setPack(controller.activePack()); });
    QObject::connect(&controller, &studio::ApplicationController::curseforgeKeyConfigured, &session,
                     &studio::DesktopSession::setCurseforgeKey);
    QObject::connect(session.content(), &studio::ContentActions::contentChanged, &controller,
                     &studio::ApplicationController::refreshActivePack);
    QObject::connect(
        session.content(), &studio::ContentActions::operationsChanged, &controller,
        [&controller, &session] { controller.setContentBusy(session.content()->operationsBusy()); });
    QObject::connect(session.content(), &studio::ContentActions::operationFailed, &controller,
                     &studio::ApplicationController::reportContentError);
    QObject::connect(session.browse(), &studio::BrowseInstalls::operationFailed, session.content(),
                     &studio::ContentActions::recordExternalFailure);
    QObject::connect(session.content(), &studio::ContentActions::copyRequested, &application,
                     [](const QString &text) { QGuiApplication::clipboard()->setText(text); });
    QQmlApplicationEngine engine;
    QVariantMap initial{{"app", QVariant::fromValue(&controller)},
                        {"session", QVariant::fromValue(&session)}};
    bool screenSpecified = false;
    const auto screenIndex = qEnvironmentVariableIntValue("CONFLUX_WINDOW_SCREEN", &screenSpecified);
    if (!verification.enabled() && screenSpecified && screenIndex >= 0 &&
        screenIndex < application.screens().size()) {
        const auto area = application.screens().at(screenIndex)->availableGeometry();
        initial["x"] = area.x() + 16;
        initial["y"] = area.y() + 16;
        initial["width"] = qMin(1360, area.width() - 32);
        initial["height"] = qMin(900, area.height() - 32);
        if (qEnvironmentVariableIntValue("CONFLUX_START_MINIMIZED") == 1)
            initial["visibility"] = QWindow::Minimized;
    }
    engine.setInitialProperties(initial);
    engine.load(QUrl("qrc:/qt/qml/ModpackStudio/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    studio::WindowFrame frame(*qobject_cast<QWindow *>(engine.rootObjects().first()));
    session.attachWindow(*engine.rootObjects().first());
    QObject::connect(&controller, &studio::ApplicationController::stateChanged, &session,
                     [&controller, &session] {
                         session.running()->setPacks(controller.installations() + controller.packs());
                         session.commands()->setLibraryPacks(controller.libraryPacks());
                     });
    controller.refreshLibrary();
    if (verification.enabled())
        verification.start(application, qobject_cast<QQuickWindow *>(engine.rootObjects().first()),
                           controller, session);
    return application.exec();
}
