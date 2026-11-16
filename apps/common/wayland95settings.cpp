#include "wayland95settings.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSEngine>
#include <QProcess>
#include <QStandardPaths>
#include <QStyleHints>
#include <cmath>

namespace {

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/wayland95/settings.ini");
}

// ---- color schemes -----------------------------------------------------
// Each scheme: desktop, then the stored color keys in colorKeyOrder().
// Generic names; the colors are approximations in the spirit of classic
// 90s desktop schemes.
const char *const kColorKeys[] = {
    "face", "window", "windowText", "menu", "menuText", "selection", "selectionText",
    "activeTitle", "activeTitle2", "activeTitleText", "inactiveTitle", "inactiveTitle2", "inactiveTitleText",
};
constexpr int kColorKeyCount = int(sizeof(kColorKeys) / sizeof(kColorKeys[0]));

struct ColorScheme {
    const char *name;
    const char *desktop;
    const char *colors[kColorKeyCount];
};

// face, window, windowText, menu, menuText, selection, selectionText,
// activeTitle, activeTitle2, activeTitleText, inactiveTitle, inactiveTitle2, inactiveTitleText
const ColorScheme kSchemes[] = {
    {"Wayland 95 Standard", "#008080", {"#c0c0c0", "#ffffff", "#000000", "#c0c0c0", "#000000", "#000080", "#ffffff",
                                        "#000080", "#1084d0", "#ffffff", "#808080", "#b5b5b5", "#c0c0c0"}},
    {"Brick", "#400000", {"#c2bfa5", "#ffffff", "#000000", "#c2bfa5", "#000000", "#800000", "#ffffff",
                          "#800000", "#c06050", "#ffffff", "#808080", "#b0aea0", "#c2bfa5"}},
    {"Desert", "#a28d68", {"#d5ccbb", "#ffffff", "#000000", "#d5ccbb", "#000000", "#008080", "#ffffff",
                           "#008080", "#40b0b0", "#ffffff", "#a28d68", "#c8b898", "#d5ccbb"}},
    {"Eggplant", "#400040", {"#90b0a8", "#ffffff", "#000000", "#90b0a8", "#000000", "#402060", "#ffffff",
                             "#402060", "#806090", "#ffffff", "#607070", "#90a8a0", "#000000"}},
    {"High Contrast Black", "#000000", {"#000000", "#000000", "#ffffff", "#000000", "#ffffff", "#800080", "#ffffff",
                                        "#800080", "#800080", "#ffffff", "#008000", "#008000", "#ffffff"}},
    {"High Contrast White", "#ffffff", {"#ffffff", "#ffffff", "#000000", "#ffffff", "#000000", "#000000", "#ffffff",
                                        "#000000", "#000000", "#ffffff", "#ffffff", "#ffffff", "#000000"}},
    {"Lilac", "#5a4e9a", {"#aea8d9", "#ffffff", "#000000", "#aea8d9", "#000000", "#5a4e9a", "#ffffff",
                          "#5a4e9a", "#9088d0", "#ffffff", "#aea8d9", "#c8c2e8", "#5a4e9a"}},
    {"Marine", "#004040", {"#88c0b8", "#ffffff", "#000000", "#88c0b8", "#000000", "#008080", "#ffffff",
                           "#000080", "#2080c0", "#ffffff", "#408080", "#70a8a0", "#000000"}},
    {"Plum", "#402040", {"#a89890", "#ffffff", "#000000", "#a89890", "#000000", "#402040", "#ffffff",
                         "#402040", "#806078", "#ffffff", "#a89890", "#c8bcb4", "#402040"}},
    {"Pumpkin", "#000000", {"#e8c070", "#ffffff", "#000000", "#e8c070", "#000000", "#400040", "#ffffff",
                            "#400040", "#806080", "#ffffff", "#808080", "#a0a0a0", "#e8c070"}},
    {"Rainy Day", "#404060", {"#8098b0", "#ffffff", "#000000", "#8098b0", "#000000", "#404080", "#ffffff",
                              "#404080", "#7090c0", "#ffffff", "#8098b0", "#a0b4c8", "#404060"}},
    {"Rose", "#804060", {"#d0a8a8", "#ffffff", "#000000", "#d0a8a8", "#000000", "#800040", "#ffffff",
                         "#800040", "#c06080", "#ffffff", "#a08080", "#c0a0a0", "#d0a8a8"}},
    {"Slate", "#404060", {"#a8b0b8", "#ffffff", "#000000", "#a8b0b8", "#000000", "#406080", "#ffffff",
                          "#406080", "#7090b0", "#ffffff", "#808890", "#a0a8b0", "#c0c8d0"}},
    {"Spruce", "#204020", {"#a0c0a0", "#ffffff", "#000000", "#a0c0a0", "#000000", "#406040", "#ffffff",
                           "#406040", "#80a080", "#ffffff", "#709070", "#90b090", "#204020"}},
    {"Storm", "#000040", {"#c0c0c0", "#ffffff", "#000000", "#c0c0c0", "#000000", "#800080", "#ffffff",
                          "#800080", "#c040c0", "#ffffff", "#808080", "#a0a0a0", "#c0c0c0"}},
    {"Teal", "#004040", {"#c0c0c0", "#ffffff", "#000000", "#c0c0c0", "#000000", "#008080", "#ffffff",
                         "#008080", "#40c0c0", "#ffffff", "#808080", "#b0b0b0", "#c0c0c0"}},
    {"Wheat", "#808040", {"#d8d0a0", "#ffffff", "#000000", "#d8d0a0", "#000000", "#808000", "#ffffff",
                          "#808000", "#c0c060", "#ffffff", "#a09870", "#c8c098", "#d8d0a0"}},
};

