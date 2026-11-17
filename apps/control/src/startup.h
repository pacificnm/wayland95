#pragma once

#include <QJSValue>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

// Start Up: what starts with the computer and with your session.
//
//  - Programs: XDG autostart entries (~/.config/autostart, and the
//    system's in XDG_CONFIG_DIRS/autostart, which are turned off for you by
//    a copy in ~/.config/autostart with Hidden=true, as the spec says).
//  - Services: systemd's services, the computer's (systemctl) and your own
//    (systemctl --user). systemctl runs as you; for the computer's it asks
//    polkit, which shows the desktop's password box.
//  - Boot: how long starting took (systemd-analyze), what took longest,
//    and whether the computer starts to the desktop or a text console.
//
// Exposed to QML as the singleton `Startup`.
class StartupManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Startup)
    QML_SINGLETON

    // [{ key (file name), name, comment, command, icon, enabled, system
    //    (one of the system's), own (a file of yours), here (meant for this
    //    desktop and installed), path }]
    Q_PROPERTY(QVariantList programs READ programs NOTIFY programsChanged)
    // [{ key/unit, name, description, file (enabled, disabled, static,
    //    masked...), active, sub, canEnable, enabled, important }]
    Q_PROPERTY(QVariantList systemUnits READ systemUnits NOTIFY unitsChanged)
    Q_PROPERTY(QVariantList userUnits READ userUnits NOTIFY unitsChanged)
    Q_PROPERTY(bool loadingSystem READ loadingSystem NOTIFY unitsChanged)
    Q_PROPERTY(bool loadingUser READ loadingUser NOTIFY unitsChanged)
    Q_PROPERTY(QString systemError READ systemError NOTIFY unitsChanged)
    Q_PROPERTY(QString userError READ userError NOTIFY unitsChanged)
    // a start / stop / enable... running now
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit StartupManager(QObject *parent = nullptr);

    QVariantList programs() const { return m_programs; }
    QVariantList systemUnits() const { return m_units[0]; }
    QVariantList userUnits() const { return m_units[1]; }
    bool loadingSystem() const { return m_loading[0] > 0; }
    bool loadingUser() const { return m_loading[1] > 0; }
    QString systemError() const { return m_error[0]; }
    QString userError() const { return m_error[1]; }
    bool busy() const { return m_busy > 0; }

    // --- programs (each answers "" or what went wrong)
    Q_INVOKABLE void refreshPrograms();
    Q_INVOKABLE QString setProgramEnabled(const QString &key, bool on);
    Q_INVOKABLE QString addProgram(const QString &name, const QString &command, const QString &icon = QString());
    // one of the installed programs (its desktop file ID), as it starts from the menu
    Q_INVOKABLE QString addInstalled(const QString &desktopId);
    Q_INVOKABLE QString editProgram(const QString &key, const QString &name, const QString &command);
    // yours only: the system's can just be turned off
    Q_INVOKABLE QString removeProgram(const QString &key);
    // [{ key (desktop file ID), name, icon, command }], by name
    Q_INVOKABLE QVariantList installedPrograms() const;

    // --- services
    Q_INVOKABLE void refreshUnits(bool user);
    // start, stop, restart, enable, disable, mask, unmask: done(ok, message)
    Q_INVOKABLE void unitAction(bool user, const QString &unit, const QString &action, const QJSValue &done);
    // systemctl status: done(text)
    Q_INVOKABLE void unitStatus(bool user, const QString &unit, const QJSValue &done);
    // the desktop, the login screen or the system itself depend on it
    Q_INVOKABLE static bool isImportant(const QString &unit);

    // --- boot: done({ summary, blame: [{ unit, time }], target })
    Q_INVOKABLE void bootInfo(const QJSValue &done);
    // graphical.target or multi-user.target: done(ok, message)
    Q_INVOKABLE void setDefaultTarget(const QString &target, const QJSValue &done);

signals:
    void programsChanged();
    void unitsChanged();
    void busyChanged();

private:
    using Done = std::function<void(int code, const QString &out, const QString &err)>;
    void run(const QString &program, const QStringList &args, const Done &done);
    QString autostartDir() const;
    QStringList systemAutostartDirs() const;
    QString writeEntry(const QString &file, const QList<QPair<QString, QString>> &set, const QStringList &remove,
                       const QString &copyFrom = QString());
    static QString failure(const QString &err, int code);

    QVariantList m_programs;
    QVariantList m_units[2];
    int m_loading[2] = {0, 0};
    QString m_error[2];
    int m_busy = 0;
    quint64 m_generation[2] = {0, 0};
};
