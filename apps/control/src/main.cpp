#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include "desktopentry.h"
#include "software.h"
#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95control [--applet accessibility|backup|bluetooth|datetime|display|firewall|fonts|hardware|keyboard|mouse|network|passwords|power|printers|problems|programs|regional|services|sounds|startup|system|systemlog|taskbar [--tab N] [--check] [--uninstall DESKTOP-ID] [--log system|kernel|user] [--find TEXT]]
// With --applet, only that applet's window opens (Start > Settings >
// Taskbar... uses this), optionally on tab N (0-based), like Win95's
// "control desk.cpl,,2"; otherwise the Control Panel folder opens.
int main(int argc, char *argv[])
{
    // a problem report if it crashes (~/.local/state/wayland95/crashes)
    installCrashHandler("win95control");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95control"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95control"));

    QString applet;
    const QStringList args = app.arguments();
    const int idx = args.indexOf(QStringLiteral("--applet"));
    if (idx != -1 && idx + 1 < args.size())
        applet = args.at(idx + 1);
    int tab = 0;
    const int tabIdx = args.indexOf(QStringLiteral("--tab"));
    if (tabIdx != -1 && tabIdx + 1 < args.size())
        tab = args.at(tabIdx + 1).toInt();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.addImageProvider(QStringLiteral("appicon"), new AppIconProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("initialApplet"), applet);
    engine.rootContext()->setContextProperty(QStringLiteral("initialTab"), tab);
    // for the tests: W95_CONTROL_GRAB=DIR saves each property sheet as
    // DIR/TITLE-TAB.png once it is up; W95_CONTROL_SETTINGS=ID opens that
    // service's Settings sheet when Services opens
    engine.rootContext()->setContextProperty(QStringLiteral("testGrabDir"), qEnvironmentVariable("W95_CONTROL_GRAB"));
    engine.rootContext()->setContextProperty(QStringLiteral("testOpenSettings"), qEnvironmentVariable("W95_CONTROL_SETTINGS"));
    // --check: look for updates straight away (Start > Wayland 95 Update)
    engine.rootContext()->setContextProperty(QStringLiteral("initialCheck"), args.contains(QStringLiteral("--check")));
    // --uninstall ID (Start menu > Uninstall...): Add/Remove Programs asks
    // to remove the program with that desktop file ID
    const int uninstallIdx = args.indexOf(QStringLiteral("--uninstall"));
    engine.rootContext()->setContextProperty(QStringLiteral("initialUninstall"),
                                             uninstallIdx != -1 && uninstallIdx + 1 < args.size() ? args.at(uninstallIdx + 1) : QString());
    // --log system|kernel|user --find TEXT: what System Log opens on
    // (Firewall's Show Blocked Connections)
    const int logIdx = args.indexOf(QStringLiteral("--log"));
    engine.rootContext()->setContextProperty(QStringLiteral("initialLog"),
                                             logIdx != -1 && logIdx + 1 < args.size() ? args.at(logIdx + 1) : QString());
    const int findIdx = args.indexOf(QStringLiteral("--find"));
    engine.rootContext()->setContextProperty(QStringLiteral("initialFind"),
                                             findIdx != -1 && findIdx + 1 < args.size() ? args.at(findIdx + 1) : QString());
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Control/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