QColor mix(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

// ---- sounds --------------------------------------------------------------
struct SoundEvent {
    const char *id;
    const char *label;
    const char *defaultFile; // in /usr/share/sounds/freedesktop/stereo, or ""
};

const SoundEvent kSoundEvents[] = {
    {"default",     "Default sound",       "bell.oga"},
    {"startup",     "Start Wayland 95",    "service-login.oga"},
    {"exit",        "Exit Wayland 95",     "service-logout.oga"},
    {"open",        "Open program",        ""},
    {"close",       "Close program",       ""},
    {"maximize",    "Maximize",            ""},
    {"minimize",    "Minimize",            ""},
    {"restore",     "Restore Up",          ""},
    {"menuPopup",   "Menu popup",          ""},
    {"asterisk",    "Asterisk",            "dialog-information.oga"},
    {"question",    "Question",            "window-question.oga"},
    {"exclamation", "Exclamation",         "dialog-warning.oga"},
    {"critical",    "Critical Stop",       "dialog-error.oga"},
    {"emptyTrash",  "Empty Recycle Bin",   "trash-empty.oga"},
    {"notification", "New notification",   "message-new-instant.oga"},
};

const QString kFreedesktopSounds = QStringLiteral("/usr/share/sounds/freedesktop/stereo/");

QString defaultSound(const SoundEvent &e)
{
    if (!*e.defaultFile)
        return {};
    const QString path = kFreedesktopSounds + QLatin1String(e.defaultFile);
    return QFileInfo::exists(path) ? path : QString();
}

} // namespace

// ---------------------------------------------------------------------------

Wayland95Settings *Wayland95Settings::instance()
{
    static Wayland95Settings *s = new Wayland95Settings(qApp);
    return s;
}

Wayland95Settings *Wayland95Settings::create(QQmlEngine *, QJSEngine *)
{
    // one instance per process, shared by QML and C++; C++ owns it
    Wayland95Settings *s = instance();
    QJSEngine::setObjectOwnership(s, QJSEngine::CppOwnership);
    return s;
}

Wayland95Settings::Wayland95Settings(QObject *parent)
    : QObject(parent)
    , m_settings(settingsPath(), QSettings::IniFormat)
{
    QDir().mkpath(QFileInfo(settingsPath()).absolutePath());
    m_clock.start();

    // QSettings saves by writing a new file and renaming it over the old
    // one, which drops a plain file watch - so watch the folder as well
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(100);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this] {
        m_settings.sync();
        load();
        watch();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    connect(this, &Wayland95Settings::changed, this, &Wayland95Settings::applyStyleHints);

    load();
    watch();
    applyStyleHints();
}

QString Wayland95Settings::filePath() const
{
    return settingsPath();
}

void Wayland95Settings::watch()
{
    const QString file = settingsPath();
    const QString dir = QFileInfo(file).absolutePath();
    if (!m_watcher.directories().contains(dir))
        m_watcher.addPath(dir);
    if (QFileInfo::exists(file) && !m_watcher.files().contains(file))
        m_watcher.addPath(file);
}

void Wayland95Settings::applyStyleHints()
{
    if (QStyleHints *hints = QGuiApplication::styleHints()) {
        if (hints->mouseDoubleClickInterval() != m_doubleClickInterval)
            hints->setMouseDoubleClickInterval(m_doubleClickInterval);
        if (hints->cursorFlashTime() != m_cursorFlashTime)
            hints->setCursorFlashTime(m_cursorFlashTime);
    }
}

