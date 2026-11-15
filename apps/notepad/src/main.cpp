#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95notepad [file]
int main(int argc, char *argv[])
{
    // a problem report if it crashes (~/.local/state/wayland95/crashes)
    installCrashHandler("win95notepad");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95notepad"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95notepad"));

    const QStringList args = app.arguments();
    const QString initialFile = args.size() > 1 ? QFileInfo(args.at(1)).absoluteFilePath() : QString();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.rootContext()->setContextProperty(QStringLiteral("initialFile"), initialFile);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Notepad/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
