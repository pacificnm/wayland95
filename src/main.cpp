#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QTimer>
#include <QDir>
#include <QWaylandCompositor>
#include <QWaylandOutput>
#include <QWindow>

#include "desktopentry.h"
#include "launcher.h"
#include "statusnotifier.h"
#include "notifications.h"

#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Win95UiPlugin)

#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include "crashhandler.h"

// Pick the socket name our compositor will listen on.
//
// If WaylandCompositor.socketName is left empty, Qt falls back to
// $WAYLAND_DISPLAY - which, when running nested inside an existing Wayland
// desktop, is the *host's* socket (usually "wayland-0"), so creating it
// fails with "unable to lock lockfile ... maybe another compositor is
// running". Instead we probe wayland-0, wayland-1, ... and take the first
// one whose lock file isn't held, the same way libwayland's
// wl_display_add_socket_auto() does.
//
// An explicit `--wayland-socket-name <name>` on the command line wins.
static QString chooseSocketName(const QStringList &args)
{
    const int idx = args.indexOf(QStringLiteral("--wayland-socket-name"));
    if (idx != -1 && idx + 1 < args.size())
        return args.at(idx + 1);

    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtimeDir.isEmpty())
        return QStringLiteral("wayland-0"); // Qt will report the real error

    const QString hostDisplay = qEnvironmentVariable("WAYLAND_DISPLAY");
    for (int i = 0; i < 32; ++i) {
        const QString name = QStringLiteral("wayland-%1").arg(i);
        if (name == hostDisplay)
            continue;

        const QByteArray lockPath =
            QDir(runtimeDir).filePath(name + QStringLiteral(".lock")).toLocal8Bit();
        const int fd = ::open(lockPath.constData(), O_CREAT | O_RDWR | O_CLOEXEC, 0660);
        if (fd < 0)
            continue;
        const bool free = ::flock(fd, LOCK_EX | LOCK_NB) == 0;
        ::close(fd); // releases the lock; Qt/libwayland re-takes it right after
        if (free)
            return name;
    }
    return QString(); // let Qt fall back and report the failure
}

// Keeps the output's advertised mode equal to the window's real size in
// device pixels. Qt's own sizeFollowsWindow computes it once with the
// screen's integer scale and doesn't update when the window's (possibly
// fractional) scale arrives, so clients - X11 ones especially, which size
// their root window from it - were told the screen was much bigger than
// what's visible.
class OutputModeFollower : public QObject
{
public:
    OutputModeFollower(QWaylandOutput *output, QWindow *window)
        : QObject(window), m_output(output), m_window(window)
    {
        output->setSizeFollowsWindow(false);
        window->installEventFilter(this);
        update();
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::DevicePixelRatioChange
            || event->type() == QEvent::Expose)
            update();
        return false;
    }

private:
    void update()
    {
        const QSize size = (QSizeF(m_window->size()) * m_window->devicePixelRatio()).toSize();
        if (size.isEmpty() || size == m_output->currentMode().size())
            return;
        const QWaylandOutputMode mode(size, 60000);
        m_output->addMode(mode, true);
        m_output->setCurrentMode(mode);
    }

    QWaylandOutput *m_output;
    QWindow *m_window;
};

int main(int argc, char *argv[])
{
    // a problem report if it crashes (~/.local/state/wayland95/crashes)
    installCrashHandler("win95compositor");
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wayland95"));
    app.setOrganizationName(QStringLiteral("win95-wayland"));

    const QString socketName = chooseSocketName(app.arguments());
    std::fprintf(stderr, "win95compositor: clients connect with WAYLAND_DISPLAY=%s\n",
                 qPrintable(socketName));

    Launcher::setSocketName(socketName);

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml")); // Qt 6.4 doesn't search it by default
    // other programs' tray icons (see statusnotifier.h)
    engine.addImageProvider(QStringLiteral("trayicon"), new TrayIconProvider);
    engine.addImageProvider(QStringLiteral("appicon"), new AppIconProvider);
    // pictures in notifications (see notifications.h)
    engine.addImageProvider(QStringLiteral("notification"), new NotificationImageProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("compositorSocketName"),
                                             socketName);
    // X11 program support (Xwayland) is on unless --no-xwayland is given
    engine.rootContext()->setContextProperty(QStringLiteral("xwaylandEnabled"),
                                             !app.arguments().contains(QStringLiteral("--no-xwayland")));

    // Qt 6.4 predates QQmlApplicationEngine::loadFromModule() (added in 6.5),
    // so we load by the module's generated resource path instead. The path
    // is derived from the module URI ("Win95Shell") and the RESOURCE_PREFIX
    // set in CMakeLists.txt ("/qt/qml").
    const QUrl url(QStringLiteral("qrc:/qt/qml/Win95Shell/Main.qml"));
    engine.load(url);

    if (engine.rootObjects().isEmpty())
        return -1;

    if (auto *compositor = qobject_cast<QWaylandCompositor *>(engine.rootObjects().constFirst())) {
        QWaylandOutput *output = compositor->defaultOutput();
        if (output && output->window())
            new OutputModeFollower(output, output->window());
    }

    // as the login session: publish the display, then autostart programs
    // (once the desktop is up and Xwayland had a moment to start)
    QTimer::singleShot(2000, &app, &Launcher::startSession);

    return app.exec();
}
