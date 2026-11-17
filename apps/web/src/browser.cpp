#include "browser.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

#include <unistd.h>

namespace {

constexpr int kMaxHistory = 5000;

QString configFile()
{
    return qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config")) + QStringLiteral("/wayland95/web.json");
}

QString historyFile()
{
    return qEnvironmentVariable("XDG_DATA_HOME", QDir::homePath() + QStringLiteral("/.local/share")) + QStringLiteral("/wayland95/web-history.json");
}

bool writePrivate(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(data);
    return f.commit();
}

struct Engine { const char *key, *name, *url; };
const Engine kEngines[] = {
    {"duckduckgo", "DuckDuckGo", "https://duckduckgo.com/?q=%s"},
    {"google", "Google", "https://www.google.com/search?q=%s"},
    {"bing", "Bing", "https://www.bing.com/search?q=%s"},
    {"startpage", "Startpage", "https://www.startpage.com/do/search?q=%s"},
    {"wikipedia", "Wikipedia", "https://en.wikipedia.org/w/index.php?search=%s"},
};

// (in the session's runtime folder when there is one: private, and cleared
// at logout; else /tmp, with the user's id in the name)
QString socketName()
{
    const QString display = qEnvironmentVariable("WAYLAND_DISPLAY");
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty() && QFileInfo(runtime).isDir())
        return runtime + QStringLiteral("/win95web-") + display;
    return QStringLiteral("win95web-%1-%2").arg(::getuid()).arg(display);
}

} // namespace

Browser::Browser(QObject *parent)
    : QObject(parent)
{
    load();
}

Browser *Browser::instance()
{
    static Browser *b = new Browser;
    return b;
}

Browser *Browser::create(QQmlEngine *, QJSEngine *)
{
    Browser *b = instance();
    QJSEngine::setObjectOwnership(b, QJSEngine::CppOwnership);
    return b;
}

bool Browser::becomeTheBrowser(const QStringList &urls)
{
    // one already? give it the URLs
    QLocalSocket other;
    other.connectToServer(socketName());
    if (other.waitForConnected(500)) {
        QByteArray msg = urls.join(QLatin1Char('\n')).toUtf8();
        if (msg.isEmpty())
            msg = "\n"; // (a new window, as asked)
        other.write(msg + "\n\n");
        other.flush();
        other.waitForBytesWritten(1000);
        other.disconnectFromServer();
        return false;
    }
    // cookies asked to be forgotten last time (they can't be while the
    // web's own store has them open): before it opens them
    const QString cookies = m_settings.value(QStringLiteral("forgetCookies")).toString();
    if (!cookies.isEmpty()) {
        for (const QString &name : {QStringLiteral("Cookies"), QStringLiteral("Cookies-journal")})
            QFile::remove(cookies + QLatin1Char('/') + name);
        m_settings.remove(QStringLiteral("forgetCookies"));
        saveSettings();
    }
    QLocalServer::removeServer(socketName()); // (a crashed one's)
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    m_server->listen(socketName());
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *s = m_server->nextPendingConnection()) {
            // (all of it, once the other one has gone: it may come in pieces)
            connect(s, &QLocalSocket::readyRead, s, [s] {
                s->setProperty("buf", s->property("buf").toByteArray() + s->readAll());
            });
            connect(s, &QLocalSocket::disconnected, this, [this, s] {
                const QByteArray all = s->property("buf").toByteArray() + s->readAll();
                s->deleteLater();
                QStringList list;
                for (const QByteArray &l : all.split('\n'))
                    if (!l.trimmed().isEmpty())
                        list << QString::fromUtf8(l.trimmed());
                // (an empty line alone: a new window was asked for)
                if (m_lastWindow && !list.isEmpty())
                    emit openRequested(list);
                else
                    emit windowWanted(list);
            });
        }
    });
    return true;
}

void Browser::load()
{
    QFile f(configFile());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_settings = o.toVariantMap();
        m_favorites = o.value(QStringLiteral("favorites")).toArray().toVariantList();
        m_settings.remove(QStringLiteral("favorites"));
    }
    if (!m_settings.contains(QStringLiteral("homePage")))
        m_settings[QStringLiteral("homePage")] = QStringLiteral("https://duckduckgo.com/");
    if (!m_settings.contains(QStringLiteral("searchEngine")))
        m_settings[QStringLiteral("searchEngine")] = QStringLiteral("duckduckgo");
    QFile h(historyFile());
    if (h.open(QIODevice::ReadOnly))
        m_history = QJsonDocument::fromJson(h.readAll()).array().toVariantList();
}

