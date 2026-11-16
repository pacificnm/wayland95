#pragma once

// The screen saver's D-Bus side, for win95shell:
//  - org.freedesktop.ScreenSaver: browsers and video players keep the
//    screen saver away while something plays (Inhibit/UnInhibit; a program
//    that quits or crashes no longer inhibits). Offered only when no other
//    desktop already does.
//  - org.wayland95.ScreenSaver: Control Panel > Display's Preview button.
// Exposed to QML as the singleton `ScreenSaverService`; Shell.qml runs the
// screen saver itself and tells it whether it's showing (`active`).

#include <QDBusContext>
#include <QHash>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class QDBusServiceWatcher;

class ScreenSaverService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool inhibited READ inhibited NOTIFY inhibitedChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit ScreenSaverService(QObject *parent = nullptr);

    bool inhibited() const { return !m_inhibitors.isEmpty(); }
    bool active() const { return m_active; }
    void setActive(bool active);

    // for the D-Bus objects
    uint addInhibitor(const QString &owner, const QString &application, const QString &reason);
    void removeInhibitor(uint cookie);
    qint64 activeSince() const { return m_activeSince; }

Q_SIGNALS:
    void inhibitedChanged();
    void activeChanged();
    // Control Panel's Preview; a program's SetActive; SimulateUserActivity
    void previewRequested();
    void activateRequested(bool active);
    void activityReported();

private:
    QHash<uint, QString> m_inhibitors; // cookie -> the program's bus name
    uint m_nextCookie = 1;
    bool m_active = false;
    qint64 m_activeSince = 0;
    QDBusServiceWatcher *m_watcher = nullptr;
};
