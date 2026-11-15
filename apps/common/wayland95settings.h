#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QJSEngine;
class QQmlEngine;

// Desktop-wide Wayland 95 settings, shared between processes through
// ~/.config/wayland95/settings.ini. The Control Panel writes them; the
// compositor and every app see changes immediately, because each process
// watches the file and reloads.
//
// It also applies the parts Qt handles itself (double-click interval,
// cursor blink) and plays event sounds.
//
// Exposed to QML as the singleton `Wayland95Settings` (one instance per
// process, also reachable from C++ as instance()).
class Wayland95Settings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Display
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY changed)
    Q_PROPERTY(QString wallpaper READ wallpaper WRITE setWallpaper NOTIFY changed)
    Q_PROPERTY(QString wallpaperMode READ wallpaperMode WRITE setWallpaperMode NOTIFY changed) // center|tile|stretch
    // Appearance: the window colors, by name (see colorKeys()), plus the
    // derived 3D edge colors light/highlight/shadow/dark
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QString colorScheme READ colorScheme NOTIFY changed)
    // Taskbar
    Q_PROPERTY(bool showClock READ showClock WRITE setShowClock NOTIFY changed)
    Q_PROPERTY(bool clock24Hour READ clock24Hour WRITE setClock24Hour NOTIFY changed)
    // the speaker icon (volume control) in the taskbar
    Q_PROPERTY(bool showVolume READ showVolume WRITE setShowVolume NOTIFY changed)
    // the notifications icon in the taskbar, and holding back pop-ups
    Q_PROPERTY(bool showNotifications READ showNotifications WRITE setShowNotifications NOTIFY changed)
    Q_PROPERTY(bool doNotDisturb READ doNotDisturb WRITE setDoNotDisturb NOTIFY changed)
    // the other notification-area icons; showAppIcons is other programs'
    // icons (StatusNotifierItem)
    Q_PROPERTY(bool showNetwork READ showNetwork WRITE setShowNetwork NOTIFY changed)
    Q_PROPERTY(bool showBluetooth READ showBluetooth WRITE setShowBluetooth NOTIFY changed)
    Q_PROPERTY(bool showBattery READ showBattery WRITE setShowBattery NOTIFY changed)
    Q_PROPERTY(bool showAppIcons READ showAppIcons WRITE setShowAppIcons NOTIFY changed)
    // Keyboard ("" layout = leave the compositor's default)
    Q_PROPERTY(QString keyboardLayout READ keyboardLayout WRITE setKeyboardLayout NOTIFY changed)
    Q_PROPERTY(int keyRepeatDelay READ keyRepeatDelay WRITE setKeyRepeatDelay NOTIFY changed) // ms
    Q_PROPERTY(int keyRepeatRate READ keyRepeatRate WRITE setKeyRepeatRate NOTIFY changed)   // chars/s
    Q_PROPERTY(int cursorFlashTime READ cursorFlashTime WRITE setCursorFlashTime NOTIFY changed) // ms per blink cycle, 0 = steady
    // Mouse
    Q_PROPERTY(int doubleClickInterval READ doubleClickInterval WRITE setDoubleClickInterval NOTIFY changed) // ms
    // Sounds: event id -> sound file ("" = none)
    Q_PROPERTY(QVariantMap sounds READ sounds NOTIFY changed)
    // Display > Screen Saver: which one ("none", "blank", "starfield",
    // "polygons", "curves", "marquee"), minutes of idle before it starts,
    // and its own options ({ speed, count, text, ... })
    // Control Panel > Power. Minutes (0 = never) for displayOffAC,
    // displayOffBattery, sleepAC, sleepBattery; actions for lidAction
    // ("nothing", "display", "sleep", "hibernate", "shutdown"), powerButton
    // ("ask", "sleep", "hibernate", "shutdown", "nothing"), sleepButton and
    // criticalBattery (at 5%). Unset keys have defaults.
    Q_PROPERTY(QVariantMap power READ power NOTIFY changed)
    // Control Panel > Accessibility Options (defaults filled in)
    Q_PROPERTY(QVariantMap accessibility READ accessibility NOTIFY changed)
    // Control Panel > Regional Settings: locale names per category
    // (region, numbers, currency, time, date) and firstDayOfWeek
    Q_PROPERTY(QVariantMap region READ region NOTIFY changed)
    // Control Panel > Mouse > Touchpad (defaults filled in)
    Q_PROPERTY(QVariantMap touchpad READ touchpad NOTIFY changed)
    Q_PROPERTY(QString screenSaver READ screenSaver NOTIFY changed)
    Q_PROPERTY(int screenSaverWait READ screenSaverWait NOTIFY changed)
    // Display > Screen Saver > Password protected: ending it asks for your
    // (login) password
    Q_PROPERTY(bool screenSaverPassword READ screenSaverPassword WRITE setScreenSaverPassword NOTIFY changed)
    Q_PROPERTY(QVariantMap screenSaverOptions READ screenSaverOptions NOTIFY changed)
    Q_PROPERTY(QString soundScheme READ soundScheme NOTIFY changed)

