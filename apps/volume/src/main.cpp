#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95volume [--record]
// --record opens the recording (microphone) controls instead of playback.
int main(int argc, char *argv[])
{
    // a problem report if it crashes (~/.local/state/wayland95/crashes)
    installCrashHandler("win95volume");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95volume"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95volume"));

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.rootContext()->setContextProperty(QStringLiteral("startRecording"),
                                             app.arguments().contains(QStringLiteral("--record")));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Volume/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
