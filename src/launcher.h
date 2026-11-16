#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

// Starts client programs connected to *this* compositor rather than to
// whatever desktop we might be nested inside: the child gets
// WAYLAND_DISPLAY pointed at our socket and is told to prefer Wayland, and
// DISPLAY pointed at our own Xwayland for X11-only programs.
//
// Programs are looked up next to the win95compositor binary first (so the
// bundled apps in the build directory are found), then on $PATH.
//
// Exposed to QML as the singleton `Launcher`.
class Launcher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // the last command that Run... started, to prefill the dialog
    Q_PROPERTY(QString lastCommand READ lastCommand NOTIFY lastCommandChanged)

public:
    explicit Launcher(QObject *parent = nullptr);

    // Set once from main() before the QML is loaded.
    static void setSocketName(const QString &name);
    // ":N" while our Xwayland runs (see XWayland), empty otherwise
    static void setX11Display(const QString &display);

    // As the session (WAYLAND95_SESSION): tells D-Bus and systemd where
    // the display is, so programs they start for us (portals, services)
    // show up here, then runs the programs in ~/.config/autostart and
    // /etc/xdg/autostart meant for any desktop or for Wayland 95.
    static void startSession();

    // Is this the login session's desktop, on the screen itself? The Qt
    // compositor is when started as the session (WAYLAND95_SESSION) on
    // eglfs/linuxfb; win95shell is when started with --session (by the
    // session, never by hand or by a desktop nested in another). Only then
    // do autostart, NetworkManager's passwords and the update check run.
    static bool isSession();
    static void setSession(bool session);

    Q_INVOKABLE bool launch(const QString &program, const QStringList &arguments = {});

    // Start > Run...: opens a folder (in the file manager), a document or
    // URL (with xdg-open), or runs a command line. Returns an empty string
    // on success, otherwise a message for the user.
    Q_INVOKABLE QString run(const QString &commandLine);

    QString lastCommand() const { return m_lastCommand; }

signals:
    void lastCommandChanged();

private:
    static QString s_socketName;
    static QString s_x11Display;
    static bool s_session;
    QString m_lastCommand;
};
