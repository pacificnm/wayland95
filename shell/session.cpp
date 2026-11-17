#include "session.h"
#include "launcher.h"
#include "desktopkeys.h"

#include <QCoreApplication>
#include <csignal>
#include <cstdio>
#include <sys/types.h>

void ShellSession::exitDesktop()
{
    bool ok = false;
    const qint64 pid = qEnvironmentVariable("WAYLAND95_COMPOSITOR_PID").toLongLong(&ok);
    if (ok && pid > 1 && ::kill(pid_t(pid), SIGTERM) == 0) {
        std::fprintf(stderr, "win95shell: asked the compositor (%lld) to stop\n", pid);
        return; // the shell goes when its compositor does
    }
    // not started by wayland95-compositor (by hand, while testing)
    std::fprintf(stderr, "win95shell: no compositor to stop (WAYLAND95_COMPOSITOR_PID); the shell exits\n");
    QCoreApplication::quit();
}

bool ShellSession::session() const
{
    return Launcher::isSession();
}

bool ShellSession::restarted() const
{
    return QCoreApplication::arguments().contains(QStringLiteral("--restarted"));
}

ShellSession::ShellSession(QObject *parent) : QObject(parent)
{
    connect(ShellExtension::instance(), &ShellExtension::primaryOutputChanged, this, &ShellSession::primaryOutputChanged);
}

QString ShellSession::primaryOutput() const
{
    return ShellExtension::instance()->primaryOutput();
}

QObject *ShellSession::mainScreen(const QJSValue &screens, const QString &name) const
{
    QObject *first = nullptr;
    const int n = screens.property(QStringLiteral("length")).toInt();
    for (int i = 0; i < n; ++i) {
        QObject *screen = screens.property(quint32(i)).toQObject();
        if (!screen)
            continue;
        if (!first)
            first = screen;
        if (!name.isEmpty() && screen->property("name").toString() == name)
            return screen;
    }
    return first;
}
