#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>
#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95disks [DEVICE]          the window, with DEVICE (/dev/sdb) chosen
//        win95disks --smart DEVICE    and its S.M.A.R.T. report open
//   WIN95_DISKS_LOOP=1: loop devices (udisksctl loop-setup -f FILE) are
//   listed too, to try it out without a spare disk
//   W95_DISKS_SMART_DUMP=file.json: smartctl's report as it came, kept
//   W95_DISKS_GRAB=file.png: the window (and the S.M.A.R.T. window, as
//   file-smart.png, with --smart) saved a moment after it is up (for tests)
int main(int argc, char *argv[])
{
    // a problem report if it crashes (~/.local/state/wayland95/crashes)
    installCrashHandler("win95disks");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95disks"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95disks"));

    QStringList args = app.arguments().mid(1);
    const bool smart = args.removeAll(QStringLiteral("--smart")) > 0;
    const QString device = args.value(0);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("initialDevice"), device);
    engine.rootContext()->setContextProperty(QStringLiteral("initialSmart"), smart && !device.isEmpty());
    engine.rootContext()->setContextProperty(QStringLiteral("testGrab"), qEnvironmentVariable("W95_DISKS_GRAB"));
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Disks/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
