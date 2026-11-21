// Network Manager (win95netmanager): logs in to the web pages of the
// network's devices (routers, switches, access points, printers, storage,
// cameras...), with Chromium (Qt WebEngine). Their details are saved as
// devices; their passwords go in the keyring. Control Panel > Network
// Manager opens it.
//
// usage: win95netmanager [DEVICE-NAME]   that device, connected
//   W95_NETMANAGER_GRAB=file.png: the window saved a moment after it is up
//   (W95_NETMANAGER_DIALOG=new|edit opens the device dialog first; for tests)
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

int main(int argc, char *argv[])
{
    installCrashHandler("win95netmanager");
    QtWebEngineQuick::initialize(); // before the application object exists
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95netmanager"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95netmanager"));

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("initialDevice"), app.arguments().value(1));
    engine.rootContext()->setContextProperty(QStringLiteral("testDialog"), qEnvironmentVariable("W95_NETMANAGER_DIALOG"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95NetManager/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    // the window as it is on the screen (web page and all: grabToImage leaves out what Chromium draws over)
    if (const QString grab = qEnvironmentVariable("W95_NETMANAGER_GRAB"); !grab.isEmpty()) {
        if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
            QTimer::singleShot(4000, w, [w, grab] {
                if (w->grabWindow().save(grab))
                    qInfo("grabbed %s", qPrintable(grab));
            });
        }
    }
    return app.exec();
}
