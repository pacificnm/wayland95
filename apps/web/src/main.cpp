#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include "browser.h"
#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95web [url...]
int main(int argc, char *argv[])
{
    installCrashHandler("win95web");
    // Qt WebEngine (Chromium) shares OpenGL with Qt Quick: set before the
    // application exists (what QtWebEngineQuick::initialize() does)
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95web"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95web"));

    QStringList urls = app.arguments().mid(1);
    // one browser at a time: a second one gives its pages to the first
    // (two can't share the web's data: cookies, cache)
    if (!Browser::instance()->becomeTheBrowser(urls))
        return 0;

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("webDebug"), qEnvironmentVariableIsSet("W95_WEB_DEBUG"));
    auto openWindow = [&engine](const QStringList &list) {
        engine.setInitialProperties({{QStringLiteral("urls"), list}});
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Web/Main.qml")));
    };
    QObject::connect(Browser::instance(), &Browser::windowWanted, &app, openWindow);
    openWindow(urls);
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