void Wayland95Settings::load()
{
    const QColor bg(m_settings.value(QStringLiteral("Display/BackgroundColor"), QStringLiteral("#008080")).toString());
    const QString wallpaper = m_settings.value(QStringLiteral("Display/Wallpaper")).toString();
    QString mode = m_settings.value(QStringLiteral("Display/WallpaperMode"), QStringLiteral("center")).toString();
    if (mode != QLatin1String("tile") && mode != QLatin1String("stretch"))
        mode = QStringLiteral("center");

    QHash<QString, QColor> colors;
    const ColorScheme &standard = kSchemes[0];
    for (int i = 0; i < kColorKeyCount; ++i) {
        const QString key = QLatin1String(kColorKeys[i]);
        const QColor c(m_settings.value(QStringLiteral("Appearance/") + key, QLatin1String(standard.colors[i])).toString());
        colors.insert(key, c.isValid() ? c : QColor(QLatin1String(standard.colors[i])));
    }
    const QString colorScheme = m_settings.value(QStringLiteral("Appearance/Scheme"),
                                                 QLatin1String(standard.name)).toString();

    const bool showClock = m_settings.value(QStringLiteral("Taskbar/ShowClock"), true).toBool();
    const bool clock24 = m_settings.value(QStringLiteral("Taskbar/Clock24Hour"), false).toBool();
    const bool showVolume = m_settings.value(QStringLiteral("Taskbar/ShowVolume"), true).toBool();
    const bool showNotifications = m_settings.value(QStringLiteral("Taskbar/ShowNotifications"), true).toBool();
    const bool doNotDisturb = m_settings.value(QStringLiteral("Notifications/DoNotDisturb"), false).toBool();
    const bool showNetwork = m_settings.value(QStringLiteral("Taskbar/ShowNetwork"), true).toBool();
    const bool showBluetooth = m_settings.value(QStringLiteral("Taskbar/ShowBluetooth"), true).toBool();
    const bool showBattery = m_settings.value(QStringLiteral("Taskbar/ShowBattery"), true).toBool();
    const bool showAppIcons = m_settings.value(QStringLiteral("Taskbar/ShowAppIcons"), true).toBool();
    const QString layout = m_settings.value(QStringLiteral("Keyboard/Layout")).toString();
    const int delay = qBound(100, m_settings.value(QStringLiteral("Keyboard/RepeatDelay"), 600).toInt(), 2000);
    const int rate = qBound(1, m_settings.value(QStringLiteral("Keyboard/RepeatRate"), 25).toInt(), 60);
    const int flash = qBound(0, m_settings.value(QStringLiteral("Keyboard/CursorFlashTime"), 1060).toInt(), 4000);
    const int dblClick = qBound(100, m_settings.value(QStringLiteral("Mouse/DoubleClickInterval"), 400).toInt(), 2000);

    QHash<QString, QString> sounds;
    const bool soundsSaved = m_settings.childGroups().contains(QStringLiteral("Sounds"));
    for (const SoundEvent &e : kSoundEvents) {
        const QString id = QLatin1String(e.id);
        // events added since the sounds were last saved get their default
        const QString key = QStringLiteral("Sounds/") + id;
        sounds.insert(id, soundsSaved && m_settings.contains(key) ? m_settings.value(key).toString() : defaultSound(e));
    }
    const QString soundScheme = m_settings.value(QStringLiteral("SoundScheme/Name"),
                                                 QStringLiteral("Wayland 95 Default")).toString();

    const bool differs = bg != m_backgroundColor || wallpaper != m_wallpaper || mode != m_wallpaperMode
        || colors != m_colors || colorScheme != m_colorScheme || showClock != m_showClock
        || clock24 != m_clock24Hour || showVolume != m_showVolume || showNotifications != m_showNotifications || doNotDisturb != m_doNotDisturb || showNetwork != m_showNetwork || showBluetooth != m_showBluetooth || showBattery != m_showBattery || showAppIcons != m_showAppIcons || layout != m_keyboardLayout || delay != m_keyRepeatDelay
        || rate != m_keyRepeatRate || flash != m_cursorFlashTime || dblClick != m_doubleClickInterval
        || sounds != m_sounds || soundScheme != m_soundScheme;

    const QString saver = m_settings.value(QStringLiteral("ScreenSaver/Name"), QStringLiteral("starfield")).toString();
    const int saverWait = qBound(1, m_settings.value(QStringLiteral("ScreenSaver/Wait"), 10).toInt(), 600);
    const bool saverPassword = m_settings.value(QStringLiteral("ScreenSaver/Password"), false).toBool();
    QVariantMap saverOptions;
    m_settings.beginGroup(QStringLiteral("ScreenSaverOptions"));
    for (const QString &key : m_settings.childKeys())
        saverOptions.insert(key, m_settings.value(key));
    m_settings.endGroup();
    QVariantMap power;
    m_settings.beginGroup(QStringLiteral("Power"));
    for (const QString &key : m_settings.childKeys())
        power.insert(key, m_settings.value(key));
    m_settings.endGroup();
    const bool powerDiffers = power != m_power;
    m_power = power;
    QVariantMap access, region;
    m_settings.beginGroup(QStringLiteral("Accessibility"));
    for (const QString &key : m_settings.childKeys())
        access.insert(key, m_settings.value(key));
    m_settings.endGroup();
    m_settings.beginGroup(QStringLiteral("Region"));
    for (const QString &key : m_settings.childKeys())
        region.insert(key, m_settings.value(key));
    m_settings.endGroup();
    QVariantMap touchpad;
    m_settings.beginGroup(QStringLiteral("Touchpad"));
    for (const QString &key : m_settings.childKeys())
        touchpad.insert(key, m_settings.value(key));
    m_settings.endGroup();
    const bool groupsDiffer = access != m_accessibility || region != m_region || touchpad != m_touchpad;
    m_accessibility = access;
    m_region = region;
    m_touchpad = touchpad;

    const bool saverDiffers = saver != m_screenSaver || saverWait != m_screenSaverWait
                              || saverOptions != m_screenSaverOptions || saverPassword != m_screenSaverPassword;
    m_screenSaverPassword = saverPassword;
    m_screenSaver = saver;
    m_screenSaverWait = saverWait;
    m_screenSaverOptions = saverOptions;

    m_backgroundColor = bg.isValid() ? bg : QColor(0x00, 0x80, 0x80);
    m_wallpaper = wallpaper;
    m_wallpaperMode = mode;
    m_colors = colors;
    m_colorScheme = colorScheme;
    m_showClock = showClock;
    m_clock24Hour = clock24;
    m_showVolume = showVolume;
    m_showNotifications = showNotifications;
    m_doNotDisturb = doNotDisturb;
    m_showNetwork = showNetwork;
    m_showBluetooth = showBluetooth;
    m_showBattery = showBattery;
    m_showAppIcons = showAppIcons;
    m_keyboardLayout = layout;
    m_keyRepeatDelay = delay;
    m_keyRepeatRate = rate;
    m_cursorFlashTime = flash;
    m_doubleClickInterval = dblClick;
    m_sounds = sounds;
    m_soundScheme = soundScheme;

    if (differs || saverDiffers || powerDiffers || groupsDiffer)
        emit changed();
}