void Browser::saveSettings()
{
    QJsonObject o = QJsonObject::fromVariantMap(m_settings);
    o.insert(QStringLiteral("favorites"), QJsonArray::fromVariantList(m_favorites));
    writePrivate(configFile(), QJsonDocument(o).toJson());
}

void Browser::saveHistory()
{
    writePrivate(historyFile(), QJsonDocument(QJsonArray::fromVariantList(m_history)).toJson(QJsonDocument::Compact));
}

void Browser::setHomePage(const QString &url)
{
    m_settings[QStringLiteral("homePage")] = url;
    saveSettings();
    emit settingsChanged();
}

void Browser::setSearchEngine(const QString &key)
{
    m_settings[QStringLiteral("searchEngine")] = key;
    saveSettings();
    emit settingsChanged();
}

QVariantList Browser::searchEngines() const
{
    QVariantList out;
    for (const Engine &e : kEngines)
        out << QVariantMap{{QStringLiteral("key"), QString::fromLatin1(e.key)}, {QStringLiteral("name"), QString::fromLatin1(e.name)}};
    return out;
}

QString Browser::downloadsDir() const
{
    QString d = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (d.isEmpty())
        d = QDir::homePath() + QStringLiteral("/Downloads");
    QDir().mkpath(d);
    return d;
}

QString Browser::searchUrl(const QString &words) const
{
    QString pattern = m_settings.value(QStringLiteral("searchUrl")).toString(); // (a custom one: the tests')
    if (pattern.isEmpty()) {
        pattern = QString::fromLatin1(kEngines[0].url);
        for (const Engine &e : kEngines)
            if (searchEngine() == QLatin1String(e.key))
                pattern = QString::fromLatin1(e.url);
    }
    return QString(pattern).replace(QStringLiteral("%s"), QString::fromLatin1(QUrl::toPercentEncoding(words.trimmed())));
}

