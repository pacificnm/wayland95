#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include "launcher.h"

// Start > Programs: every installed program (from the .desktop files in
// the usual places, Flatpak's included), sorted into Win95-style folders by
// category. Updates by itself when programs are installed or removed.
//
// Exposed to QML as the singleton `ProgramMenu`.
class ProgramMenu : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // [{ label, items: [{ label, iconSource, desktopId }] }], folders in
    // alphabetical order, programs in each too
    Q_PROPERTY(QVariantList folders READ folders NOTIFY changed)

public:
    explicit ProgramMenu(QObject *parent = nullptr);

    QVariantList folders() const { return m_folders; }

    // runs a program by its desktop file ID (in a terminal if it asks for one)
    Q_INVOKABLE bool launch(const QString &desktopId);
    // the same as root (Launcher::launchAsRoot, launchEntry)
    Q_INVOKABLE bool launchAsRoot(const QString &desktopId);
    // its .desktop file, "" if there's no such program
    Q_INVOKABLE QString pathOf(const QString &desktopId) const { return m_paths.value(desktopId); }
    // a window's icon from its program's ID (Wayland app ID or X11 class):
    // its menu entry's icon, else a theme icon by that name; "" if none
    Q_INVOKABLE QString iconFor(const QString &appId) const;

signals:
    void changed();

private:
    bool start(const QString &desktopId, bool asRoot);
    void reload();
    void watch();

    QVariantList m_folders;
    QHash<QString, QString> m_paths; // desktop ID -> file
    QFileSystemWatcher m_watcher;
    QTimer m_reload;
    Launcher m_launcher;
};
