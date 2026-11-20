#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// Other programs' look in the Wayland 95 session: GTK programs (and
// Chromium, Firefox, Electron programs, which take their fonts from GTK) get
// Wayland 95's theme, fonts, icons and pointer, and web browsers use the
// Win95 title bar instead of drawing their own.
//
//  - GTK: the theme "Wayland95" is written into ~/.local/share/themes with
//    the colour scheme from Control Panel > Display > Appearance (and again
//    when it changes); GNOME's settings (org.gnome.desktop.interface, which
//    GTK reads on Wayland) name it, Liberation Sans 8.25 (the desktop's 11
//    pixels), Adwaita icons and the compositor's pointer. KDE's Plasma puts
//    its own back when you log in there.
//  - Browsers: Chromium-family profiles get "Use system title bar and
//    borders", Firefox profiles the Title Bar box, once each (at a login
//    when the browser isn't running); changing it back in the browser
//    sticks. Firefox and plain GTK windows then ask the compositor for the
//    frame over KDE's server-decoration protocol, Chromium over
//    xdg-decoration.
//
// Control Panel > Display > Appearance > "Other programs use this look"
// (Appearance/OtherPrograms) turns it off: GNOME's settings go back to their
// defaults, and the browsers are left as they are.
class ProgramLook : public QObject
{
    Q_OBJECT
public:
    explicit ProgramLook(QObject *parent = nullptr);

    // at login (the session's shell): everything above
    void startSession();

    // the browsers' profiles (for the tests too): returns what it changed
    static QStringList useSystemTitleBars(const QString &home, const QString &configHome,
                                          const QString &stateDir);

private:
    void settingsChanged();
    bool enabled() const;
    void writeGtkTheme();
    void applyGtkSettings();
    void resetGtkSettings();

    QString m_colors; // the scheme the theme was last written with
    bool m_applied = false;
};
