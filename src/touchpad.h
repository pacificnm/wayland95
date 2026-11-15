#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// Touchpads, when Wayland 95 is on the screen itself (Qt's eglfs, reading
// input with libinput). Qt only hands libinput events through and never
// configures the devices, so:
//  - Control Panel > Mouse > Touchpad's settings are applied here to every
//    touchpad libinput opens (tap to click, two-finger or edge scrolling,
//    natural scrolling, ignoring it while typing, how clicks work, speed);
//  - finger scrolling is turned into smooth scroll events here: Qt reads
//    only mouse wheels' scroll values, so a touchpad didn't scroll at all.
//
// It works by catching libinput_get_event(), which Qt's input code calls
// (see touchpad.cpp). Inside another desktop it does nothing: that desktop
// handles its touchpad.
//
// Instantiated in Main.qml, which feeds it Wayland95Settings.touchpad.
class TouchpadControl : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantMap settings READ settings WRITE setSettings NOTIFY settingsChanged)
    // touchpads found: their names
    Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)

public:
    explicit TouchpadControl(QObject *parent = nullptr);
    ~TouchpadControl() override;

    QVariantMap settings() const;
    void setSettings(const QVariantMap &settings);
    QStringList devices() const;

signals:
    void settingsChanged();
    void devicesChanged();
};
