#pragma once

// The shell's view of its desktop session: which compositor it's on (for
// Start > Help), and ending the desktop (Start > Shut Down > "Close all
// programs and exit Wayland 95"). Exposed to QML as the singleton
// `ShellSession`.

#include <QObject>
#include <QString>
#include <QJSValue>
#include <QtQml/qqmlregistration.h>

class ShellSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString socketName READ socketName CONSTANT)
    Q_PROPERTY(QString x11Display READ x11Display CONSTANT)
    // a minute, in ms, for the idle settings (screen saver, display off,
    // sleep), which are in minutes. The tests shorten it
    // (WAYLAND95_TEST_MINUTE_MS, only with WAYLAND95_NO_POWER_ACTIONS).
    Q_PROPERTY(int minuteMs READ minuteMs CONSTANT)
    // started as the login session's desktop (--desktop-session)
    Q_PROPERTY(bool session READ session CONSTANT)
    // started again by the compositor after the shell went away (--restarted)
    Q_PROPERTY(bool restarted READ restarted CONSTANT)
    // the main display's name (the compositor says: Control Panel > Display)
    Q_PROPERTY(QString primaryOutput READ primaryOutput NOTIFY primaryOutputChanged)

public:
    explicit ShellSession(QObject *parent = nullptr);
    QString primaryOutput() const;
    // The main display among Qt.application.screens (the first, if the
    // compositor hasn't said or it isn't there), for a panel's `screen`:
    //   screen: ShellSession.mainScreen(Qt.application.screens, ShellSession.primaryOutput)
    Q_INVOKABLE QObject *mainScreen(const QJSValue &screens, const QString &name) const;
    // whether a screen (one of Qt.application.screens) is the main one
    Q_INVOKABLE bool isMainScreen(QObject *screen, const QJSValue &screens, const QString &name) const
    {
        return screen && screen == mainScreen(screens, name);
    }

    QString socketName() const { return qEnvironmentVariable("WAYLAND_DISPLAY"); }
    QString x11Display() const { return qEnvironmentVariable("DISPLAY"); }
    int minuteMs() const
    {
        bool ok = false;
        const int test = qEnvironmentVariableIntValue("WAYLAND95_TEST_MINUTE_MS", &ok);
        return ok && test > 0 && qEnvironmentVariableIsSet("WAYLAND95_NO_POWER_ACTIONS") ? test : 60000;
    }
    bool session() const;
    bool restarted() const;

    // ends the desktop: asks wayland95-compositor (which started the shell
    // and told it its process id) to stop; the session ends with it
    Q_INVOKABLE void exitDesktop();

Q_SIGNALS:
    void primaryOutputChanged();
};
