#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

class AccessibilityController;

// org.wayland95.Accessibility: programs' Wayland95Settings::playSound()
// says a sound played, for SoundSentry and ShowSounds
class AccessibilityAdaptor : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wayland95.Accessibility")

public:
    explicit AccessibilityAdaptor(AccessibilityController *controller);

public slots:
    void SoundPlayed(const QString &event);

private:
    AccessibilityController *m_controller;
};

// Control Panel > Accessibility Options, done where every key and mouse
// event passes first: the compositor.
//  - StickyKeys: Shift, Ctrl, Alt or the Windows key, pressed and let go,
//    stays down for the next key (twice: until pressed again)
//  - FilterKeys: ignores keys pressed again too quickly (bounce), or
//    keys not held for long enough (slow keys)
//  - ToggleKeys: a high tone when Caps/Num/Scroll Lock turns on, low off
//  - MouseKeys: the number pad moves the pointer and clicks
//  - the Win95 shortcuts that turn them on and off
// Programs get the result as ordinary input.
//
// Instantiated in Main.qml, which feeds it the settings.
class AccessibilityController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool stickyKeys MEMBER m_sticky WRITE setStickyKeys NOTIFY settingsChanged)
    Q_PROPERTY(bool stickyLock MEMBER m_stickyLock NOTIFY settingsChanged)
    // a click when a modifier is latched, tones when locked/unlocked
    Q_PROPERTY(bool stickySound MEMBER m_stickySound NOTIFY settingsChanged)
    Q_PROPERTY(bool filterKeys MEMBER m_filter NOTIFY settingsChanged)
    Q_PROPERTY(int bounceMs MEMBER m_bounceMs NOTIFY settingsChanged)
    Q_PROPERTY(int slowMs MEMBER m_slowMs NOTIFY settingsChanged)
    Q_PROPERTY(bool toggleKeys MEMBER m_toggle NOTIFY settingsChanged)
    Q_PROPERTY(bool mouseKeys MEMBER m_mouseKeys WRITE setMouseKeys NOTIFY settingsChanged)
    Q_PROPERTY(int mouseKeysSpeed MEMBER m_mouseSpeed NOTIFY settingsChanged)
    Q_PROPERTY(int mouseKeysAcceleration MEMBER m_mouseAccel NOTIFY settingsChanged)
    Q_PROPERTY(bool mouseKeysNumLockOn MEMBER m_mouseNumLockOn NOTIFY settingsChanged)
    // which shortcut keys work (features.toggled names)
    Q_PROPERTY(QStringList shortcuts MEMBER m_shortcuts NOTIFY settingsChanged)
    // the modifiers StickyKeys holds: "Shift", "Ctrl"... ("Shift (locked)")
    Q_PROPERTY(QStringList heldModifiers READ heldModifiers NOTIFY heldChanged)
    // MouseKeys' pointer, for drawing it when the real cursor can't be
    // moved (nested in another desktop)
    Q_PROPERTY(QPointF pointer READ pointer NOTIFY pointerChanged)
    Q_PROPERTY(bool pointerMoved READ pointerMoved NOTIFY pointerChanged)
    // seconds since the last keyboard or mouse input
    Q_PROPERTY(int idleSeconds READ idleSeconds)

public:
    explicit AccessibilityController(QObject *parent = nullptr);
    ~AccessibilityController() override;

    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    void setStickyKeys(bool on);
    void setMouseKeys(bool on);
    QStringList heldModifiers() const;
    QPointF pointer() const { return m_pointer; }
    bool pointerMoved() const { return m_pointerMoved; }
    int idleSeconds() const { return int(m_lastInput.elapsed() / 1000); }

    // WAV tones: "on", "off" (rising, falling), "high", "low", "click";
    // answers with toneReady(path)
    Q_INVOKABLE void playTone(const QString &tone);

    void soundPlayed(const QString &event) { emit soundEvent(event); }

signals:
    void windowChanged();
    void settingsChanged();
    void heldChanged();
    void pointerChanged();
    // a shortcut key asks to turn a feature on or off: "stickyKeys",
    // "filterKeys", "toggleKeys", "mouseKeys", "highContrast"
    void shortcutPressed(const QString &feature);
    // a lock key changed: on or off (ToggleKeys)
    void lockToggled(bool on);
    // a sound played somewhere on the desktop
    void soundEvent(const QString &event);
    // playTone()'s file is ready to play
    void toneReady(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Key {
        int key = 0;
        quint32 scan = 0, vkey = 0, nmods = 0;
        Qt::KeyboardModifiers mods;
        QString text;
    };
    enum Sticky { Up, Latched, Locked };
    struct Modifier {
        Key press;            // to replay its release
        bool down = false;
        bool usedWithKey = false;
        bool swallowRelease = false;
        Sticky state = Up;
    };

    static Key keyOf(const QKeyEvent *e);
    void deliver(QEvent::Type type, const Key &k);
    // StickyKeys; true if the event is used up
    bool sticky(QEvent::Type type, const Key &k, bool autoRepeat);
    void releaseLatched();
    void shortcutKeys(QEvent::Type type, const Key &k, bool autoRepeat);
    bool mouseKey(QEvent::Type type, const Key &k, bool autoRepeat);
    void moveStep();
    void mouseButton(QEvent::Type type, Qt::MouseButton button);
    void clearSticky();

    QPointer<QQuickWindow> m_window;
    bool m_synthesizing = false;
    QElapsedTimer m_lastInput;

    bool m_sticky = false, m_stickyLock = true, m_stickySound = true;
    QHash<int, Modifier> m_modifiers; // Qt key -> state

    bool m_filter = false;
    int m_bounceMs = 0, m_slowMs = 500;
    int m_lastReleasedScan = -1;
    QElapsedTimer m_sinceRelease;
    QSet<quint32> m_bounced;          // presses ignored: their releases too
    QHash<quint32, Key> m_slowPending; // waiting to be held long enough
    QHash<quint32, QTimer *> m_slowTimers;
    QSet<quint32> m_slowAccepted;

    bool m_toggle = false;
    bool m_scrollLock = false;

    bool m_mouseKeys = false, m_mouseNumLockOn = true;
    int m_mouseSpeed = 5, m_mouseAccel = 5;
    QSet<quint32> m_moveKeys;         // direction keys held
    QTimer m_moveTimer;
    QElapsedTimer m_moveSince, m_moveStep;
    QPointF m_pointer;
    bool m_pointerMoved = false;
    Qt::MouseButton m_mouseButton = Qt::LeftButton; // "/", "*", "-" choose
    Qt::MouseButtons m_held;          // "0" holds a button down (drag)

    QStringList m_shortcuts;
    int m_shiftPresses = 0;
    QElapsedTimer m_shiftTimer;
    QTimer m_rightShiftHold, m_numLockHold;
};
