#include "crashhandler.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QMutex>
#include <QString>
#include <QtGlobal>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef WAYLAND95_VERSION
#define WAYLAND95_VERSION "unknown"
#endif

namespace {

// Everything the signal handler uses is prepared beforehand: it may only
// call async-signal-safe functions (write, open...), so no Qt, no malloc.
char g_dir[512];
char g_path[640];
char g_program[64];

// the last messages logged, for the report
constexpr int RingSize = 40;
constexpr int LineSize = 300;
char g_ring[RingSize][LineSize];
int g_ringNext = 0;
QMutex g_logMutex;

// repeated messages
QByteArray g_last;
int g_repeats = 0;
qint64 g_firstRepeat = 0;

const int CrashSignals[] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL };

void put(int fd, const char *s) { if (s) { ssize_t r = write(fd, s, strlen(s)); (void)r; } }

void putNumber(int fd, unsigned long long n, int base = 10)
{
    char buf[32];
    int i = sizeof buf;
    buf[--i] = 0;
    do {
        const int d = int(n % base);
        buf[--i] = char(d < 10 ? '0' + d : 'a' + d - 10);
        n /= base;
    } while (n && i > 0);
    put(fd, buf + i);
}

const char *signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGABRT: return "SIGABRT (aborted)";
    case SIGBUS: return "SIGBUS (bus error)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGILL: return "SIGILL (illegal instruction)";
    default: return "signal";
    }
}

void onCrash(int sig, siginfo_t *info, void *)
{
    static volatile sig_atomic_t inside = 0;
    if (!inside) {
        inside = 1;
        const int fd = open(g_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd >= 0) {
            put(fd, "Wayland 95 problem report\n");
            put(fd, "Program: "); put(fd, g_program); put(fd, "\n");
            put(fd, "Version: " WAYLAND95_VERSION "\n");
            put(fd, "Time: "); putNumber(fd, (unsigned long long)time(nullptr)); put(fd, "\n");
            put(fd, "PID: "); putNumber(fd, (unsigned long long)getpid()); put(fd, "\n");
            put(fd, "Signal: "); putNumber(fd, (unsigned long long)sig); put(fd, " "); put(fd, signalName(sig)); put(fd, "\n");
            if (info && (sig == SIGSEGV || sig == SIGBUS)) {
                put(fd, "Address: 0x"); putNumber(fd, (unsigned long long)(uintptr_t)info->si_addr, 16); put(fd, "\n");
            }

            put(fd, "\n== Backtrace\n");
            void *frames[64];
            const int n = backtrace(frames, 64);
            backtrace_symbols_fd(frames, n, fd);
            // module + offset: `addr2line -f -C -e MODULE OFFSET` with the
            // debug symbols (wayland95-dbgsym) gives the source line
            put(fd, "\n== Frames (module+offset)\n");
            for (int i = 0; i < n; ++i) {
                Dl_info dl;
                put(fd, "#"); putNumber(fd, (unsigned long long)i); put(fd, " ");
                if (dladdr(frames[i], &dl) && dl.dli_fname) {
                    put(fd, dl.dli_fname);
                    put(fd, "+0x");
                    putNumber(fd, (unsigned long long)((uintptr_t)frames[i] - (uintptr_t)dl.dli_fbase), 16);
                } else {
                    put(fd, "0x");
                    putNumber(fd, (unsigned long long)(uintptr_t)frames[i], 16);
                }
                put(fd, "\n");
            }

            put(fd, "\n== Recent messages\n");
            for (int i = 0; i < RingSize; ++i) {
                const char *line = g_ring[(g_ringNext + i) % RingSize];
                if (line[0]) {
                    put(fd, line);
                    put(fd, "\n");
                }
            }
            close(fd);
        }
        put(STDERR_FILENO, g_program);
        put(STDERR_FILENO, ": crashed with ");
        put(STDERR_FILENO, signalName(sig));
        put(STDERR_FILENO, "; problem report: ");
        put(STDERR_FILENO, g_path);
        put(STDERR_FILENO, "\n");
    }
    // die the way we would have: core dump, exit status
    signal(sig, SIG_DFL);
    raise(sig);
}

void remember(const QByteArray &line)
{
    char *slot = g_ring[g_ringNext];
    const int n = int(qMin<qsizetype>(line.size(), LineSize - 1));
    memcpy(slot, line.constData(), n);
    slot[n] = 0;
    g_ringNext = (g_ringNext + 1) % RingSize;
}

void writeLine(const QByteArray &line)
{
    std::fwrite(line.constData(), 1, line.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

void flushRepeats()
{
    if (g_repeats > 0) {
        writeLine(QByteArray("    (the message above was repeated ") + QByteArray::number(g_repeats)
                  + (g_repeats == 1 ? " more time)" : " more times)"));
        g_repeats = 0;
    }
}

void onMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QByteArray text = qFormatLogMessage(type, context, message).toLocal8Bit();
    const QByteArray stamped = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")).toLatin1() + text;
    {
        QMutexLocker lock(&g_logMutex);
        remember(stamped);
        if (text == g_last && type != QtFatalMsg) {
            // the same again: count it, and say so now and then
            ++g_repeats;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (g_repeats == 1)
                g_firstRepeat = now;
            if (now - g_firstRepeat > 60000) {
                flushRepeats();
                g_firstRepeat = now;
            }
        } else {
            flushRepeats();
            g_last = text;
            writeLine(stamped);
        }
    }
    if (type == QtFatalMsg)
        std::abort(); // the crash handler writes the report
}

} // namespace

const char *crashReportDir() { return g_dir; }

void installCrashHandler(const char *program)
{
    // "win95compositor" is enough; no paths
    std::snprintf(g_program, sizeof g_program, "%s", program);

    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty())
        state = QDir::homePath() + QStringLiteral("/.local/state");
    const QString dir = state + QStringLiteral("/wayland95/crashes");
    QDir().mkpath(dir);
    std::snprintf(g_dir, sizeof g_dir, "%s", dir.toLocal8Bit().constData());
    // one file per run: the program, when it started, and its pid
    std::snprintf(g_path, sizeof g_path, "%s/%s-%lld-%d.crash", g_dir, g_program,
                  (long long)time(nullptr), int(getpid()));

    // backtrace() loads libgcc the first time, which allocates: do it now
    void *warmup[2];
    backtrace(warmup, 2);

    // its own stack, so a stack overflow can still be reported
    static char altStack[64 * 1024];
    stack_t ss {};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof altStack;
    sigaltstack(&ss, nullptr);

    struct sigaction sa {};
    sa.sa_sigaction = onCrash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int sig : CrashSignals)
        sigaction(sig, &sa, nullptr);

    qInstallMessageHandler(onMessage);
}
