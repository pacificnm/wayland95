#pragma once

#include <QQuickImageProvider>
#include <QString>
#include <QStringList>

// A program's .desktop file (the freedesktop Desktop Entry spec):
// [Desktop Entry] only, unlocalized keys.
struct DesktopEntry
{
    QString id;          // e.g. "org.gnome.Chess.desktop"
    QString path;
    QString name, genericName, comment, icon, exec, tryExec;
    QStringList categories, keywords, onlyShowIn, notShowIn;
    bool application = false;
    bool terminal = false;
    bool noDisplay = false;
    bool hidden = false;

    static DesktopEntry read(const QString &path, const QString &id = QString());
    // should appear in menus: an application, not hidden, runnable
    bool visible() const;
    // Exec split into program and arguments, field codes (%f, %U...) removed
    QStringList command() const;

    // where .desktop files live, most important first: the user's own,
    // Flatpak's exports, then XDG_DATA_DIRS
    static QStringList applicationDirs();
    // every visible program, by desktop file ID (the user's own win)
    static QList<DesktopEntry> all();
};

// The URL QML should use for an icon name or path from a .desktop file:
// "image://appicon/<name>", "file:///path", or "" when there's no such icon.
QString appIconUrl(const QString &icon);

// image://appicon/<icon name or path>: a program's icon from the icon
// themes (hicolor and friends), /usr/share/pixmaps or Flatpak's exports.
class AppIconProvider : public QQuickImageProvider
{
public:
    AppIconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    // the file for an icon name, or "" (cached)
    static QString find(const QString &name);
};