void Wayland95Settings::setScreenSaver(const QString &name, int waitMinutes, const QVariantMap &options)
{
    m_screenSaver = name;
    m_screenSaverWait = qBound(1, waitMinutes, 600);
    m_screenSaverOptions = options;
    m_settings.setValue(QStringLiteral("ScreenSaver/Name"), m_screenSaver);
    m_settings.setValue(QStringLiteral("ScreenSaver/Wait"), m_screenSaverWait);
    m_settings.remove(QStringLiteral("ScreenSaverOptions"));
    for (auto it = options.cbegin(); it != options.cend(); ++it)
        m_settings.setValue(QStringLiteral("ScreenSaverOptions/") + it.key(), it.value());
    m_settings.sync();
    watch();
    emit changed();
}

QVariantMap Wayland95Settings::power() const
{
    QVariantMap p{
        { QStringLiteral("displayOffAC"), 10 },
        { QStringLiteral("displayOffBattery"), 5 },
        { QStringLiteral("sleepAC"), 0 },
        { QStringLiteral("sleepBattery"), 15 },
        { QStringLiteral("lidAction"), QStringLiteral("sleep") },
        { QStringLiteral("powerButton"), QStringLiteral("ask") },
        { QStringLiteral("sleepButton"), QStringLiteral("sleep") },
        { QStringLiteral("criticalBattery"), QStringLiteral("hibernate") },
    };
    for (auto it = m_power.cbegin(); it != m_power.cend(); ++it) {
        // numbers come back from the INI file as strings
        const QVariant def = p.value(it.key());
        p.insert(it.key(), def.typeId() == QMetaType::Int ? QVariant(it.value().toInt()) : it.value());
    }
    return p;
}

void Wayland95Settings::setPower(const QVariantMap &power)
{
    m_power = power;
    m_settings.remove(QStringLiteral("Power"));
    for (auto it = power.cbegin(); it != power.cend(); ++it)
        m_settings.setValue(QStringLiteral("Power/") + it.key(), it.value());
    m_settings.sync();
    watch();
    emit changed();
}

namespace {
// an INI group as a map over defaults (INI values come back as strings)
QVariantMap withDefaults(QVariantMap defaults, const QVariantMap &stored)
{
    for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
        const QVariant def = defaults.value(it.key());
        if (def.typeId() == QMetaType::Int)
            defaults.insert(it.key(), it.value().toInt());
        else if (def.typeId() == QMetaType::Bool)
            defaults.insert(it.key(), it.value().toString() == QLatin1String("true") || it.value().toString() == QLatin1String("1"));
        else
            defaults.insert(it.key(), it.value());
    }
    return defaults;
}
} // namespace

