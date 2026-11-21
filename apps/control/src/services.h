#pragma once

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

class PkTransaction;

// Services: servers and programming languages that can be turned on and
// off from the Control Panel. Turning one on installs what it needs first
// (PackageKit), then starts it and has it start with the computer
// (systemd). System settings are changed by wayland95-services, run as
// root through pkexec; everything else runs as the user.
//
// Exposed to QML as the singleton `Services`.
class ServiceManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Services)
    QML_SINGLETON

    // [{ id, name, kind ("server"/"language"), description, packages,
    //    installed, enabled, active, version, status, settings }]
    Q_PROPERTY(QVariantList services READ services NOTIFY servicesChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY servicesChanged)
    // the install / start / stop running now
    Q_PROPERTY(bool busy READ busy NOTIFY taskChanged)
    Q_PROPERTY(QString task READ task NOTIFY taskChanged)
    Q_PROPERTY(QString status READ status NOTIFY taskChanged)
    Q_PROPERTY(int percent READ percent NOTIFY taskChanged)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString homePath READ homePath CONSTANT)

public:
    explicit ServiceManager(QObject *parent = nullptr);

    QVariantList services() const { return m_services; }
    bool loading() const { return m_loading > 0; }
    bool busy() const { return m_busy; }
    QString task() const { return m_task; }
    QString status() const { return m_status; }
    int percent() const { return m_percent; }
    QString userName() const;
    QString homePath() const;

    // looks again at what's installed and running
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap service(const QString &id) const;

    // on: installs what's missing, then starts it (and at every boot);
    // off: servers stop (and stay installed), languages are removed
    Q_INVOKABLE void turnOn(const QString &id);
    Q_INVOKABLE void turnOff(const QString &id);
    // start / stop / restart a server that's installed
    Q_INVOKABLE void control(const QString &id, const QString &action);
    // extra packages (Python's components): installs or removes them
    Q_INVOKABLE void changePackages(const QString &title, const QStringList &install, const QStringList &remove);
    // wayland95-services ARGS as root, as a task (its progress shown like an
    // install's): Ollama's update and removal
    Q_INVOKABLE void adminTask(const QString &title, const QStringList &args, const QString &status, const QString &doneMessage);
    // which of these packages are installed: answers done([names])
    Q_INVOKABLE void installedPackages(const QStringList &names, const QJSValue &done);

    // wayland95-services ARGS, as root (password prompt), with `input` on
    // its stdin: done(ok, output)
    Q_INVOKABLE void admin(const QStringList &args, const QString &input, const QJSValue &done);
    // a program as the user: done(exitCode, stdout, stderr); exitCode -1
    // if it couldn't start
    Q_INVOKABLE void run(const QString &program, const QStringList &args, const QString &input,
                         const QJSValue &done, const QString &workdir = QString());
    // a Terminal window in `workdir`, running `command` if given
    Q_INVOKABLE void openTerminal(const QString &workdir, const QStringList &command = {});

    // the user's own files (~/.ssh/authorized_keys, ~/.npmrc...)
    Q_INVOKABLE QString readFile(const QString &path) const;
    Q_INVOKABLE bool writeFile(const QString &path, const QString &text, bool privateFile = false);
    Q_INVOKABLE bool exists(const QString &path) const;
    Q_INVOKABLE QStringList listDirs(const QString &path) const;
    Q_INVOKABLE bool removeDir(const QString &path);

    // this computer's network addresses, for "connect to ..."
    Q_INVOKABLE QStringList addresses() const;
    Q_INVOKABLE QString hostName() const;
    // SSH's settings as sshd will read them: Port, PasswordAuthentication,
    // PermitRootLogin, X11Forwarding, PubkeyAuthentication, AllowUsers
    Q_INVOKABLE QVariantMap sshConfig() const;
    // WireGuard's tunnels that are up or start with the computer:
    // [{ name, enabled, active }] (the rest: wayland95-services wg-list)
    Q_INVOKABLE QVariantList wireguardTunnels() const;
    // PostgreSQL: version, port, listen ("localhost"/"all"), max_connections,
    // remote (bool); empty if it isn't installed
    Q_INVOKABLE QVariantMap postgresConfig() const;

    // MySQL (MariaDB): version, port, listen ("localhost"/"all"),
    // max_connections, as the server reads its option files; empty if it
    // isn't installed
    Q_INVOKABLE QVariantMap mysqlConfig() const;

    // SQLite: the database files in a folder: [{ path, name, size, modified }]
    Q_INVOKABLE QVariantList sqliteFiles(const QString &folder) const;
    // what's in one (opened read-only): { tables: [{ name, type, rows }], error }
    Q_INVOKABLE QVariantMap sqliteInfo(const QString &path) const;
    // a new, empty database: "" or what went wrong
    Q_INVOKABLE QString sqliteCreate(const QString &path);
    // to the Recycle Bin (the desktop's trash)
    Q_INVOKABLE bool moveToTrash(const QString &path);

    // nginx: version, listen ("localhost"/"all"), port, root (the website's
    // folder), autoindex, ours (its site is the one in use), rootExists,
    // rootWritable (by you); empty if it isn't installed
    Q_INVOKABLE QVariantMap nginxConfig() const;
    // ~/Website, made with a first page if it isn't there: its path
    Q_INVOKABLE QString makeWebsite();
    Q_INVOKABLE void openUrl(const QString &url) const;

    // Samba: version, workgroup, description (its server string), ours (our
    // settings file is included), shares: [{ name, path, comment, access
    // ("read"/"full"), who ("me": with the owner's Samba password, or
    // "anyone"), owner, mine }] (the ones Services made); empty if Samba
    // isn't installed
    Q_INVOKABLE QVariantMap sambaConfig() const;

    // The login screen (Passwords > Log On), as SDDM will read its settings:
    // displayManager ("sddm", "gdm3", "lightdm", ... or ""), installed (our
    // theme is there), current (the theme SDDM uses; "" for its default),
    // currentName (that theme's own name), ours (it's Wayland 95's), other /
    // otherName / otherPreview (the theme SDDM would use without ours),
    // ourFile (/etc/sddm.conf.d/wayland95.conf is there), overriddenBy (the
    // file that wins over ours, if one does), preview (our theme's picture)
    Q_INVOKABLE QVariantMap loginScreen() const;

signals:
    void servicesChanged();
    void taskChanged();
    void taskFinished(bool ok, const QString &message);

private:
    struct Def {
        QString id, name, kind, description, unit, settings;
        QStringList packages;   // all installed: it's installed
        QStringList removable;  // what turning a language off removes
        QStringList version;    // program and arguments printing its version
    };
    const Def *def(const QString &id) const;

    using Step = std::function<void()>;
    void runTask(const QString &title, const QList<Step> &steps, const QString &doneMessage);
    void nextStep();
    void taskFailed(const QString &error);
    void setStatus(const QString &status, int percent);
    PkTransaction *pk(const QString &method, const QVariantList &args, bool interactive);
    Step installStep(const QStringList &packages);
    Step removeStep(const QStringList &packages);
    Step adminStep(const QStringList &args, const QString &status);

    void publish();
    QString helperPath() const;

    QList<Def> m_defs;
    QVariantList m_services;
    QSet<QString> m_installed;          // package names
    QHash<QString, QVariantMap> m_unitState; // unit -> ActiveState, UnitFileState
    QHash<QString, QString> m_versions; // service id -> version
    int m_loading = 0;

    bool m_busy = false;
    QString m_task, m_status, m_doneMessage;
    int m_percent = -1;
    QList<Step> m_steps;
    QPointer<PkTransaction> m_current;
};
