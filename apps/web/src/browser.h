#pragma once

#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QQmlEngine>

// Web Browser's own things around Qt WebEngine (Chromium): favorites,
// history, settings (home page, search engine), what the address bar makes
// of what's typed, and one browser at a time (a second `win95web URL`
// opens the URL here and goes; two processes can't share a profile).
//
// Files: ~/.config/wayland95/web.json (settings, favorites) and
// ~/.local/share/wayland95/web-history.json (history); the web's own data
// (cookies, cache) is WebEngine's, under ~/.local/share/win95-wayland.
//
// Exposed to QML as the singleton `Browser`.
class Browser : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Browser)
    QML_SINGLETON

    Q_PROPERTY(QString homePage READ homePage WRITE setHomePage NOTIFY settingsChanged)
    Q_PROPERTY(QString searchEngine READ searchEngine WRITE setSearchEngine NOTIFY settingsChanged)
    // [{ key, name }]
    Q_PROPERTY(QVariantList searchEngines READ searchEngines CONSTANT)
    // [{ title, url }]
    Q_PROPERTY(QVariantList favorites READ favorites NOTIFY favoritesChanged)
    // [{ title, url, time (ms) }], newest first
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QString downloadsDir READ downloadsDir CONSTANT)
    // the browser window used last: where another `win95web URL` opens
    Q_PROPERTY(QObject *lastWindow READ lastWindow WRITE setLastWindow NOTIFY lastWindowChanged)

public:
    // (one, shared by main.cpp and QML: its constructor is private, so QML
    // uses create() rather than making another)
    static Browser *instance();
    static Browser *create(QQmlEngine *, QJSEngine *);
    // the first browser: listen for later ones' URLs. False: another is
    // running, and has been given `urls` (this one should go)
    bool becomeTheBrowser(const QStringList &urls);

    QString homePage() const { return m_settings.value(QStringLiteral("homePage")).toString(); }
    void setHomePage(const QString &url);
    QString searchEngine() const { return m_settings.value(QStringLiteral("searchEngine")).toString(); }
    void setSearchEngine(const QString &key);
    QVariantList searchEngines() const;
    QVariantList favorites() const { return m_favorites; }
    QVariantList history() const { return m_history; }
    QString downloadsDir() const;
    QObject *lastWindow() const { return m_lastWindow; }
    void setLastWindow(QObject *w);

    // a new browser window, with these pages (none: the home page)
    Q_INVOKABLE void newWindow(const QStringList &urls) { emit windowWanted(urls); }

    // what's typed in the address bar: a web address as it is, a name like
    // "example.com" made https://, anything else a search
    Q_INVOKABLE QString urlFor(const QString &typed) const;
    Q_INVOKABLE QString searchUrl(const QString &words) const;
    // what to offer while typing: [{ title, url }] from favorites and history
    Q_INVOKABLE QVariantList suggestions(const QString &typed) const;

    Q_INVOKABLE void addFavorite(const QString &title, const QString &url);
    Q_INVOKABLE void removeFavorite(int index);
    Q_INVOKABLE void renameFavorite(int index, const QString &title);
    Q_INVOKABLE bool isFavorite(const QString &url) const;
    Q_INVOKABLE void visited(const QString &url, const QString &title);
    Q_INVOKABLE void clearHistory();
    // remove the cookies in the web's store at `storagePath` the next time
    // the browser starts (they're in use until then)
    Q_INVOKABLE void forgetCookies(const QString &storagePath);

    Q_INVOKABLE bool openFile(const QString &path) const;
    Q_INVOKABLE bool showInFolder(const QString &path) const;
    // the desktop's default browser (xdg-settings)
    Q_INVOKABLE bool isDefault() const;
    Q_INVOKABLE bool makeDefault() const;
    Q_INVOKABLE QString fileName(const QString &path) const;
    Q_INVOKABLE QString homePath() const;
    // a web page's notification (one it was allowed to send), on the desktop
    Q_INVOKABLE void notify(const QString &title, const QString &message, const QUrl &origin) const;

signals:
    void settingsChanged();
    void favoritesChanged();
    void historyChanged();
    // another `win95web URL...` was started: open these here
    void openRequested(const QStringList &urls);
    void lastWindowChanged();
    // (main.cpp makes it)
    void windowWanted(const QStringList &urls);

private:
    explicit Browser(QObject *parent = nullptr);
    void load();
    void saveSettings();
    void saveHistory();

    QVariantMap m_settings;
    QVariantList m_favorites;
    QVariantList m_history;
    QLocalServer *m_server = nullptr;
    QPointer<QObject> m_lastWindow;
};