QVariantMap Wayland95Settings::accessibility() const
{
    return withDefaults({
        // Keyboard
        { QStringLiteral("stickyKeys"), false }, { QStringLiteral("stickyLock"), true },
        { QStringLiteral("stickyShortcut"), true }, { QStringLiteral("stickySound"), true },
        { QStringLiteral("filterKeys"), false }, { QStringLiteral("filterBounceMs"), 0 },
        { QStringLiteral("filterSlowMs"), 500 }, { QStringLiteral("filterShortcut"), true },
        { QStringLiteral("toggleKeys"), false }, { QStringLiteral("toggleShortcut"), true },
        // Sound: none, caption (the active title bar), window, desktop
        { QStringLiteral("soundSentry"), false }, { QStringLiteral("soundSentryFlash"), QStringLiteral("caption") },
        { QStringLiteral("showSounds"), false },
        // Display
        { QStringLiteral("highContrast"), false }, { QStringLiteral("highContrastScheme"), QStringLiteral("High Contrast Black") },
        { QStringLiteral("highContrastShortcut"), true }, { QStringLiteral("schemeBeforeHighContrast"), QString() },
        // Mouse: speed 1-10; the keypad moves the pointer when Num Lock is on (or off)
        { QStringLiteral("mouseKeys"), false }, { QStringLiteral("mouseKeysSpeed"), 5 },
        { QStringLiteral("mouseKeysAcceleration"), 5 }, { QStringLiteral("mouseKeysNumLockOn"), true },
        { QStringLiteral("mouseKeysShortcut"), true },
        // General
        { QStringLiteral("autoReset"), false }, { QStringLiteral("autoResetMinutes"), 10 },
        { QStringLiteral("notifySound"), true }, { QStringLiteral("notifyMessage"), true },
    }, m_accessibility);
}

void Wayland95Settings::setAccessibility(const QVariantMap &values)
{
    m_accessibility = values;
    m_settings.remove(QStringLiteral("Accessibility"));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        m_settings.setValue(QStringLiteral("Accessibility/") + it.key(), it.value());
    m_settings.sync();
    watch();
    emit changed();
}

void Wayland95Settings::setHighContrast(bool on)
{
    QVariantMap access = accessibility();
    auto apply = [this](const QString &name) {
        for (const QVariant &v : colorSchemes()) {
            const QVariantMap scheme = v.toMap();
            if (scheme.value(QStringLiteral("name")).toString() == name) {
                setColors(scheme.value(QStringLiteral("colors")).toMap(), name);
                return true;
            }
        }
        return false;
    };
    if (on) {
        if (!access.value(QStringLiteral("highContrast")).toBool())
            access.insert(QStringLiteral("schemeBeforeHighContrast"), m_colorScheme);
        apply(access.value(QStringLiteral("highContrastScheme")).toString());
    } else if (access.value(QStringLiteral("highContrast")).toBool()) {
        // back to what it was (or the default scheme)
        if (!apply(access.value(QStringLiteral("schemeBeforeHighContrast")).toString()))
            apply(colorSchemes().value(0).toMap().value(QStringLiteral("name")).toString());
    }
    access.insert(QStringLiteral("highContrast"), on);
    setAccessibility(access);
}

QVariantMap Wayland95Settings::touchpad() const
{
    return withDefaults({
        { QStringLiteral("tapToClick"), true },
        // "twofinger", "edge" or "none"
        { QStringLiteral("scrollMethod"), QStringLiteral("twofinger") },
        // content follows the fingers, like a phone
        { QStringLiteral("naturalScroll"), false },
        // 1-20, 10 = normal
        { QStringLiteral("scrollSpeed"), 10 },
        { QStringLiteral("disableWhileTyping"), true },
        // "default" (what the touchpad does by itself), "areas" (the
        // bottom right corner is the right button), "fingers" (two fingers
        // pressing down right-click, three middle-click)
        { QStringLiteral("clickMethod"), QStringLiteral("default") },
        // -10..10, 0 = normal
        { QStringLiteral("speed"), 0 },
        { QStringLiteral("acceleration"), true },
        { QStringLiteral("disableWithMouse"), false },
        { QStringLiteral("middleEmulation"), false },
    }, m_touchpad);
}

void Wayland95Settings::setTouchpad(const QVariantMap &values)
{
    m_touchpad = values;
    m_settings.remove(QStringLiteral("Touchpad"));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        m_settings.setValue(QStringLiteral("Touchpad/") + it.key(), it.value());
    m_settings.sync();
    watch();
    emit changed();
}

QVariantMap Wayland95Settings::region() const
{
    // "" means: whatever the session already uses
    return withDefaults({
        { QStringLiteral("region"), QString() }, { QStringLiteral("numbers"), QString() },
        { QStringLiteral("currency"), QString() }, { QStringLiteral("time"), QString() },
        { QStringLiteral("date"), QString() }, { QStringLiteral("firstDayOfWeek"), -1 },
    }, m_region);
}

void Wayland95Settings::setRegion(const QVariantMap &values)
{
    m_region = values;
    m_settings.remove(QStringLiteral("Region"));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        m_settings.setValue(QStringLiteral("Region/") + it.key(), it.value());
    m_settings.sync();

    // Programs Wayland 95 starts get these (Launcher); so do programs that
    // systemd starts for you (D-Bus services), from environment.d
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/environment.d");
    const QString file = dir + QStringLiteral("/90-wayland95-region.conf");
    const QVariantMap env = localeEnvironment();
    if (env.isEmpty()) {
        QFile::remove(file);
    } else if (QDir().mkpath(dir)) {
        QFile f(file);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            f.write("# Written by Wayland 95's Control Panel > Regional Settings\n");
            for (auto it = env.cbegin(); it != env.cend(); ++it)
                f.write(it.key().toUtf8() + '=' + it.value().toString().toUtf8() + '\n');
        }
    }
    watch();
    emit changed();
}

