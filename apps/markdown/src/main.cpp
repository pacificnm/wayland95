#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringDecoder>
#include <QTextDocument>
#include <QTextStream>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include "crashhandler.h"
#include "markdowndocument.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95markdown [file]
//        win95markdown --read FILE        (FILE in Reading View)
//        win95markdown --reader FILE      (the Markdown Reader: FILE only to
//                                          read, its links followed; a manual)
//        win95markdown --roundtrip FILE   (read FILE and print it as this
//                                          program would save it; for tests)
int main(int argc, char *argv[])
{
    installCrashHandler("win95markdown");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95markdown"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95markdown"));

    const QStringList args = app.arguments();
    if (args.size() > 2 && args.at(1) == QLatin1String("--roundtrip")) {
        QFile f(args.at(2));
        if (!f.open(QIODevice::ReadOnly))
            return 1;
        QTextDocument doc;
        doc.setMarkdown(QString::fromUtf8(f.readAll()), QTextDocument::MarkdownDialectGitHub);
        QTextStream(stdout) << MarkdownDocument::toMarkdown(&doc);
        return 0;
    }
    const bool reader = args.size() > 2 && args.at(1) == QLatin1String("--reader");
    const bool reading = reader || (args.size() > 2 && args.at(1) == QLatin1String("--read"));
    const QString file = reading ? args.at(2) : args.size() > 1 ? args.at(1) : QString();
    const QString initialFile = file.isEmpty() ? QString() : QFileInfo(file).absoluteFilePath();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.rootContext()->setContextProperty(QStringLiteral("initialFile"), initialFile);
    engine.rootContext()->setContextProperty(QStringLiteral("initialReading"), reading);
    engine.rootContext()->setContextProperty(QStringLiteral("initialReader"), reader);
    // W95_MARKDOWN_DEBUG: what it did, on stderr (for the tests)
    engine.rootContext()->setContextProperty(QStringLiteral("markdownDebug"), qEnvironmentVariableIsSet("W95_MARKDOWN_DEBUG"));
    // W95_MARKDOWN_NO_OPEN: links are only logged, not opened (no browser on a test desktop)
    engine.rootContext()->setContextProperty(QStringLiteral("markdownNoOpen"), qEnvironmentVariableIsSet("W95_MARKDOWN_NO_OPEN"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Markdown/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
