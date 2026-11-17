#pragma once

#include <QJSValue>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

class QDBusMessage;
class QQmlEngine;
class QJSEngine;

// Drives you plug in (USB sticks and disks, memory cards), through UDisks2
// over D-Bus: listed whether or not they're mounted, mounted when opened
// (as you: UDisks2 and polkit allow that for removable drives without a
// password), and unmounted and powered off by Eject. Changes (a drive
// plugged in or pulled out, mounted elsewhere) show up live.
//
// The automated tests talk to a stand-in UDisks2 on the test's own session
// bus (tests/fakes/udisks.py) instead of the system's, as the other agents
// do (WAYLAND95_NO_SYSTEM_AGENTS + WAYLAND95_TEST_AGENTS_ON_SESSION_BUS).
//
// Exposed to QML as the singleton `Drives` (one shared instance).
class Drives : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Drives)
    QML_SINGLETON

    // UDisks2 answered
    Q_PROPERTY(bool available READ available NOTIFY drivesChanged)
    // [{ id (the filesystem's UDisks2 object), name, device, size,
    //    sizeText, fsType, uuid, mounted, mountPath, encrypted, drive (its
    //    drive's object), driveName, canEject }], by name
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)

public:
    static Drives *instance();
    static Drives *create(QQmlEngine *, QJSEngine *);

    bool available() const { return m_available; }
    QVariantList drives() const { return m_drives; }
    QVariantMap drive(const QString &id) const;
    // the one mounted at path (or a folder inside it), or {}
    QVariantMap driveAt(const QString &path) const;

    using Done = std::function<void(bool ok, const QString &pathOrError)>;
    // done(ok, the folder it's mounted at | what went wrong)
    void mount(const QString &id, const Done &done);
    void unmount(const QString &id, const Done &done);
    // unmounts every filesystem on its drive, then powers it off (or
    // ejects the medium): done(ok, "" | what went wrong)
    void eject(const QString &id, const Done &done);

    Q_INVOKABLE void mount(const QString &id, const QJSValue &done);
    Q_INVOKABLE void unmount(const QString &id, const QJSValue &done);
    Q_INVOKABLE void eject(const QString &id, const QJSValue &done);
    Q_INVOKABLE void refresh();

    static QString sizeText(qint64 bytes);

signals:
    void drivesChanged();
    // plugged in after the list was first read (not the ones there already)
    void driveAdded(const QVariantMap &drive);
    // gone; "ejected": by Eject here (otherwise pulled out)
    void driveRemoved(const QVariantMap &drive);

private slots:
    // a filesystem mounted or unmounted: applied at once (not with the
    // reload), so a drive ejected anywhere is unmounted here before it goes
    void propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated,
                           const QDBusMessage &message);

private:
    explicit Drives(QObject *parent = nullptr);
    void load();
    void call(const QString &path, const QString &interface, const QString &method, const QVariantMap &options,
              const std::function<void(bool ok, const QVariant &result, const QString &error)> &done);
    static QString explain(const QString &errorName, const QString &message, const QString &name);
    std::function<void(bool, const QString &)> wrap(const QJSValue &done);

    bool m_available = false;
    bool m_loaded = false;
    QVariantList m_drives;
    QTimer m_reload;
    QSet<QString> m_ejected; // drives (their objects) ejected here: gone on purpose
};