QVariantMap Wayland95Settings::localeEnvironment() const
{
    const QVariantMap r = region();
    QVariantMap env;
    const QString lang = r.value(QStringLiteral("region")).toString();
    if (!lang.isEmpty()) {
        env.insert(QStringLiteral("LANG"), lang);
        // measurements and paper go with the region
        env.insert(QStringLiteral("LC_MEASUREMENT"), lang);
        env.insert(QStringLiteral("LC_PAPER"), lang);
    }
    // time and date share one locale category on Linux (LC_TIME); the
    // Date tab's choice wins when the two differ
    const QString time = r.value(QStringLiteral("date")).toString().isEmpty() ? r.value(QStringLiteral("time")).toString()
                                                                              : r.value(QStringLiteral("date")).toString();
    for (const auto &[key, var] : { std::pair{ "numbers", "LC_NUMERIC" }, std::pair{ "currency", "LC_MONETARY" } }) {
        const QString v = r.value(QLatin1String(key)).toString();
        if (!v.isEmpty())
            env.insert(QLatin1String(var), v);
    }
    if (!time.isEmpty())
        env.insert(QStringLiteral("LC_TIME"), time);
    return env;
}

bool Wayland95Settings::previewScreenSaver() const
{
    // the compositor's org.wayland95.ScreenSaver (src/screensaver.cpp)
    const QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.wayland95.ScreenSaver"), QStringLiteral("/org/wayland95/ScreenSaver"),
        QStringLiteral("org.wayland95.ScreenSaver"), QStringLiteral("Preview"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 2000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

void Wayland95Settings::store(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
    m_settings.sync(); // write now, so other processes pick it up
    watch();
    emit changed();
}

// ---- display ---------------------------------------------------------------

void Wayland95Settings::setBackgroundColor(const QColor &c)
{
    if (c == m_backgroundColor || !c.isValid())
        return;
    m_backgroundColor = c;
    store(QStringLiteral("Display/BackgroundColor"), c.name());
}

void Wayland95Settings::setWallpaper(const QString &path)
{
    if (path == m_wallpaper)
        return;
    m_wallpaper = path;
    store(QStringLiteral("Display/Wallpaper"), path);
}

void Wayland95Settings::setWallpaperMode(const QString &mode)
{
    if (mode == m_wallpaperMode)
        return;
    m_wallpaperMode = mode;
    store(QStringLiteral("Display/WallpaperMode"), mode);
}

// ---- appearance --------------------------------------------------------------

QVariantMap Wayland95Settings::edgeColors(const QColor &face) const
{
    // Win95's standard grays come out exactly: c0c0c0 -> light dfdfdf,
    // highlight ffffff, shadow 808080. Dark faces (high contrast) get light
    // edges instead, so 3D shapes stay visible.
    const QColor white(Qt::white), black(Qt::black);
    QColor light, highlight, shadow;
    if (face.lightnessF() < 0.25) {
        shadow = mix(face, white, 0.4);
        light = mix(face, white, 0.6);
        highlight = white;
    } else {
        shadow = mix(face, black, 1.0 / 3.0);
        light = mix(face, white, 0.49); // c0 -> df, as in Win95
        highlight = face.lightnessF() >= 0.5 ? white : mix(face, white, 0.75);
    }
    return {
        {QStringLiteral("light"), light},
        {QStringLiteral("highlight"), highlight},
        {QStringLiteral("shadow"), shadow},
        {QStringLiteral("dark"), black},
    };
}

QVariantMap Wayland95Settings::colors() const
{
    QVariantMap map;
    for (auto it = m_colors.cbegin(); it != m_colors.cend(); ++it)
        map.insert(it.key(), it.value());
    const QVariantMap edges = edgeColors(m_colors.value(QStringLiteral("face")));
    for (auto it = edges.cbegin(); it != edges.cend(); ++it)
        map.insert(it.key(), it.value());
    return map;
}

QStringList Wayland95Settings::colorKeys() const
{
    QStringList keys;
    for (const char *k : kColorKeys)
        keys << QLatin1String(k);
    return keys;
}

QVariantList Wayland95Settings::colorSchemes() const
{
    QVariantList list;
    for (const ColorScheme &s : kSchemes) {
        QVariantMap colors;
        for (int i = 0; i < kColorKeyCount; ++i)
            colors.insert(QLatin1String(kColorKeys[i]), QColor(QLatin1String(s.colors[i])));
        colors.insert(QStringLiteral("desktop"), QColor(QLatin1String(s.desktop)));
        list << QVariantMap{{QStringLiteral("name"), QLatin1String(s.name)}, {QStringLiteral("colors"), colors}};
    }
    return list;
}

void Wayland95Settings::setColors(const QVariantMap &colors, const QString &schemeName)
{
    bool any = false;
    for (const char *k : kColorKeys) {
        const QString key = QLatin1String(k);
        const QColor c = colors.value(key).value<QColor>();
        if (!c.isValid() || c == m_colors.value(key))
            continue;
        m_colors.insert(key, c);
        m_settings.setValue(QStringLiteral("Appearance/") + key, c.name());
        any = true;
    }
    if (schemeName != m_colorScheme) {
        m_colorScheme = schemeName;
        m_settings.setValue(QStringLiteral("Appearance/Scheme"), schemeName);
        any = true;
    }
    if (!any)
        return;
    m_settings.sync();
    watch();
    emit changed();
}

// ---- taskbar / keyboard / mouse ---------------------------------------------

void Wayland95Settings::setShowNetwork(bool on)
{
    if (on == m_showNetwork)
        return;
    m_showNetwork = on;
    store(QStringLiteral("Taskbar/ShowNetwork"), on);
}

void Wayland95Settings::setShowBluetooth(bool on)
{
    if (on == m_showBluetooth)
        return;
    m_showBluetooth = on;
    store(QStringLiteral("Taskbar/ShowBluetooth"), on);
}

void Wayland95Settings::setShowBattery(bool on)
{
    if (on == m_showBattery)
        return;
    m_showBattery = on;
    store(QStringLiteral("Taskbar/ShowBattery"), on);
}

void Wayland95Settings::setShowAppIcons(bool on)
{
    if (on == m_showAppIcons)
        return;
    m_showAppIcons = on;
    store(QStringLiteral("Taskbar/ShowAppIcons"), on);
}

void Wayland95Settings::setShowNotifications(bool on)
{
    if (on == m_showNotifications)
        return;
    m_showNotifications = on;
    store(QStringLiteral("Taskbar/ShowNotifications"), on);
}

void Wayland95Settings::setDoNotDisturb(bool on)
{
    if (on == m_doNotDisturb)
        return;
    m_doNotDisturb = on;
    store(QStringLiteral("Notifications/DoNotDisturb"), on);
}

void Wayland95Settings::setScreenSaverPassword(bool on)
{
    if (on == m_screenSaverPassword)
        return;
    m_screenSaverPassword = on;
    store(QStringLiteral("ScreenSaver/Password"), on);
}

void Wayland95Settings::setShowVolume(bool on)
{
    if (on == m_showVolume)
        return;
    m_showVolume = on;
    store(QStringLiteral("Taskbar/ShowVolume"), on);
}

void Wayland95Settings::setShowClock(bool on)
{
    if (on == m_showClock)
        return;
    m_showClock = on;
    store(QStringLiteral("Taskbar/ShowClock"), on);
}

void Wayland95Settings::setClock24Hour(bool on)
{
    if (on == m_clock24Hour)
        return;
    m_clock24Hour = on;
    store(QStringLiteral("Taskbar/Clock24Hour"), on);
}

void Wayland95Settings::setKeyboardLayout(const QString &layout)
{
    if (layout == m_keyboardLayout)
        return;
    m_keyboardLayout = layout;
    store(QStringLiteral("Keyboard/Layout"), layout);
}

void Wayland95Settings::setKeyRepeatDelay(int ms)
{
    ms = qBound(100, ms, 2000);
    if (ms == m_keyRepeatDelay)
        return;
    m_keyRepeatDelay = ms;
    store(QStringLiteral("Keyboard/RepeatDelay"), ms);
}

void Wayland95Settings::setKeyRepeatRate(int rate)
{
    rate = qBound(1, rate, 60);
    if (rate == m_keyRepeatRate)
        return;
    m_keyRepeatRate = rate;
    store(QStringLiteral("Keyboard/RepeatRate"), rate);
}

void Wayland95Settings::setCursorFlashTime(int ms)
{
    ms = qBound(0, ms, 4000);
    if (ms == m_cursorFlashTime)
        return;
    m_cursorFlashTime = ms;
    store(QStringLiteral("Keyboard/CursorFlashTime"), ms);
}

void Wayland95Settings::setDoubleClickInterval(int ms)
{
    ms = qBound(100, ms, 2000);
    if (ms == m_doubleClickInterval)
        return;
    m_doubleClickInterval = ms;
    store(QStringLiteral("Mouse/DoubleClickInterval"), ms);
}

// ---- sounds -----------------------------------------------------------------

QVariantMap Wayland95Settings::sounds() const
{
    QVariantMap map;
    for (auto it = m_sounds.cbegin(); it != m_sounds.cend(); ++it)
        map.insert(it.key(), it.value());
    return map;
}

QVariantList Wayland95Settings::soundEvents() const
{
    QVariantList list;
    for (const SoundEvent &e : kSoundEvents)
        list << QVariantMap{{QStringLiteral("id"), QLatin1String(e.id)}, {QStringLiteral("label"), QLatin1String(e.label)}};
    return list;
}

QVariantList Wayland95Settings::soundSchemes() const
{
    QVariantMap defaults, none;
    for (const SoundEvent &e : kSoundEvents) {
        defaults.insert(QLatin1String(e.id), defaultSound(e));
        none.insert(QLatin1String(e.id), QString());
    }
    return {
        QVariantMap{{QStringLiteral("name"), QStringLiteral("Wayland 95 Default")}, {QStringLiteral("sounds"), defaults}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("No Sounds")}, {QStringLiteral("sounds"), none}},
    };
}

void Wayland95Settings::setSounds(const QVariantMap &sounds, const QString &schemeName)
{
    for (const SoundEvent &e : kSoundEvents) {
        const QString id = QLatin1String(e.id);
        const QString path = sounds.value(id).toString();
        m_sounds.insert(id, path);
        // written even when empty, so "no sound" is remembered
        m_settings.setValue(QStringLiteral("Sounds/") + id, path);
    }
    m_soundScheme = schemeName;
    m_settings.setValue(QStringLiteral("SoundScheme/Name"), schemeName);
    m_settings.sync();
    watch();
    emit changed();
}

bool Wayland95Settings::playFile(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return false;
    // pw-play (PipeWire) and paplay (PulseAudio) handle ogg/flac/wav; aplay
    // only wav. Fire and forget.
    //
    // Labelled as event sounds: pw-play otherwise says "Music", and the
    // sound system keeps a volume (and mute) per label, so muting music once
    // silenced every desktop sound.
    for (const char *player : {"pw-play", "paplay", "aplay"}) {
        const QString exe = QStandardPaths::findExecutable(QLatin1String(player));
        if (exe.isEmpty())
            continue;
        if (QLatin1String(player) == QLatin1String("aplay") && !path.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive))
            continue;
        QStringList args;
        if (QLatin1String(player) == QLatin1String("pw-play"))
            args << QStringLiteral("--media-role=Notification");
        else if (QLatin1String(player) == QLatin1String("paplay"))
            args << QStringLiteral("--property=media.role=event");
        args << path;
        if (QProcess::startDetached(exe, args))
            return true;
    }
    return false;
}

