#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include "crashhandler.h"
#include "photoeditor.h"
#include "photofiles.h"
#include "photoservices.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95photos [folder | photo]
//   a folder opens in the gallery (default: ~/Pictures), a photo opens
//   full-window, with the rest of its folder a click away
//   W95_PHOTOS_DEBUG=1: what it did, on stderr (for the tests)
//   W95_PHOTOS_SCRIPT="show:PATH;edit:rotateRight;crop:X,Y,W,H;...":
//   actions run one after another once it's up (for the tests; Main.qml's
//   runStep lists them)
int main(int argc, char *argv[])
{
    installCrashHandler("win95photos");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95photos"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95photos"));

    // Photo services register here, e.g.
    //   PhotoServices::registerService(new MyCloudPhotos);
    // (see photoservices.h; none come with Photo Gallery yet)

    QString folder, photo;
    const QStringList args = app.arguments().mid(1);
    if (!args.isEmpty()) {
        const QFileInfo info(args.first());
        if (info.isDir()) {
            folder = info.absoluteFilePath();
        } else if (info.isFile()) {
            photo = info.absoluteFilePath();
            folder = info.absolutePath();
        }
    }

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.addImageProvider(QStringLiteral("thumb"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("view"), new ViewProvider);
    engine.addImageProvider(QStringLiteral("photo"), new EditorImageProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("initialFolder"), folder);
    engine.rootContext()->setContextProperty(QStringLiteral("initialPhoto"), photo);
    engine.rootContext()->setContextProperty(QStringLiteral("photosDebug"), qEnvironmentVariableIsSet("W95_PHOTOS_DEBUG"));
    engine.rootContext()->setContextProperty(QStringLiteral("photosScript"), qEnvironmentVariable("W95_PHOTOS_SCRIPT"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Photos/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
