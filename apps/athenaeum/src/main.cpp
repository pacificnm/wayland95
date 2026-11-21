// Athenaeum (win95athenaeum): KAthenaeum, the offline Wikipedia library with
// a local AI research assistant, as a Wayland 95 program. The library, the
// assistant and the database are KAthenaeum's own C++ (src/core and the
// objects QML talks to); KDE Frameworks are replaced by plain Qt and Win95Ui.
//
// usage: win95athenaeum [zim://... | file.zim]
//   W95_ATHENAEUM_GRAB=file.png: the window saved a moment after it is up
//     (for tests); W95_ATHENAEUM_PAGE=search|ask|notes|... opens that page
//     first, and W95_ATHENAEUM_QUERY a search or question on it;
//     W95_ATHENAEUM_TEST=notes|project|reading|catalog|classroom walks
//     through that feature and grabs each step (TestDriver.qml)
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickWebEngineProfile>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QtQml/qqmlextensionplugin.h>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include <KLocalizedString>

#include "appstate.h"
#include "articlestore.h"
#include "connectivity.h"
#include "core/catalog.h"
#include "core/database.h"
#include "core/extract.h"
#include "crashhandler.h"
#include "i18ncontext.h"
#include "windowgrabber.h"
#include "controlpanel.h"
#include "librarymanager.h"
#include "modelsettings.h"
#include "projectlist.h"
#include "zimschemehandler.h"

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

namespace
{
// The QML engine's network access: catalog illustrations from kiwix.org (that address shape only, see
// Catalog::isIllustrationUrl) and data: URLs, nothing else. Any text a QML item renders as rich text or Markdown (a ZIM
// title, a snippet, a model's answer) could otherwise make the app fetch an image from anywhere.
class KiwixOnlyAccess : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override
    {
        if (op == GetOperation && (Catalog::isIllustrationUrl(request.url()) || request.url().scheme() == QLatin1String("data")))
            return QNetworkAccessManager::createRequest(op, request, data);
        qWarning("Refused a network request from the interface: %s", qUtf8Printable(request.url().toDisplayString(QUrl::RemoveQuery)));
        return QNetworkAccessManager::createRequest(GetOperation, QNetworkRequest(QUrl(QStringLiteral("refused:"))), nullptr);
    }
};

// catalog illustrations are kept on disk, so the catalog shows them again offline
class CachingNetworkFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *nam = new KiwixOnlyAccess(parent);
        auto *cache = new QNetworkDiskCache(nam);
        cache->setCacheDirectory(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("qml-network")));
        cache->setMaximumCacheSize(50 << 20);
        nam->setCache(cache);
        return nam;
    }
};

QString socketName()
{
    return QStringLiteral("win95athenaeum-") + qEnvironmentVariable("USER");
}
}

int main(int argc, char *argv[])
{
    installCrashHandler("win95athenaeum");
    initXmlParser(); // once, on the main thread, before any worker parses HTML
    ZimSchemeHandler::registerScheme(); // before any web engine profile exists
    QtWebEngineQuick::initialize(); // before the application object exists
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95athenaeum"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));
    app.setDesktopFileName(QStringLiteral("win95athenaeum"));

    const QString opening = app.arguments().value(1);

    // one at a time (one database): a second start gives what it was asked to open to the first
    {
        QLocalSocket other;
        other.connectToServer(socketName());
        if (other.waitForConnected(500)) {
            other.write((opening.isEmpty() ? QStringLiteral("\n") : opening + QLatin1Char('\n')).toUtf8());
            other.flush();
            other.waitForBytesWritten(1000);
            return 0;
        }
    }
    QLocalServer::removeServer(socketName()); // (a crashed one's)
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(socketName());

    QString dbError;
    const QString dbPath = Database::defaultPath();
    const bool dbOpen = Database::open(dbPath, &dbError);
    AppState::instance()->setDatabase(dbPath, dbError);
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    I18nContext i18nContext;
    engine.rootContext()->setContextObject(&i18nContext); // i18n() in QML, as KDE's context gave it
    engine.rootContext()->setContextProperty(QStringLiteral("testGrab"), qEnvironmentVariable("W95_ATHENAEUM_GRAB"));
    engine.rootContext()->setContextProperty(QStringLiteral("testPage"), qEnvironmentVariable("W95_ATHENAEUM_PAGE"));
    engine.rootContext()->setContextProperty(QStringLiteral("testQuery"), qEnvironmentVariable("W95_ATHENAEUM_QUERY"));
    engine.rootContext()->setContextProperty(QStringLiteral("testScript"), qEnvironmentVariable("W95_ATHENAEUM_TEST"));
    ControlPanel controlPanel;
    engine.rootContext()->setContextProperty(QStringLiteral("controlPanel"), &controlPanel);
    WindowGrabber windowGrabber;
    engine.rootContext()->setContextProperty(QStringLiteral("windowGrabber"), &windowGrabber);
    engine.rootContext()->setContextProperty(QStringLiteral("openingArgument"), opening);
    if (!dbOpen) {
        // without its database the app would lose every chat, note and library: say so and stop
        engine.rootContext()->setContextProperty(QStringLiteral("databaseFailure"),
                                                 i18n("Athenaeum cannot open its database, so it will not start.\n\n%1\n\nThe database is %2. "
                                                      "Nothing in it has been changed.",
                                                      dbError,
                                                      dbPath));
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Athenaeum/DatabaseError.qml")));
        return engine.rootObjects().isEmpty() ? 1 : app.exec();
    }

    Connectivity::createInstance(&app); // before anything that may use the internet
    LibraryManager *libraries = LibraryManager::createInstance(&app);
    ModelSettings::createInstance(&app)->refresh(); // is Ollama there, with the models? (one quick local request)
    // an article caches its rendered page with its saved excerpts marked: a project change may change those
    QObject::connect(ProjectsNotifier::instance(), &ProjectsNotifier::changed, &app, [] {
        ArticleStore::clear();
    });
    libraries->initialize();

    // articles come from zim:// only: nothing the reader shows needs a disk cache or cookies
    QQuickWebEngineProfile *profile = QQuickWebEngineProfile::defaultProfile();
    profile->setHttpCacheType(QQuickWebEngineProfile::MemoryHttpCache);
    profile->setPersistentCookiesPolicy(QQuickWebEngineProfile::NoPersistentCookies);
    profile->installUrlSchemeHandler("zim", new ZimSchemeHandler(&app));

    CachingNetworkFactory networkFactory; // the engine uses it until it is destroyed
    engine.setNetworkAccessManagerFactory(&networkFactory);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Win95Athenaeum/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // a second start: come forward, and open what it was given
    QObject *root = engine.rootObjects().constFirst();
    QObject::connect(&server, &QLocalServer::newConnection, root, [&server, root] {
        while (QLocalSocket *s = server.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, s, [s, root] {
                const QString what = QString::fromUtf8(s->readAll()).trimmed();
                QMetaObject::invokeMethod(root, "openExternal", Q_ARG(QVariant, what));
                s->deleteLater();
            });
        }
    });
    return app.exec();
}
