// win95shell: Wayland 95's desktop (taskbar, Start menu, desktop icons...)
// as a client of wayland95-compositor. See docs/architecture.md.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include <cstdio>

#include "crashhandler.h"
#include "desktopentry.h"
#include "launcher.h"
#include "programlook.h"
#include "notifications.h"
#include "statusnotifier.h"
#include <QElapsedTimer>
#include "desktopkeys.h"

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
    // --desktop-session: this is the login session's desktop (the compositor
    // starts it so when it's the session; by hand, or nested in another
    // desktop, it isn't): autostart, NetworkManager's passwords and the
    // update check (see Launcher). Not --session: Qt takes that, and the
    // argument after it, for X11 session management.
    // --restarted (below): the compositor started us again
    const bool session = app.arguments().contains(QStringLiteral("--desktop-session"));
    Launcher::setSession(session);
    // (the power manager, a library singleton, reads it from here: the
    // power policy, lid and power keys are the session desktop's)
    app.setProperty("wayland95Session", session);

    // which display is the main one, before the panels are made (they'd
    // be made on the first and moved): the compositor says right after the
    // shell's protocol is bound; without it (another compositor), the first
    {
        ShellExtension *extension = ShellExtension::instance();
        QElapsedTimer waited;
        waited.start();
        while (extension->primaryOutput().isEmpty() && waited.elapsed() < 400) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (waited.elapsed() > 150 && !extension->isActive())
                break; // (no such protocol here)
        }
    }

    // --restarted: the compositor started us again after we went away;
    // the session's programs are running already
    const bool restarted = app.arguments().contains(QStringLiteral("--restarted"));

    // at login, "Starting Wayland 95..." while the desktop loads (it takes a
    // few seconds, and the screen would be empty meanwhile): shown and drawn
    // first, then taken away once the desktop has drawn itself (Splash.qml)
    // (--splash: show it anyway, to try it out)
    QQmlApplicationEngine *splashEngine = nullptr;
    QQuickWindow *splash = nullptr;
    if ((session && !restarted) || app.arguments().contains(QStringLiteral("--splash"))) {
        splashEngine = new QQmlApplicationEngine(&app);
        splashEngine->addImportPath(QStringLiteral("qrc:/qt/qml"));
        splashEngine->load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Shell/Splash.qml")));
        if (!splashEngine->rootObjects().isEmpty())
            splash = qobject_cast<QQuickWindow *>(splashEngine->rootObjects().constFirst());
        if (splash) {
            bool drawn = false;
            QObject::connect(splash, &QQuickWindow::frameSwapped, &app, [&drawn] { drawn = true; },
                             Qt::SingleShotConnection);
            QElapsedTimer waited;
            waited.start();
            while (!drawn && waited.elapsed() < 1000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            std::fprintf(stderr, "win95shell: splash %s after %lld ms\n", drawn ? "shown" : "not drawn yet",
                         static_cast<long long>(waited.elapsed()));
        }
    }

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
    std::fprintf(stderr, "win95shell: ready%s%s\n", session ? " (the session's desktop)" : "",
                 restarted ? " (started again)" : "");
    // the splash goes once the desktop has drawn itself (or after a while,
    // whatever happens)
    if (splash) {
        QObject::connect(splash, SIGNAL(finished()), splashEngine, SLOT(deleteLater()));
        QObject::connect(splashEngine, &QObject::destroyed, &app,
                         [] { std::fprintf(stderr, "win95shell: splash gone\n"); });
        const auto finish = [splash] { QMetaObject::invokeMethod(splash, "finish"); };
        auto *desktop = qvariant_cast<QQuickWindow *>(engine.rootObjects().constFirst()->property("desktop"));
        if (desktop) {
            QObject::connect(desktop, &QQuickWindow::frameSwapped, splash,
                             [splash, finish] { QTimer::singleShot(150, splash, finish); }, Qt::SingleShotConnection);
            QTimer::singleShot(10000, splash, finish);
        } else {
            finish();
        }
    }
    // as the session, once the desktop is up: GTK's theme and fonts and the
    // browsers' title bars, then tell D-Bus and systemd where the display
    // is, and start the autostart programs
    // (other programs' look first: the autostart programs may be browsers)
    ProgramLook look;
    if (session) {
        QTimer::singleShot(2000, &app, [&look, restarted] {
            look.startSession();
            if (!restarted)
                Launcher::startSession();
        });
    }
    return app.exec();
}