QString Browser::urlFor(const QString &typed) const
{
    const QString t = typed.trimmed();
    if (t.isEmpty())
        return QString();
    // a scheme: as it is (http:, https:, file:, about:, view-source:...)
    static const QRegularExpression scheme(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*:"));
    if (scheme.match(t).hasMatch() && !t.contains(QLatin1Char(' '))) {
        // (but "localhost:8000" is a host and port)
        static const QRegularExpression hostPort(QStringLiteral("^[a-zA-Z0-9.-]+:\\d+(/.*)?$"));
        if (!hostPort.match(t).hasMatch())
            return t;
    }
    if (t.startsWith(QLatin1Char('/')) && QFileInfo::exists(t))
        return QUrl::fromLocalFile(t).toString();
    // a name with a dot and no spaces (example.com, 192.168.1.1:8080/x), or localhost
    static const QRegularExpression host(QStringLiteral("^(localhost|[a-zA-Z0-9-]+(\\.[a-zA-Z0-9-]+)+)(:\\d+)?(/\\S*)?$"));
    if (host.match(t).hasMatch()) {
        const bool local = t.startsWith(QLatin1String("localhost")) || t.startsWith(QLatin1String("127.")) || t.startsWith(QLatin1String("192.168."))
            || t.startsWith(QLatin1String("10."));
        return (local ? QStringLiteral("http://") : QStringLiteral("https://")) + t;
    }
    return searchUrl(t);
}

QVariantList Browser::suggestions(const QString &typed) const
{
    const QString t = typed.trimmed().toLower();
    QVariantList out;
    if (t.size() < 2)
        return out;
    QSet<QString> seen;
    auto consider = [&](const QVariantList &list) {
        for (const QVariant &v : list) {
            const QVariantMap m = v.toMap();
            const QString url = m.value(QStringLiteral("url")).toString();
            if (seen.contains(url))
                continue;
            QString bare = url.toLower();
            bare.remove(QRegularExpression(QStringLiteral("^https?://(www\\.)?")));
            if (bare.startsWith(t) || m.value(QStringLiteral("title")).toString().toLower().contains(t)) {
                seen.insert(url);
                out << QVariantMap{{QStringLiteral("title"), m.value(QStringLiteral("title"))}, {QStringLiteral("url"), url}};
                if (out.size() >= 8)
                    return;
            }
        }
    };
    consider(m_favorites);
    consider(m_history);
    return out;
}

void Browser::addFavorite(const QString &title, const QString &url)
{
    if (url.isEmpty() || isFavorite(url))
        return;
    m_favorites << QVariantMap{{QStringLiteral("title"), title.isEmpty() ? url : title}, {QStringLiteral("url"), url}};
    saveSettings();
    emit favoritesChanged();
}

void Browser::removeFavorite(int index)
{
    if (index < 0 || index >= m_favorites.size())
        return;
    m_favorites.removeAt(index);
    saveSettings();
    emit favoritesChanged();
}

void Browser::renameFavorite(int index, const QString &title)
{
    if (index < 0 || index >= m_favorites.size() || title.trimmed().isEmpty())
        return;
    QVariantMap m = m_favorites.at(index).toMap();
    m[QStringLiteral("title")] = title.trimmed();
    m_favorites[index] = m;
    saveSettings();
    emit favoritesChanged();
}

bool Browser::isFavorite(const QString &url) const
{
    for (const QVariant &v : m_favorites)
        if (v.toMap().value(QStringLiteral("url")).toString() == url)
            return true;
    return false;
}

void Browser::visited(const QString &url, const QString &title)
{
    if (url.isEmpty() || url.startsWith(QLatin1String("about:")) || url.startsWith(QLatin1String("data:"))
        || url.startsWith(QLatin1String("view-source:")))
        return;
    // the same page again: moved to the top
    for (int i = int(m_history.size()) - 1; i >= 0; --i)
        if (m_history.at(i).toMap().value(QStringLiteral("url")).toString() == url)
            m_history.removeAt(i);
    m_history.prepend(QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("url"), url},
                                  {QStringLiteral("time"), QDateTime::currentMSecsSinceEpoch()}});
    while (m_history.size() > kMaxHistory)
        m_history.removeLast();
    saveHistory();
    emit historyChanged();
}

void Browser::setLastWindow(QObject *w)
{
    if (m_lastWindow == w)
        return;
    m_lastWindow = w;
    emit lastWindowChanged();
}

void Browser::forgetCookies(const QString &storagePath)
{
    m_settings[QStringLiteral("forgetCookies")] = storagePath;
    saveSettings();
}

void Browser::clearHistory()
{
    m_history.clear();
    saveHistory();
    emit historyChanged();
}

bool Browser::openFile(const QString &path) const
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool Browser::showInFolder(const QString &path) const
{
    QString files = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("win95files"));
    if (!QFileInfo(files).isExecutable())
        files = QStandardPaths::findExecutable(QStringLiteral("win95files"));
    if (files.isEmpty())
        return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    return QProcess::startDetached(files, {QFileInfo(path).absolutePath(), QStringLiteral("--select"), QFileInfo(path).fileName()});
}

bool Browser::isDefault() const
{
    QProcess p;
    p.start(QStringLiteral("xdg-settings"), {QStringLiteral("get"), QStringLiteral("default-web-browser")});
    if (!p.waitForFinished(3000))
        return false;
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed() == QLatin1String("win95web.desktop");
}

bool Browser::makeDefault() const
{
    return QProcess::execute(QStringLiteral("xdg-settings"), {QStringLiteral("set"), QStringLiteral("default-web-browser"),
                                                              QStringLiteral("win95web.desktop")}) == 0;
}

QString Browser::fileName(const QString &path) const
{
    return QFileInfo(path).fileName();
}

void Browser::notify(const QString &title, const QString &message, const QUrl &origin) const
{
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
                                                    QStringLiteral("/org/freedesktop/Notifications"),
                                                    QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    m << QStringLiteral("Web Browser") << uint(0) << QStringLiteral("web-browser") << title
      << (message + QStringLiteral("\n(%1)").arg(origin.host())) << QStringList() << QVariantMap() << -1;
    QDBusConnection::sessionBus().asyncCall(m);
}

QString Browser::homePath() const
{
    return QDir::homePath();
}
