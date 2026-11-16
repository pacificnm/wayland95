#pragma once

// The desktop's own keys (volume, brightness, the power and sleep buttons,
// Print Screen and Ctrl+Shift+5), which wayland95-compositor
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
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>

#include "qwayland-wayland95-shell-v1.h"

// The one binding of wayland95_shell_v1 (the compositor allows one): the
// keys come here, and the window list is asked for here (ScreenGrab).
class ShellExtension : public QWaylandClientExtensionTemplate<ShellExtension>,
                       public QtWayland::wayland95_shell_v1
{
    Q_OBJECT
public:
    ShellExtension() : QWaylandClientExtensionTemplate<ShellExtension>(1) { initialize(); }
    static ShellExtension *instance();

Q_SIGNALS:
    void key(uint key, bool pressed);
    void tone(uint tone);
    void shortcut(uint feature);
    void stickyModifiers(const QString &held);
    // Alt+Tab's box: the windows (title, appId, minimized), then which
    void switcherShown(const QVariantList &windows, int selected);
    void switcherSelected(int selected);
    void switcherHidden();
    void windowMenu(uint window, int x, int y, uint state);

protected:
    void wayland95_shell_v1_key(uint32_t key, uint32_t state) override
    {
        Q_EMIT this->key(key, state == WAYLAND95_SHELL_V1_KEY_STATE_PRESSED);
    }
    void wayland95_shell_v1_tone(uint32_t tone) override { Q_EMIT this->tone(tone); }
    void wayland95_shell_v1_shortcut(uint32_t feature) override { Q_EMIT shortcut(feature); }
    void wayland95_shell_v1_sticky_modifiers(const QString &held) override { Q_EMIT stickyModifiers(held); }
    void wayland95_shell_v1_switcher_entry(const QString &title, const QString &appId, uint32_t minimized) override
    {
        m_entries << QVariantMap{ { QStringLiteral("title"), title }, { QStringLiteral("appId"), appId },
                                  { QStringLiteral("minimized"), minimized != 0 } };
    }
    void wayland95_shell_v1_switcher_show(uint32_t selected) override
    {
        Q_EMIT switcherShown(m_entries, int(selected));
        m_entries.clear();
    }
    void wayland95_shell_v1_switcher_select(uint32_t selected) override { Q_EMIT switcherSelected(int(selected)); }
    void wayland95_shell_v1_switcher_hide() override { Q_EMIT switcherHidden(); }
    void wayland95_shell_v1_window_menu(uint32_t window, int32_t x, int32_t y, uint32_t state) override
    {
        Q_EMIT windowMenu(window, x, y, state);
    }

private:
    QVariantList m_entries;
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
        Print = 9,
        Capture = 10, // Ctrl+Shift+5
        StartMenu = 11, // Ctrl+Esc
        TaskManager = 12, // Ctrl+Shift+Esc
    };
    Q_ENUM(Key)

    explicit DesktopKeys(QObject *parent = nullptr);

    // the compositor sends this shell its keys
    bool active() const;

    // the window menu's choice
    enum WindowAction { Restore = 1, Move = 2, Size = 3, Minimize = 4, Maximize = 5, Close = 6 };
    Q_ENUM(WindowAction)
    Q_INVOKABLE void windowAction(int window, int action);

    // Accessibility Options, done in the compositor: its sounds, its
    // shortcut keys, and the keys StickyKeys holds ("Shift", "Ctrl (locked)")
    enum Tone { Click = 1, High = 2, Low = 3 };
    Q_ENUM(Tone)
    enum Feature { StickyKeys = 1, FilterKeys = 2, ToggleKeys = 3, MouseKeys = 4, HighContrast = 5 };
    Q_ENUM(Feature)

Q_SIGNALS:
    void pressed(int key);
    void released(int key);
    void activeChanged();
    void tone(int tone);
    void shortcut(int feature);
    void stickyModifiers(const QString &held);
    // Alt+Tab's box (the compositor runs Alt+Tab; we draw it)
    void switcherShown(const QVariantList &windows, int selected);
    void switcherSelected(int selected);
    void switcherHidden();
    // a window's menu, at x, y on the screen; state: 1 maximized, 2 minimized
    void windowMenu(int window, int x, int y, int state);

};
