#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include "crashhandler.h"
#include "filesearch.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95files [--details] [--select NAME] [folder | computer: | trash:]
//        win95files --find [folder]     Find: All Files, looking in folder
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
    const bool find = args.removeAll(QStringLiteral("--find")) > 0;
    QString select;
    const int at = args.indexOf(QStringLiteral("--select"));
    if (at >= 0 && at + 1 < args.size()) {
        select = args.at(at + 1);
        args.remove(at, 2);
    }
    const QString initialPath = !args.isEmpty() ? args.first() : QDir::homePath();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.rootContext()->setContextProperty(QStringLiteral("initialPath"), initialPath);
    engine.rootContext()->setContextProperty(QStringLiteral("initialSelect"), select);
    if (find) {
        // Find: All Files, its own window (and program, as in Win95)
        app.setDesktopFileName(QStringLiteral("win95find"));
        qmlRegisterUncreatableType<FileSearch>("Win95Files", 1, 0, "FileSearch", QStringLiteral("the window's own"));
        auto *search = new FileSearch(&app);
        engine.rootContext()->setContextProperty(QStringLiteral("search"), search);
        engine.rootContext()->setContextProperty(QStringLiteral("initialLookIn"),
                                                 args.isEmpty() ? QString() : QDir(args.first()).absolutePath());
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Files/FindWindow.qml")));
        if (engine.rootObjects().isEmpty())
            return -1;
        const int status = app.exec();
        search->stop();
        return status;
    }
    engine.rootContext()->setContextProperty(QStringLiteral("initialViewMode"),
                                             details ? QStringLiteral("details") : QStringLiteral("icons"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Files/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
