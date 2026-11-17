#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include "crashhandler.h"
#include "repohub.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95repos
int main(int argc, char *argv[])
{
    installCrashHandler("win95repos");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95repos"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95repos"));

    RepoHub hub;
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.rootContext()->setContextProperty(QStringLiteral("Hub"), &hub);
    // W95_REPOS_DEBUG: what it listed and did, on stderr (for the tests)
    engine.rootContext()->setContextProperty(QStringLiteral("reposDebug"), qEnvironmentVariableIsSet("W95_REPOS_DEBUG"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Repos/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
