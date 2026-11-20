#include "programlook.h"
#include "wayland95settings.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QtDebug>

#include <cerrno>
#include <csignal>

// GLib's headers use "signals" as a name; Qt makes it a keyword
#pragma push_macro("signals")
#undef signals
#include <gio/gio.h>
#pragma pop_macro("signals")

namespace {

const char *const kTheme = "Wayland95";
// the desktop's font (Theme.qml: Liberation Sans, 11 pixels = 8.25 points)
const char *const kFont = "Liberation Sans 8.25";
const char *const kDocumentFont = "Liberation Sans 10";
const char *const kMonospaceFont = "Liberation Mono 10";

// ---- GNOME's settings (GSettings), which GTK reads on Wayland -------------

// a schema's settings, or null where it isn't installed (g_settings_new
// would abort)
GSettings *settingsFor(const char *schemaId, GSettingsSchema **schemaOut)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source, schemaId, TRUE) : nullptr;
    if (!schema)
        return nullptr;
    *schemaOut = schema;
    return g_settings_new(schemaId);
}

struct Gsettings
{
    explicit Gsettings(const char *schemaId) { settings = settingsFor(schemaId, &schema); }
    ~Gsettings()
    {
        if (settings) {
            g_settings_sync();
            g_object_unref(settings);
        }
        if (schema)
            g_settings_schema_unref(schema);
    }
    bool has(const char *key) const { return settings && g_settings_schema_has_key(schema, key); }
    void set(const char *key, const char *value)
    {
        if (has(key))
            g_settings_set_string(settings, key, value);
    }
    void set(const char *key, int value)
    {
        if (has(key))
            g_settings_set_int(settings, key, value);
    }
    void set(const char *key, double value)
    {
        if (has(key))
            g_settings_set_double(settings, key, value);
    }
    void reset(const char *key)
    {
        if (has(key))
            g_settings_reset(settings, key);
    }
    QString string(const char *key) const
    {
        if (!has(key))
            return {};
        gchar *v = g_settings_get_string(settings, key);
        const QString s = QString::fromUtf8(v);
        g_free(v);
        return s;
    }
    GSettings *settings = nullptr;
    GSettingsSchema *schema = nullptr;
};

const char *const kInterfaceKeys[] = {
    "gtk-theme", "icon-theme", "cursor-theme", "cursor-size", "font-name", "document-font-name",
    "monospace-font-name", "text-scaling-factor", "color-scheme", "font-antialiasing", "font-hinting",
};

// ---- the browsers -----------------------------------------------------------

bool processRunning(qint64 pid)
{
    return pid > 0 && (::kill(pid_t(pid), 0) == 0 || errno == EPERM);
}

// Chromium's SingletonLock -> "HOST-PID", Firefox's lock -> "ADDRESS:+PID":
// a link while the browser runs
bool lockedBy(const QString &lockPath, QChar separator)
{
    const QFileInfo lock(lockPath);
    if (!lock.isSymLink())
        return false;
    const QString target = lock.readSymLink();
    bool ok = false;
    const qint64 pid = target.section(separator, -1).toLongLong(&ok);
    return !ok || processRunning(pid); // (unreadable: assume it runs)
}

bool chromiumProfile(const QString &profile)
{
    QFile file(profile + QStringLiteral("/Preferences"));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning().noquote() << "Program look: can't read" << file.fileName() << error.errorString();
        return false;
    }
    QJsonObject root = doc.object();
    QJsonObject browser = root.value(QStringLiteral("browser")).toObject();
    // "Use system title bar and borders"
    browser.insert(QStringLiteral("custom_chrome_frame"), false);
    root.insert(QStringLiteral("browser"), browser);
    QSaveFile out(file.fileName());
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return out.commit();
}

bool firefoxProfile(const QString &profile)
{
    // the Title Bar box (Customize Toolbar): browser.tabs.inTitlebar 0
    QFile file(profile + QStringLiteral("/prefs.js"));
    QStringList lines;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
            if (!line.startsWith(QLatin1String("user_pref(\"browser.tabs.inTitlebar\"")))
                lines << line;
        }
        file.close();
        while (!lines.isEmpty() && lines.last().isEmpty())
            lines.removeLast();
    }
    lines << QStringLiteral("user_pref(\"browser.tabs.inTitlebar\", 0);") << QString();
    QSaveFile out(file.fileName());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    out.write(lines.join(QLatin1Char('\n')).toUtf8());
    return out.commit();
}

} // namespace

ProgramLook::ProgramLook(QObject *parent)
    : QObject(parent)
{
}

bool ProgramLook::enabled() const
{
    return Wayland95Settings::instance()->otherProgramsLook();
}

