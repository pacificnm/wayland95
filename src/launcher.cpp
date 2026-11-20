#include "launcher.h"
#include "wayland95settings.h"
#include "desktopentry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QtDebug>

QString Launcher::s_socketName;
QString Launcher::s_x11Display;

Launcher::Launcher(QObject *parent)
    : QObject(parent)
{
}

void Launcher::setSocketName(const QString &name)
{
    s_socketName = name;
}

bool Launcher::s_session = false;

void Launcher::setSession(bool session)
{
    s_session = session;
}

bool Launcher::isSession()
{
    if (s_session)
        return true;
    // the Qt compositor: started as the session and on the screen itself,
    // not a window inside another desktop (a test copy started from a
    // terminal in the session inherits WAYLAND95_SESSION, and mustn't act
    // as it)
    const QString platform = QGuiApplication::platformName();
    return qEnvironmentVariableIsSet("WAYLAND95_SESSION")
           && (platform == QLatin1String("eglfs") || platform == QLatin1String("linuxfb"));
}

namespace {
// systemd's user manager and D-Bus activation: programs they start get
// these. Never from the automated tests: their desktops run inside your
// session, whose systemd this would change (tests/run.sh sets
// WAYLAND95_NO_SYSTEM_AGENTS)
void exportEnvironment(const QStringList &assignments)
{
    if (!Launcher::isSession())
        return;
    if (qEnvironmentVariableIsSet("WAYLAND95_NO_SYSTEM_AGENTS")) {
        qInfo().noquote() << "Session: would tell systemd and D-Bus" << assignments.join(QLatin1Char(' '))
                          << "(WAYLAND95_NO_SYSTEM_AGENTS)";
        return;
    }
    QProcess::startDetached(QStringLiteral("dbus-update-activation-environment"),
                            QStringList{ QStringLiteral("--systemd") } + assignments);
}

// the session's fonts and GTK scroll bars (wayland95-session), for programs
// systemd and D-Bus start too
QStringList sessionLook()
{
    QStringList list;
    for (const char *name : { "FONTCONFIG_FILE", "GTK_OVERLAY_SCROLLING" }) {
        if (qEnvironmentVariableIsSet(name))
            list << QLatin1String(name) + QLatin1Char('=') + qEnvironmentVariable(name);
    }
    return list;
}
}

void Launcher::setX11Display(const QString &display)
{
    s_x11Display = display;
    if (!display.isEmpty())
        exportEnvironment({ QStringLiteral("DISPLAY=") + display });
}

void Launcher::startSession()
{
    if (!isSession())
        return;
    exportEnvironment(QStringList{ QStringLiteral("WAYLAND_DISPLAY=") + s_socketName,
                        QStringLiteral("XDG_CURRENT_DESKTOP=Wayland95"),
                        QStringLiteral("XDG_SESSION_DESKTOP=wayland95"),
                        QStringLiteral("XDG_SESSION_TYPE=wayland") }
                      + sessionLook());

    // XDG autostart: the user's own entries override the system's by name
    const QString config = qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config"));
    QStringList dirs{ config + QStringLiteral("/autostart") };
    for (const QString &d : qEnvironmentVariable("XDG_CONFIG_DIRS", QStringLiteral("/etc/xdg")).split(QLatin1Char(':'), Qt::SkipEmptyParts))
        dirs << d + QStringLiteral("/autostart");
    // other desktops' plumbing that doesn't say it's only for them
    // (OnlyShowIn), and only works there:
    //  - KDE's Xwayland Video Bridge shares screens to X11 programs through
    //    KWin; elsewhere it's just an empty window
    QSet<QString> seen{ QStringLiteral("org.kde.xwaylandvideobridge.desktop") };
    Launcher launcher;
    for (const QString &dir : dirs) {
        for (const QFileInfo &f : QDir(dir).entryInfoList({ QStringLiteral("*.desktop") }, QDir::Files)) {
            if (seen.contains(f.fileName()))
                continue;
            seen.insert(f.fileName());
            const DesktopEntry e = DesktopEntry::read(f.absoluteFilePath());
            // autostart entries are often NoDisplay (not for menus); Hidden
            // (or GNOME's X-GNOME-Autostart-enabled=false) means "turned off"
            if (!e.application || e.hidden || e.autostartOff || e.exec.isEmpty())
                continue;
            if (!e.onlyShowIn.isEmpty() && !e.onlyShowIn.contains(QLatin1String("Wayland95")))
                continue;
            if (e.notShowIn.contains(QLatin1String("Wayland95")))
                continue;
            if (!e.tryExec.isEmpty() && QStandardPaths::findExecutable(e.tryExec).isEmpty() && !QFileInfo(e.tryExec).isExecutable())
                continue;
            QStringList command = e.command();
            if (command.isEmpty())
                continue;
            qInfo().noquote() << "Autostart:" << f.fileName();
            const QString program = command.takeFirst();
            launcher.launch(program, command);
        }
    }
}

QString Launcher::resolve(const QString &program)
{
    if (program.contains(QLatin1Char('/')))
        return program;
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(program);
    const QFileInfo info(bundled);
    return (info.isFile() && info.isExecutable()) ? bundled : QStandardPaths::findExecutable(program);
}

