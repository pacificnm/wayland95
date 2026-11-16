#pragma once

// The desktop's own keys (volume, brightness, the power and sleep buttons), which wayland95-compositor
// catches and sends to its shell over wayland95-shell-v1
// (protocols/wayland95/). In QML:
//
//   DesktopKeys {
//       onPressed: (key) => ...   // DesktopKeys.VolumeUp, ...
//       onReleased: (key) => ...
//   }
//
// The compositor doesn't repeat a held key; the QML does, if it wants to.
// Only one shell may have them: a second win95shell on the same compositor
// is refused (the protocol's already_bound error ends it).

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>

#include "qwayland-wayland95-shell-v1.h"

class DesktopKeysExtension : public QWaylandClientExtensionTemplate<DesktopKeysExtension>,
                             public QtWayland::wayland95_shell_v1
{
    Q_OBJECT
public:
    DesktopKeysExtension() : QWaylandClientExtensionTemplate<DesktopKeysExtension>(1) { initialize(); }

Q_SIGNALS:
    void key(uint key, bool pressed);

protected:
    void wayland95_shell_v1_key(uint32_t key, uint32_t state) override
    {
        Q_EMIT this->key(key, state == WAYLAND95_SHELL_V1_KEY_STATE_PRESSED);
    }
};

class DesktopKeys : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    enum Key {
        VolumeUp = 1,
        VolumeDown = 2,
        VolumeMute = 3,
        BrightnessUp = 4,
        BrightnessDown = 5,
        Power = 6,
        Sleep = 7,
        Hibernate = 8,
    };
    Q_ENUM(Key)

    explicit DesktopKeys(QObject *parent = nullptr);

    // the compositor sends this shell its keys
    bool active() const;

Q_SIGNALS:
    void pressed(int key);
    void released(int key);
    void activeChanged();

private:
    DesktopKeysExtension *m_extension;
};
