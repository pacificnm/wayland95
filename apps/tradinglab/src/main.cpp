// Trading Lab (win95tradinglab): github.com/pacificnm/trading-labs.app, a paper-trading learning tool with charts,
// market data and a Claude assistant, as a Wayland 95 program. The Electron main process is ported to C++
// (src/core, same database and behaviour); the React screens are redrawn with Win95Ui.
//
// usage: win95tradinglab
//   TRADING_DATA_DIR=dir: use a different data folder (tests), as upstream
//   W95_TRADINGLAB_GRAB=file.png: the window saved a moment after it is up (for tests);
//     W95_TRADINGLAB_VIEW=chart|quote|...: opens that screen first; W95_TRADINGLAB_SYMBOL=NVDA: the symbol;
//     W95_TRADINGLAB_TEST=name: a screen sets up that test state (see the testScript checks in the screens)
#include <QDir>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

#include "backend.h"
#include "core.h"
#include "core/schema.h"
#include "core/stores.h"
#include "core/portfoliostore.h"
#include "crashhandler.h"
#include "keystore.h"
#include "windowgrabber.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

namespace
{
QString socketName()
{
    return QStringLiteral("win95tradinglab-") + qEnvironmentVariable("USER");
}
}

int main(int argc, char *argv[])
{
    installCrashHandler("win95tradinglab");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95tradinglab"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95tradinglab"));

    // one at a time (one database, one order book): a second start brings the first forward
    {
        QLocalSocket other;
        other.connectToServer(socketName());
        if (other.waitForConnected(500)) {
            other.write("\n");
            other.flush();
            other.waitForBytesWritten(1000);
            return 0;
        }
    }
    QLocalServer::removeServer(socketName());
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(socketName());

    // one data folder, as upstream: ~/.local/share/win95-wayland/win95tradinglab (TRADING_DATA_DIR for tests)
    const QString dataDir = qEnvironmentVariableIsEmpty("TRADING_DATA_DIR")
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : qEnvironmentVariable("TRADING_DATA_DIR");
    QDir().mkpath(dataDir);
    QString dbError;
    try {
        tl::initDatabase(QDir(dataDir).filePath(QStringLiteral("trading.db")));
        tl::watchlists::init();
        tl::PortfolioStore::initSchema();
    } catch (const tl::Error &e) {
        dbError = e.message();
    }

    // made before the engine, so they outlive it (bindings still read them while the window goes away)
    std::unique_ptr<KeyStore> keysPtr;
    std::unique_ptr<Backend> backendPtr;
    Core core;
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("testGrab"), qEnvironmentVariable("W95_TRADINGLAB_GRAB"));
    ctx->setContextProperty(QStringLiteral("testView"), qEnvironmentVariable("W95_TRADINGLAB_VIEW"));
    ctx->setContextProperty(QStringLiteral("testSymbol"), qEnvironmentVariable("W95_TRADINGLAB_SYMBOL"));
    ctx->setContextProperty(QStringLiteral("testScript"), qEnvironmentVariable("W95_TRADINGLAB_TEST"));
    WindowGrabber grabber;
    ctx->setContextProperty(QStringLiteral("windowGrabber"), &grabber);
    if (!dbError.isEmpty()) {
        ctx->setContextProperty(QStringLiteral("databaseFailure"),
                                QStringLiteral("Trading Lab cannot open its database, so it will not start.\n\n%1\n\nThe data folder is %2.").arg(dbError, dataDir));
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95TradingLab/DatabaseError.qml")));
        return engine.rootObjects().isEmpty() ? 1 : app.exec();
    }

    keysPtr = std::make_unique<KeyStore>();
    backendPtr = std::make_unique<Backend>(keysPtr.get(), dataDir);
    KeyStore &keys = *keysPtr;
    Backend &backend = *backendPtr;
    ctx->setContextProperty(QStringLiteral("api"), &backend);
    ctx->setContextProperty(QStringLiteral("core"), &core);
    ctx->setContextProperty(QStringLiteral("keyStore"), &keys);
    QObject::connect(&backend, &Backend::restartRequested, &app, [&app] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
        app.quit();
    });

    // the keys come from the keyring first, so the market data and the plan check start with them
    QObject::connect(&keys, &KeyStore::ready, &backend, [&backend, &engine] {
        backend.start();
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95TradingLab/Main.qml")));
        if (engine.rootObjects().isEmpty())
            QCoreApplication::exit(1);
    });
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&server, &engine] {
        while (QLocalSocket *s = server.nextPendingConnection()) {
            s->deleteLater();
            if (!engine.rootObjects().isEmpty())
                if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
                    w->show();
                    w->raise();
                    w->requestActivate();
                }
        }
    });
    keys.load();
    return app.exec();
}
