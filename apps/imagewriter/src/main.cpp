#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95imagewriter [IMAGE]
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95imagewriter"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95imagewriter"));

    QString image;
    const QStringList args = app.arguments().mid(1);
    if (!args.isEmpty())
        image = QFileInfo(args.constFirst()).absoluteFilePath();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("initialImage"), image);
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95ImageWriter/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
