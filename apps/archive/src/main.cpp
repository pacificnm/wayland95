#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>

#include "crashhandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage:
//   win95archive [ARCHIVE]                      the window, with ARCHIVE open
//   win95archive --extract-here ARCHIVE...      each into the folder it's in
//   win95archive --extract-subfolders ARCHIVE...  each into a folder named after it, beside it
//   win95archive --extract-to DIR ARCHIVE...    all into DIR
//   win95archive --add ARCHIVE FILE...          FILEs into ARCHIVE (made if it isn't
//                                               there; its format from its name)
//   win95archive --add-dialog FILE...           asks where and how, then adds
// The --options show just a progress box (the file manager's right-click
// Extract Here / Add to ... run them), and exit when done.
//   W95_ARCHIVE_DEBUG=1: what it did, on stderr (for the tests)
//   W95_ARCHIVE_PASSWORD=pw: the answer to the first password question,
//   W95_ARCHIVE_OVERWRITE=0-4: to every Confirm File Replace (Yes, Yes to
//   All, No, No to All, Cancel) - for the tests
//   W95_ARCHIVE_GRAB=file.png: the window, saved a moment after it's up
int main(int argc, char *argv[])
{
    installCrashHandler("win95archive");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95archive"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95archive"));

    QStringList args = app.arguments().mid(1);
    QString mode, dest, archive;
    QStringList archives, files;
    auto absolute = [](const QStringList &list) {
        QStringList out;
        for (const QString &p : list)
            out << QFileInfo(p).absoluteFilePath();
        return out;
    };
    auto usage = []() {
        std::fputs("usage: win95archive [ARCHIVE]\n"
                   "       win95archive --extract-here ARCHIVE...\n"
                   "       win95archive --extract-subfolders ARCHIVE...\n"
                   "       win95archive --extract-to DIR ARCHIVE...\n"
                   "       win95archive --add ARCHIVE FILE...\n"
                   "       win95archive --add-dialog FILE...\n", stderr);
        return 2;
    };
    const QString first = args.value(0);
    if (first == QLatin1String("--extract-here") || first == QLatin1String("--extract-subfolders")) {
        mode = QStringLiteral("extract");
        dest = first == QLatin1String("--extract-here") ? QStringLiteral(":here") : QStringLiteral(":subfolder");
        archives = absolute(args.mid(1));
    } else if (first == QLatin1String("--extract-to")) {
        if (args.size() < 3)
            return usage();
        mode = QStringLiteral("extract");
        dest = QFileInfo(args.at(1)).absoluteFilePath();
        archives = absolute(args.mid(2));
    } else if (first == QLatin1String("--add")) {
        if (args.size() < 3)
            return usage();
        mode = QStringLiteral("add");
        archive = QFileInfo(args.at(1)).absoluteFilePath();
        files = absolute(args.mid(2));
    } else if (first == QLatin1String("--add-dialog")) {
        mode = QStringLiteral("addDialog");
        files = absolute(args.mid(1));
    } else if (first == QLatin1String("--help") || first == QLatin1String("-h")) {
        return usage();
    } else if (!first.isEmpty()) {
        archive = QFileInfo(first).absoluteFilePath();
    }
    if ((mode == QLatin1String("extract") && archives.isEmpty()) || (mode == QLatin1String("addDialog") && files.isEmpty()))
        return usage();
    // the progress box: its own app (the compositor remembers windows' places
    // by app; the box's mustn't become the main window's)
    if (!mode.isEmpty())
        app.setDesktopFileName(QStringLiteral("win95archive-progress"));

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("cliMode"), mode);
    ctx->setContextProperty(QStringLiteral("cliDest"), dest);
    ctx->setContextProperty(QStringLiteral("cliArchives"), archives);
    ctx->setContextProperty(QStringLiteral("cliFiles"), files);
    ctx->setContextProperty(QStringLiteral("initialArchive"), archive);
    ctx->setContextProperty(QStringLiteral("archiveDebug"), qEnvironmentVariableIsSet("W95_ARCHIVE_DEBUG"));
    ctx->setContextProperty(QStringLiteral("testGrab"), qEnvironmentVariable("W95_ARCHIVE_GRAB"));
    ctx->setContextProperty(QStringLiteral("testPassword"), qEnvironmentVariable("W95_ARCHIVE_PASSWORD"));
    ctx->setContextProperty(QStringLiteral("testOverwrite"),
                            qEnvironmentVariableIsSet("W95_ARCHIVE_OVERWRITE") ? qEnvironmentVariableIntValue("W95_ARCHIVE_OVERWRITE") : -1);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Archive/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
