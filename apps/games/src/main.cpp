#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include "crashhandler.h"
#include "gamestore.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

// usage: win95solitaire | win95mines  [--seed N]
// --seed (or W95_GAMES_SEED) deals the same game every time: for the tests.
int main(int argc, char *argv[])
{
    installCrashHandler(GAME_NAME);
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral(GAME_NAME));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral(GAME_NAME));

    qint64 seed = qEnvironmentVariableIsSet("W95_GAMES_SEED") ? qEnvironmentVariableIntValue("W95_GAMES_SEED") : -1;
    const QStringList args = app.arguments();
    const int at = args.indexOf(QStringLiteral("--seed"));
    if (at > 0 && at + 1 < args.size())
        seed = args.at(at + 1).toLongLong();

    GameStore store(QStringLiteral(GAME_NAME));
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    engine.rootContext()->setContextProperty(QStringLiteral("gameSeed"), double(seed));
    // W95_GAMES_DEBUG: log presses and moves (where the tests learn what to click)
    engine.rootContext()->setContextProperty(QStringLiteral("gameDebug"), qEnvironmentVariableIsSet("W95_GAMES_DEBUG"));
    engine.rootContext()->setContextProperty(QStringLiteral("GameStore"), &store);
    engine.load(QUrl(QStringLiteral(GAME_QML)));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
