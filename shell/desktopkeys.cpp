#include "desktopkeys.h"

#include <QGuiApplication>
#include <cstdio>

ShellExtension *ShellExtension::instance()
{
    static ShellExtension *extension = new ShellExtension;
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
    connect(extension, &ShellExtension::activeChanged, this, [this] {
        if (ShellExtension::instance()->isActive())
            std::fprintf(stderr, "win95shell: the compositor sends the desktop's keys here\n");
        Q_EMIT activeChanged();
    });
}

bool DesktopKeys::active() const
{
    return ShellExtension::instance()->isActive();
}
