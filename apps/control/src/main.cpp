#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include "desktopentry.h"
#include "software.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95control [--applet accessibility|bluetooth|datetime|display|fonts|hardware|keyboard|mouse|network|passwords|power|printers|programs|regional|services|sounds|system|taskbar [--tab N] [--check]]
// With --applet, only that applet's window opens (Start > Settings >
// Taskbar... uses this), optionally on tab N (0-based), like Win95's
// "control desk.cpl,,2"; otherwise the Control Panel folder opens.
int main(int argc, char *argv[])
{
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
    // --check: look for updates straight away (Start > Wayland 95 Update)
    engine.rootContext()->setContextProperty(QStringLiteral("initialCheck"), args.contains(QStringLiteral("--check")));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Control/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
