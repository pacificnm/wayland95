#include "desktopkeys.h"

#include <cstdio>

DesktopKeys::DesktopKeys(QObject *parent)
    : QObject(parent)
    , m_extension(new DesktopKeysExtension)
{
    m_extension->setParent(this);
    connect(m_extension, &DesktopKeysExtension::key, this, [this](uint key, bool down) {
        if (down)
            Q_EMIT pressed(int(key));
        else
            Q_EMIT released(int(key));
    });
    connect(m_extension, &DesktopKeysExtension::activeChanged, this, [this] {
        if (m_extension->isActive())
            std::fprintf(stderr, "win95shell: the compositor sends the desktop's keys here\n");
        Q_EMIT activeChanged();
    });
}

bool DesktopKeys::active() const
{
    return m_extension->isActive();
}