void Wayland95Settings::playSound(const QString &event)
{
    // the same event many times in a burst (several windows closing) plays once
    const qint64 now = m_clock.elapsed();
    if (now - m_lastPlayed.value(event, -10000) < 150)
        return;
    m_lastPlayed.insert(event, now);
    // SoundSentry / ShowSounds: the desktop shows it, too (even with the
    // No Sounds scheme: that's what it's for)
    const QVariantMap access = accessibility();
    if (access.value(QStringLiteral("soundSentry")).toBool() || access.value(QStringLiteral("showSounds")).toBool()) {
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.wayland95.Accessibility"),
                                                           QStringLiteral("/org/wayland95/Accessibility"),
                                                           QStringLiteral("org.wayland95.Accessibility"), QStringLiteral("SoundPlayed"));
        call << event;
        QDBusConnection::sessionBus().asyncCall(call);
    }
    playFile(m_sounds.value(event));
}

QString Wayland95Settings::toneFile(const QString &tone)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wayland95/tones");
    const QString path = dir + QLatin1Char('/') + tone + QStringLiteral(".wav");
    if (QFile::exists(path))
        return path;
    QList<std::pair<double, int>> notes; // frequency, milliseconds
    if (tone == QLatin1String("on")) notes = { { 600, 70 }, { 900, 90 } };
    else if (tone == QLatin1String("off")) notes = { { 900, 70 }, { 600, 90 } };
    else if (tone == QLatin1String("high")) notes = { { 880, 110 } };
    else if (tone == QLatin1String("low")) notes = { { 440, 110 } };
    else notes = { { 1400, 12 } };
    const int rate = 22050;
    QByteArray pcm;
    for (const auto &[freq, ms] : notes) {
        const int n = rate * ms / 1000;
        for (int i = 0; i < n; ++i) {
            // a short fade in and out, so it doesn't click
            const double env = qMin(1.0, qMin(i, n - i) / (rate * 0.004));
            const qint16 v = qint16(9000 * env * std::sin(2 * M_PI * freq * i / rate));
            pcm.append(char(v & 0xff)).append(char((v >> 8) & 0xff));
        }
    }
    QByteArray wav;
    auto u32 = [&wav](quint32 v) { for (int i = 0; i < 4; ++i) wav.append(char((v >> (8 * i)) & 0xff)); };
    auto u16 = [&wav](quint16 v) { wav.append(char(v & 0xff)).append(char(v >> 8)); };
    wav += "RIFF"; u32(36 + pcm.size()); wav += "WAVEfmt ";
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    wav += "data"; u32(pcm.size()); wav += pcm;
    QDir().mkpath(dir);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(wav);
    return path;
}
