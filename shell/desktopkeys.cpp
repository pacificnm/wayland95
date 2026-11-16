#include "desktopkeys.h"

#include <QGuiApplication>
#include <cstdio>

ShellExtension *ShellExtension::instance()
{
    static ShellExtension *extension = [] {
        auto *e = new ShellExtension;
        auto said = [e] {
            if (e->isActive())
                std::fprintf(stderr, "win95shell: the compositor sends the desktop's keys here\n");
        };
        QObject::connect(e, &ShellExtension::activeChanged, e, said);
        said();
        return e;
    }();
    return extension;
}

DesktopKeys::DesktopKeys(QObject *parent) : QObject(parent)
{
    ShellExtension *extension = ShellExtension::instance();
    connect(extension, &ShellExtension::key, this, [this](uint key, bool down) {
        if (down)
            Q_EMIT pressed(int(key));
        else
            Q_EMIT released(int(key));
    });
    connect(extension, &ShellExtension::tone, this, [this](uint t) { Q_EMIT tone(int(t)); });
    connect(extension, &ShellExtension::shortcut, this, [this](uint f) { Q_EMIT shortcut(int(f)); });
    connect(extension, &ShellExtension::stickyModifiers, this, &DesktopKeys::stickyModifiers);
    connect(extension, &ShellExtension::switcherShown, this, &DesktopKeys::switcherShown);
    connect(extension, &ShellExtension::switcherSelected, this, &DesktopKeys::switcherSelected);
    connect(extension, &ShellExtension::switcherHidden, this, &DesktopKeys::switcherHidden);
    connect(extension, &ShellExtension::windowMenu, this, [this](uint w, int x, int y, uint state) {
        Q_EMIT windowMenu(int(w), x, y, int(state));
    });
    connect(extension, &ShellExtension::activeChanged, this, &DesktopKeys::activeChanged);
}

bool DesktopKeys::active() const
{
    return ShellExtension::instance()->isActive();
}

void DesktopKeys::windowAction(int window, int action)
{
    ShellExtension *extension = ShellExtension::instance();
    if (extension->isActive())
        extension->window_action(uint32_t(window), uint32_t(action));
}