void ProgramLook::startSession()
{
    // the tests' desktops run inside your session: never your settings
    if (qEnvironmentVariableIsSet("WAYLAND95_NO_SYSTEM_AGENTS"))
        qputenv("GSETTINGS_BACKEND", "keyfile");
    connect(Wayland95Settings::instance(), &Wayland95Settings::changed, this, &ProgramLook::settingsChanged,
            Qt::UniqueConnection);
    if (!enabled()) {
        // turned off: if GTK still has ours, back to GNOME's defaults
        if (Gsettings("org.gnome.desktop.interface").string("gtk-theme") == QLatin1String(kTheme))
            resetGtkSettings();
        return;
    }
    writeGtkTheme();
    applyGtkSettings();
    const QString home = QDir::homePath();
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString state = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
        + QStringLiteral("/wayland95");
    useSystemTitleBars(home, config, state);
}

void ProgramLook::settingsChanged()
{
    if (!enabled()) {
        if (m_applied)
            resetGtkSettings();
        return;
    }
    QString colors;
    const QVariantMap map = Wayland95Settings::instance()->colors();
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        colors += it.key() + it.value().value<QColor>().name();
    if (m_applied && colors == m_colors)
        return;
    const bool wasApplied = m_applied;
    writeGtkTheme();
    if (wasApplied) {
        // GTK reads a theme's file again only when the theme's name changes:
        // away and back (apart, so each is seen as a change)
        Gsettings("org.gnome.desktop.interface").set("gtk-theme", "Adwaita");
        QTimer::singleShot(400, this, [this] {
            if (enabled())
                Gsettings("org.gnome.desktop.interface").set("gtk-theme", kTheme);
        });
    } else {
        applyGtkSettings(); // turned back on
    }
}

void ProgramLook::writeGtkTheme()
{
    const QVariantMap c = Wayland95Settings::instance()->colors();
    auto color = [&](const char *key) { return c.value(QLatin1String(key)).value<QColor>().name(); };
    const struct { const char *name, *key; } names[] = {
        {"w95_face", "face"}, {"w95_light", "light"}, {"w95_highlight", "highlight"},
        {"w95_shadow", "shadow"}, {"w95_dark", "dark"}, {"w95_window", "window"},
        {"w95_window_text", "windowText"}, {"w95_menu", "menu"}, {"w95_menu_text", "menuText"},
        {"w95_selection", "selection"}, {"w95_selection_text", "selectionText"},
        {"w95_title", "activeTitle"}, {"w95_title2", "activeTitle2"}, {"w95_title_text", "activeTitleText"},
        {"w95_ititle", "inactiveTitle"}, {"w95_ititle2", "inactiveTitle2"}, {"w95_ititle_text", "inactiveTitleText"},
    };
    QString defines;
    m_colors.clear();
    for (const auto &n : names)
        defines += QStringLiteral("@define-color %1 %2;\n").arg(QLatin1String(n.name), color(n.key));
    for (auto it = c.cbegin(); it != c.cend(); ++it)
        m_colors += it.key() + it.value().value<QColor>().name();

    QFile templ(QStringLiteral(":/programlook/gtk.css"));
    templ.open(QIODevice::ReadOnly);
    const QString body = QString::fromUtf8(templ.readAll());
    const QString header = QStringLiteral("/* Written by Wayland 95 from its colour scheme (Control Panel >\n"
                                          "   Display > Appearance); it's written again when that changes. */\n");
    // GTK 3: its scroll bars get Win95's arrow buttons
    const QString gtk3Extra = QStringLiteral(
        "\nscrollbar {\n    -GtkScrollbar-has-backward-stepper: true;\n"
        "    -GtkScrollbar-has-forward-stepper: true;\n}\n");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/themes/") + QLatin1String(kTheme);
    const struct { const char *sub, *import; QString extra; } versions[] = {
        {"gtk-3.0", "resource:///org/gtk/libgtk/theme/Adwaita/gtk-contained.css", gtk3Extra},
        {"gtk-4.0", "resource:///org/gtk/libgtk/theme/Default/Default-light.css", QString()},
    };
    for (const auto &v : versions) {
        QDir().mkpath(dir + QLatin1Char('/') + QLatin1String(v.sub));
        QSaveFile out(dir + QLatin1Char('/') + QLatin1String(v.sub) + QStringLiteral("/gtk.css"));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning().noquote() << "Program look: can't write" << out.fileName();
            continue;
        }
        out.write((header + QStringLiteral("@import url(\"%1\");\n\n").arg(QLatin1String(v.import)) + defines
                   + QLatin1Char('\n') + body + v.extra).toUtf8());
        out.commit();
    }
    QSaveFile index(dir + QStringLiteral("/index.theme"));
    if (index.open(QIODevice::WriteOnly | QIODevice::Text)) {
        index.write("[Desktop Entry]\nType=X-GNOME-Metatheme\nName=Wayland95\n"
                    "Comment=Wayland 95's look, written by the desktop from its colour scheme\n"
                    "Encoding=UTF-8\n\n[X-GNOME-Metatheme]\nGtkTheme=Wayland95\nIconTheme=Adwaita\n");
        index.commit();
    }
    qInfo().noquote() << "Program look: GTK theme written to" << dir;
}

