#pragma once

// The shell's view of its desktop session: which compositor it's on (for
// Start > Help), and ending the desktop (Start > Shut Down > "Close all
// programs and exit Wayland 95"). Exposed to QML as the singleton
// `ShellSession`.

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class ShellSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString socketName READ socketName CONSTANT)
    Q_PROPERTY(QString x11Display READ x11Display CONSTANT)

public:
    explicit ShellSession(QObject *parent = nullptr) : QObject(parent) {}

    QString socketName() const { return qEnvironmentVariable("WAYLAND_DISPLAY"); }
    QString x11Display() const { return qEnvironmentVariable("DISPLAY"); }

    // ends the desktop: asks wayland95-compositor (which started the shell
    // and told it its process id) to stop; the session ends with it
    Q_INVOKABLE void exitDesktop();
};
