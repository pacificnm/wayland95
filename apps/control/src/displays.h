#pragma once

// Control Panel > Display > Settings: the displays (monitors) and their
// settings. wayland95-compositor says what there is and what each can do
// ($XDG_RUNTIME_DIR/wayland95-displays-SOCKET.ini, rewritten whenever
// anything changes), and follows ~/.config/wayland95/displays.ini, which
// apply() writes (compositor/displays.c has the format).
//
// Exposed to QML as the singleton `Displays`.

#include <QFileSystemWatcher>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Displays : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // false when the desktop isn't wayland95-compositor (nothing to set)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // [{ name, label, description, enabled, primary, width, height, refresh
    //    (mHz), scale, transform (degrees), x, y, logicalWidth,
    //    logicalHeight, adaptiveSyncSupported, adaptiveSync,
    //    modes: [{ width, height, refresh, preferred }] (none: any size) }]
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)

public:
    explicit Displays(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QVariantList outputs() const { return m_outputs; }

    // Writes the settings ([{ name, enabled, primary, width, height,
    // refresh, scale, transform, x, y, adaptiveSync }]); the compositor
    // applies them at once. What was there before is kept for revert().
    Q_INVOKABLE void apply(const QVariantList &settings);
    // back to what was there before the last apply()
    Q_INVOKABLE void revert();

Q_SIGNALS:
    void changed();

private:
    void reload();
    QString statePath() const;
    QString configPath() const;

    QFileSystemWatcher m_watcher;
    QVariantList m_outputs;
    bool m_available = false;
    QByteArray m_before;      // displays.ini before the last apply()
    bool m_hadBefore = false; // (there was one)
};