public:
    explicit Wayland95Settings(QObject *parent = nullptr);

    static Wayland95Settings *instance();
    static Wayland95Settings *create(QQmlEngine *, QJSEngine *);

    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor &c);
    QString wallpaper() const { return m_wallpaper; }
    void setWallpaper(const QString &path);
    QString wallpaperMode() const { return m_wallpaperMode; }
    void setWallpaperMode(const QString &mode);

    QVariantMap colors() const;
    QString colorScheme() const { return m_colorScheme; }
    // all stored color keys plus the scheme name ("" or "(Custom)" if edited)
    Q_INVOKABLE void setColors(const QVariantMap &colors, const QString &schemeName);
    Q_INVOKABLE QVariantList colorSchemes() const;       // [{ name, colors: {...} }]
    Q_INVOKABLE QStringList colorKeys() const;
    // the 3D edge colors Wayland 95 derives from a face color
    Q_INVOKABLE QVariantMap edgeColors(const QColor &face) const;

    bool showClock() const { return m_showClock; }
    void setShowClock(bool on);
    bool clock24Hour() const { return m_clock24Hour; }
    bool showVolume() const { return m_showVolume; }
    void setShowVolume(bool on);
    bool showNotifications() const { return m_showNotifications; }
    void setShowNotifications(bool on);
    bool doNotDisturb() const { return m_doNotDisturb; }
    void setDoNotDisturb(bool on);
    bool showNetwork() const { return m_showNetwork; }
    void setShowNetwork(bool on);
    bool showBluetooth() const { return m_showBluetooth; }
    void setShowBluetooth(bool on);
    bool showBattery() const { return m_showBattery; }
    void setShowBattery(bool on);
    bool showAppIcons() const { return m_showAppIcons; }
    void setShowAppIcons(bool on);
    void setClock24Hour(bool on);

    QString keyboardLayout() const { return m_keyboardLayout; }
    void setKeyboardLayout(const QString &layout);
    int keyRepeatDelay() const { return m_keyRepeatDelay; }
    void setKeyRepeatDelay(int ms);
    int keyRepeatRate() const { return m_keyRepeatRate; }
    void setKeyRepeatRate(int rate);
    int cursorFlashTime() const { return m_cursorFlashTime; }
    void setCursorFlashTime(int ms);

    int doubleClickInterval() const { return m_doubleClickInterval; }
    void setDoubleClickInterval(int ms);

    QVariantMap sounds() const;
    QString soundScheme() const { return m_soundScheme; }
    Q_INVOKABLE void setSounds(const QVariantMap &sounds, const QString &schemeName);
    Q_INVOKABLE QVariantList soundEvents() const;   // [{ id, label }]
    Q_INVOKABLE QVariantList soundSchemes() const;  // [{ name, sounds: {...} }]
    // play the sound assigned to an event (no-op if none / file missing)
    Q_INVOKABLE void playSound(const QString &event);

    QVariantMap power() const;
    Q_INVOKABLE void setPower(const QVariantMap &power);
    QVariantMap accessibility() const;
    Q_INVOKABLE void setAccessibility(const QVariantMap &values);
    // High Contrast: switches to its color scheme, and back to the one
    // before when turned off
    Q_INVOKABLE void setHighContrast(bool on);
    QVariantMap region() const;
    QVariantMap touchpad() const;
    Q_INVOKABLE void setTouchpad(const QVariantMap &values);
    Q_INVOKABLE void setRegion(const QVariantMap &values);
    // the environment programs should get for the regional settings
    // (LANG, LC_NUMERIC...), empty values for "leave as is"
    Q_INVOKABLE QVariantMap localeEnvironment() const;
    QString screenSaver() const { return m_screenSaver; }
    int screenSaverWait() const { return m_screenSaverWait; }
    bool screenSaverPassword() const { return m_screenSaverPassword; }
    void setScreenSaverPassword(bool on);
    QVariantMap screenSaverOptions() const { return m_screenSaverOptions; }
    Q_INVOKABLE void setScreenSaver(const QString &name, int waitMinutes, const QVariantMap &options);
    // asks the running desktop to show the saver now (Display > Preview);
    // false if no Wayland 95 desktop answered
    Q_INVOKABLE bool previewScreenSaver() const;
    // play a file directly (the Sounds applet's Preview button)
    Q_INVOKABLE bool playFile(const QString &path);

    Q_INVOKABLE QString filePath() const;

signals:
    // one signal for everything: settings change rarely, and it keeps
    // cross-process reloads simple
    void changed();

private:
    void load();
    void store(const QString &key, const QVariant &value);
    void watch();
    void applyStyleHints();

    QSettings m_settings;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;

    QColor m_backgroundColor;
    QString m_wallpaper;
    QString m_wallpaperMode;
    QHash<QString, QColor> m_colors;
    QString m_colorScheme;
    bool m_showClock = true;
    bool m_clock24Hour = false;
    bool m_showVolume = true;
    bool m_showNotifications = true;
    bool m_doNotDisturb = false;
    bool m_showNetwork = true;
    bool m_showBluetooth = true;
    bool m_showBattery = true;
    bool m_showAppIcons = true;
    QString m_keyboardLayout;
    int m_keyRepeatDelay = 600;
    int m_keyRepeatRate = 25;
    int m_cursorFlashTime = 1060;
    int m_doubleClickInterval = 400;
    QHash<QString, QString> m_sounds;
    QVariantMap m_power;
    QString m_screenSaver = QStringLiteral("starfield");
    int m_screenSaverWait = 10;
    QVariantMap m_accessibility;
    QVariantMap m_region;
    QVariantMap m_touchpad;
    bool m_screenSaverPassword = false;
    QVariantMap m_screenSaverOptions;
    QString m_soundScheme;

    QHash<QString, qint64> m_lastPlayed; // event -> ms, to avoid sound pile-ups
    QElapsedTimer m_clock;
};
