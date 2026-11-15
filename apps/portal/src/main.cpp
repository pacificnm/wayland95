#include <QDBusConnection>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include "filechooser.h"
#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// win95portal: Wayland 95's xdg-desktop-portal backend. D-Bus starts it
// when a program asks the portal for a file dialog (see
// session/wayland95.portal and wayland95-portals.conf), and it quits again
// after a while with nothing to do.
int main(int argc, char *argv[])
{
    // a problem report if it crashes (~/.local/state/wayland95/crashes)
    installCrashHandler("win95portal");
    // started by D-Bus, maybe without Qt's platform chosen
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY") && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "wayland");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95portal"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95portal"));
    app.setQuitOnLastWindowClosed(false);

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));

    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *chooser = new FileChooserPortal(&engine, &app);
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), chooser, QDBusConnection::ExportAllSlots)
        || !bus.registerService(QStringLiteral("org.freedesktop.impl.portal.desktop.wayland95"))) {
        qWarning("win95portal: another one is already running");
        return 1;
    }

    // nothing to do for five minutes: quit (D-Bus starts it again)
    QTimer idle;
    idle.setSingleShot(true);
    idle.setInterval(5 * 60 * 1000);
    QObject::connect(&idle, &QTimer::timeout, &app, [&] { if (chooser->idle()) app.quit(); else idle.start(); });
    QObject::connect(chooser, &FileChooserPortal::idleChanged, &idle, [&] { if (chooser->idle()) idle.start(); else idle.stop(); });
    idle.start();

    return app.exec();
}
