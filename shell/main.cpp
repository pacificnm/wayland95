// win95shell: Wayland 95's desktop (taskbar, Start menu, desktop icons...)
// as a client of wayland95-compositor. See docs/architecture.md.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include <cstdio>

#include "crashhandler.h"
#include "desktopentry.h"
#include "launcher.h"
#include "notifications.h"
#include "statusnotifier.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

int main(int argc, char *argv[])
{
    // a problem report if it crashes (~/.local/state/wayland95/crashes)
    installCrashHandler("win95shell");
    // panels are a Wayland thing: no other platform
    qputenv("QT_QPA_PLATFORM", "wayland");
    // Qt puts every window at the screen's corner, since a Wayland window
    // can't know where it is; panels do know (layershell.cpp works it out
    // from their anchors), and items are mapped between them by it
    qputenv("QT_WAYLAND_DISABLE_FIXED_POSITIONS", "1");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wayland95"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    // the shell's windows go away with the compositor; nothing to ask
    app.setQuitOnLastWindowClosed(false);

    const QString socket = qEnvironmentVariable("WAYLAND_DISPLAY");
    std::fprintf(stderr, "win95shell: on WAYLAND_DISPLAY=%s\n", qPrintable(socket));
    // programs started from the Start menu go to the same compositor, and
    // X11 ones to its Xwayland (the compositor gives the shell DISPLAY;
    // without it the launcher drops DISPLAY, rather than send X11 programs
    // to some other desktop)
    Launcher::setSocketName(socket);
    Launcher::setX11Display(qEnvironmentVariable("DISPLAY"));
    // --session: this is the login session's desktop (its session starts
    // it so; by hand, or nested in another desktop, it isn't): autostart,
    // NetworkManager's passwords and the update check (see Launcher)
    const bool session = app.arguments().contains(QStringLiteral("--session"));
    Launcher::setSession(session);
    // (the power manager, a library singleton, reads it from here: the
    // power policy, lid and power keys are the session desktop's)
    app.setProperty("wayland95Session", session);

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.addImageProvider(QStringLiteral("trayicon"), new TrayIconProvider);
    engine.addImageProvider(QStringLiteral("appicon"), new AppIconProvider);
    engine.addImageProvider(QStringLiteral("notification"), new NotificationImageProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Shell/Shell.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    std::fprintf(stderr, "win95shell: ready%s\n", session ? " (the session's desktop)" : "");
    // as the session: tell D-Bus and systemd where the display is, then
    // start the autostart programs, once the desktop is up
    if (session)
        QTimer::singleShot(2000, &app, &Launcher::startSession);
    return app.exec();
}
