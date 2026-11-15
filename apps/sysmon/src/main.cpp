#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95sysmon
int main(int argc, char *argv[])
{
    // a problem report if it crashes (~/.local/state/wayland95/crashes)
    installCrashHandler("win95sysmon");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95sysmon"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95sysmon"));

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95SysMon/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
