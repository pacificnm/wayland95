#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95email
int main(int argc, char *argv[])
{
    installCrashHandler("win95email");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95email"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95email"));

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    // W95_EMAIL_DEBUG: what it showed and did, on stderr (for the tests)
    engine.rootContext()->setContextProperty(QStringLiteral("emailDebug"), qEnvironmentVariableIsSet("W95_EMAIL_DEBUG"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Email/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
