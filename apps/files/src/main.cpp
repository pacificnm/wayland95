#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95files [--details] [folder | computer: | trash:]
int main(int argc, char *argv[])
{
    // a problem report if it crashes (~/.local/state/wayland95/crashes)
    installCrashHandler("win95files");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95files"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95files"));

    QStringList args = app.arguments().mid(1);
    const bool details = args.removeAll(QStringLiteral("--details")) > 0;
    const QString initialPath = !args.isEmpty() ? args.first() : QDir::homePath();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.rootContext()->setContextProperty(QStringLiteral("initialPath"), initialPath);
    engine.rootContext()->setContextProperty(QStringLiteral("initialViewMode"),
                                             details ? QStringLiteral("details") : QStringLiteral("icons"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Files/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
