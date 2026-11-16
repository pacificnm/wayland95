#include "session.h"
#include "launcher.h"

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
