#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include "launcher.h"

// Shortcuts on the desktop: the programs' .desktop files in the user's
// Desktop folder (~/Desktop, or wherever xdg-user-dirs puts it), as other
// desktops keep them. Start menu > (right-click a program) > Add Shortcut
// to Desktop makes one; the desktop shows them under its own icons and
// updates by itself when the folder changes.
//
// Exposed to QML as the singleton `DesktopShortcuts`.
class DesktopShortcuts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // [{ label, iconSource, icon, path, key, modified }], by label:
    // iconSource is the program's own icon (an image URL, or ""), icon the
    // drawn one of a Wayland 95 program (MenuIcon's kind, or ""); key names
    // it in the layout ("shortcut:" and its file name), modified is the
    // file's time (ms since 1970, for Arrange Icons > by Date)
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY changed)
    // Where the desktop's icons are (desktop right-click > Arrange Icons;
    // dragging them), kept in ~/.config/wayland95/desktop-icons.json:
    //   { autoArrange: bool,
    //     order: [key...],                 the icons' order, for Auto Arrange
    //     positions: { key: { x, y } } }   where each one was put, in pixels
    //                                      from the desktop's top left
    // Keys: "computer", "network", "bin", "terminal", "shortcut:FILE".
    Q_PROPERTY(QVariantMap layout READ layout NOTIFY layoutChanged)

public:
    explicit DesktopShortcuts(QObject *parent = nullptr);

    QVariantList shortcuts() const { return m_shortcuts; }
    QVariantMap layout() const { return m_layout; }
    Q_INVOKABLE void saveLayout(const QVariantMap &layout);
    // reads the Desktop folder again (desktop right-click > Refresh)
    Q_INVOKABLE void refresh() { reload(); }

    // a shortcut to an installed program, from its .desktop file; one to
    // the same program that's already there is kept instead of doubled.
    // Returns "" or what went wrong, for the user.
    Q_INVOKABLE QString addProgram(const QString &desktopFile);
    // a shortcut to one of Wayland 95's own programs (no .desktop file of
    // its own): its name, command and drawn icon
    Q_INVOKABLE QString addCommand(const QString &label, const QString &program,
                                   const QStringList &arguments, const QString &icon);

    Q_INVOKABLE bool open(const QString &path);
    Q_INVOKABLE bool openAsRoot(const QString &path);
    // Delete: into the Recycle Bin, like any file
    Q_INVOKABLE bool remove(const QString &path);

signals:
    void changed();
    void layoutChanged();

private:
    static QString folder();
    static QString layoutPath();
    void loadLayout();
    bool isShortcut(const QString &path) const;
    QString write(const QString &fileName, const QByteArray &contents);
    void reload();
    void watch();

    QVariantList m_shortcuts;
    QVariantMap m_layout;
    QFileSystemWatcher m_watcher;
    QTimer m_reload;
    Launcher m_launcher;
};
