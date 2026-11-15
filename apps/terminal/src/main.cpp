#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95terminal"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95terminal"));

    // win95terminal [--workdir DIR] [-e PROGRAM [ARGS...]]
    QString workdir;
    QStringList args = app.arguments();
    QStringList command;
    const int e = args.indexOf(QStringLiteral("-e"));
    if (e != -1) {
        command = args.mid(e + 1);
        args = args.mid(0, e);
    }
    const int idx = args.indexOf(QStringLiteral("--workdir"));
    if (idx != -1 && idx + 1 < args.size())
        workdir = args.at(idx + 1);

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.rootContext()->setContextProperty(QStringLiteral("initialWorkingDirectory"), workdir);
    engine.rootContext()->setContextProperty(QStringLiteral("initialCommand"), command);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Terminal/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