void ProgramLook::applyGtkSettings()
{
    Gsettings gtk("org.gnome.desktop.interface");
    if (!gtk.settings) {
        qInfo("Program look: GNOME's settings aren't installed (gsettings-desktop-schemas); GTK keeps its own");
        return;
    }
    gtk.set("gtk-theme", kTheme);
    if (QFileInfo::exists(QStringLiteral("/usr/share/icons/Adwaita")))
        gtk.set("icon-theme", "Adwaita");
    // the compositor's pointer (wlroots: XCURSOR_THEME, else "default")
    gtk.set("cursor-theme", qEnvironmentVariable("XCURSOR_THEME", QStringLiteral("default")).toUtf8().constData());
    gtk.set("cursor-size", qEnvironmentVariableIntValue("XCURSOR_SIZE") > 0 ? qEnvironmentVariableIntValue("XCURSOR_SIZE") : 24);
    gtk.set("font-name", kFont);
    gtk.set("document-font-name", kDocumentFont);
    gtk.set("monospace-font-name", kMonospaceFont);
    gtk.set("text-scaling-factor", 1.0);
    gtk.set("color-scheme", "default");
    gtk.set("font-antialiasing", "grayscale");
    gtk.set("font-hinting", "slight");
    // header bars' buttons in Win95's order, on the right
    Gsettings wm("org.gnome.desktop.wm.preferences");
    wm.set("button-layout", ":minimize,maximize,close");
    m_applied = true;
    qInfo().noquote() << "Program look: GTK uses" << kTheme << "and" << kFont;
}

void ProgramLook::resetGtkSettings()
{
    Gsettings gtk("org.gnome.desktop.interface");
    for (const char *key : kInterfaceKeys)
        gtk.reset(key);
    Gsettings("org.gnome.desktop.wm.preferences").reset("button-layout");
    m_applied = false;
    qInfo("Program look: turned off; GTK's settings are back to their defaults");
}

QStringList ProgramLook::useSystemTitleBars(const QString &home, const QString &configHome, const QString &stateDir)
{
    // each profile once: changing it back in the browser sticks
    const QString doneFile = stateDir + QStringLiteral("/browser-title-bars");
    QStringList done;
    {
        QFile f(doneFile);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            done = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    QStringList changed;
    const int before = done.size();

    // Chromium and the browsers built on it
    const QStringList chromiumDirs{
        configHome + QStringLiteral("/chromium"),
        configHome + QStringLiteral("/google-chrome"),
        configHome + QStringLiteral("/google-chrome-beta"),
        configHome + QStringLiteral("/google-chrome-unstable"),
        configHome + QStringLiteral("/BraveSoftware/Brave-Browser"),
        configHome + QStringLiteral("/microsoft-edge"),
        configHome + QStringLiteral("/vivaldi"),
        home + QStringLiteral("/.var/app/org.chromium.Chromium/config/chromium"),
    };
    for (const QString &dir : chromiumDirs) {
        if (!QFileInfo(dir).isDir())
            continue;
        const bool running = lockedBy(dir + QStringLiteral("/SingletonLock"), QLatin1Char('-'));
        for (const QFileInfo &p : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString profile = p.absoluteFilePath();
            if (done.contains(profile) || !QFileInfo::exists(profile + QStringLiteral("/Preferences")))
                continue;
            if (running) {
                qInfo().noquote() << "Program look: the browser is running; next time:" << profile;
                continue;
            }
            if (chromiumProfile(profile)) {
                done << profile;
                changed << profile;
            }
        }
    }

    // Firefox
    const QStringList firefoxDirs{
        home + QStringLiteral("/.mozilla/firefox"),
        configHome + QStringLiteral("/mozilla/firefox"),
        home + QStringLiteral("/.var/app/org.mozilla.firefox/.mozilla/firefox"),
    };
    for (const QString &dir : firefoxDirs) {
        if (!QFileInfo(dir).isDir())
            continue;
        for (const QFileInfo &p : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString profile = p.absoluteFilePath();
            if (done.contains(profile) || !QFileInfo::exists(profile + QStringLiteral("/prefs.js")))
                continue;
            if (lockedBy(profile + QStringLiteral("/lock"), QLatin1Char('+'))) {
                qInfo().noquote() << "Program look: Firefox is running; next time:" << profile;
                continue;
            }
            if (firefoxProfile(profile)) {
                done << profile;
                changed << profile;
            }
        }
    }

    if (done.size() != before) {
        QDir().mkpath(stateDir);
        QSaveFile f(doneFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write((done.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
            f.commit();
        }
    }
    for (const QString &p : changed)
        qInfo().noquote() << "Program look: the Win95 title bar for" << p;
    return changed;
}