bool Launcher::launch(const QString &program, const QStringList &arguments)
{
    const QString path = resolve(program);
    if (path.isEmpty()) {
        qWarning() << "Launcher: program not found:" << program;
        return false;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!s_socketName.isEmpty())
        env.insert(QStringLiteral("WAYLAND_DISPLAY"), s_socketName);
    // Toolkits that can do both should still pick Wayland, falling back to X11.
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland;xcb"));
    env.insert(QStringLiteral("GDK_BACKEND"), QStringLiteral("wayland,x11"));
    // X11 programs go to our Xwayland. Without one, DISPLAY must go
    // entirely: otherwise, when nested, they'd silently open on the host
    // desktop instead.
    if (!s_x11Display.isEmpty())
        env.insert(QStringLiteral("DISPLAY"), s_x11Display);
    else
        env.remove(QStringLiteral("DISPLAY"));
    // Control Panel > Regional Settings
    static Wayland95Settings *settings = new Wayland95Settings(qApp);
    const QVariantMap locale = settings->localeEnvironment();
    for (auto it = locale.cbegin(); it != locale.cend(); ++it)
        env.insert(it.key(), it.value().toString());
    if (!locale.isEmpty())
        env.remove(QStringLiteral("LC_ALL")); // it would override them all

    QProcess process;
    process.setProgram(path);
    process.setArguments(arguments);
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(QDir::homePath());
    process.setStandardInputFile(QProcess::nullDevice());

    qint64 pid = 0;
    if (!process.startDetached(&pid)) {
        qWarning() << "Launcher: failed to start" << path << ":" << process.errorString();
        return false;
    }
    qInfo() << "Launcher: started" << path << "pid" << pid;
    return true;
}

#ifndef WAYLAND95_RUN_AS_ADMIN_HELPER
#define WAYLAND95_RUN_AS_ADMIN_HELPER "/usr/libexec/wayland95/wayland95-run-as-admin"
#endif

bool Launcher::launchAsRoot(const QString &program, const QStringList &arguments)
{
    const QString path = resolve(program);
    if (path.isEmpty()) {
        qWarning() << "Launcher: program not found:" << program;
        return false;
    }
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty()) {
        qWarning() << "Launcher: can't run" << path << "as root: pkexec isn't installed";
        return false;
    }
    QString helper = QStringLiteral(WAYLAND95_RUN_AS_ADMIN_HELPER);
#ifdef WAYLAND95_RUN_AS_ADMIN_HELPER_SRC
    if (!QFileInfo::exists(helper))
        helper = QStringLiteral(WAYLAND95_RUN_AS_ADMIN_HELPER_SRC);
#endif
    if (!QFileInfo(helper).isExecutable()) {
        qWarning() << "Launcher: can't run" << path << "as root: no" << helper;
        return false;
    }

    // root has no runtime dir of ours to find the socket in: its full path
    QString wayland = s_socketName.isEmpty() ? qEnvironmentVariable("WAYLAND_DISPLAY") : s_socketName;
    if (!wayland.startsWith(QLatin1Char('/')))
        wayland = qEnvironmentVariable("XDG_RUNTIME_DIR") + QLatin1Char('/') + wayland;
    // (as root, still the user's own X server; none means none)
    const QString x11 = s_x11Display;
    const QString xauth = x11.isEmpty() ? QString() : qEnvironmentVariable("XAUTHORITY");

    return launch(pkexec, QStringList{ helper, wayland, x11, xauth, path } + arguments);
}

bool Launcher::launchEntry(const DesktopEntry &entry, bool asRoot)
{
    QStringList command = entry.command();
    if (command.isEmpty())
        return false;
    if (entry.terminal) {
        if (asRoot)
            command.prepend(QStringLiteral("pkexec"));
        return launch(QStringLiteral("win95terminal"), QStringList{ QStringLiteral("-e") } + command);
    }
    const QString program = command.takeFirst();
    return asRoot ? launchAsRoot(program, command) : launch(program, command);
}

QString Launcher::run(const QString &commandLine)
{
    const QString command = commandLine.trimmed();
    if (command.isEmpty())
        return {};

    QString path = command;
    if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/")))
        path = QDir::homePath() + path.mid(1);
    const QFileInfo info(path);

    bool ok = false;
    if (info.exists() && info.isDir()) {
        ok = launch(QStringLiteral("win95files"), {info.absoluteFilePath()});
    } else if (info.exists() && info.isFile()) {
        ok = info.isExecutable() ? launch(info.absoluteFilePath())
                                 : launch(QStringLiteral("xdg-open"), {info.absoluteFilePath()});
    } else if (command.contains(QLatin1String("://"))) {
        ok = launch(QStringLiteral("xdg-open"), {command});
    } else {
        QStringList args = QProcess::splitCommand(command);
        if (args.isEmpty())
            return QStringLiteral("Cannot run '%1'.").arg(command);
        const QString program = args.takeFirst();
        ok = launch(program, args);
        if (!ok)
            return QStringLiteral("Cannot find the file '%1' (or one of its components). Make sure the "
                                  "path and filename are correct and that all required libraries are available.")
                .arg(program);
    }
    if (!ok)
        return QStringLiteral("Cannot open '%1'.").arg(command);

    if (m_lastCommand != command) {
        m_lastCommand = command;
        emit lastCommandChanged();
    }
    return {};
}
