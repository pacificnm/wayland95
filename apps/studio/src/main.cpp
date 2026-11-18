#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)
Q_IMPORT_QML_PLUGIN(Win95AppPlugin)

// usage: win95studio [PROJECT.w95proj]
int main(int argc, char *argv[])
{
    installCrashHandler("win95studio");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95studio"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95studio"));

    const QStringList args = app.arguments();
    const QString initialProject = args.size() > 1 ? QFileInfo(args.at(1)).absoluteFilePath() : QString();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.rootContext()->setContextProperty(QStringLiteral("initialProject"), initialProject);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Studio/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
