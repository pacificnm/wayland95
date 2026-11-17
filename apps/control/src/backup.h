#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// Backup: the settings behind Control Panel > Backup, and running its
// engine (session/wayland95-backup, installed as `wayland95-backup`).
//
// The settings are ~/.config/wayland95/backup.conf (KEY=VALUE lines the
// engine reads); the schedule is one line in your crontab, marked
// "# wayland95-backup" (the rest of your crontab is left as it is). What
// the last backup did is the engine's status file, watched for changes.
//
// Exposed to QML as the singleton `Backup`.
class BackupManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Backup)
    QML_SINGLETON

    // { driveUuid, driveName, destDir, keep, schedule (off/hourly/daily/
    //   weekly), time ("21:00"), day (0 Sunday..6), whenPlugged, folders,
    //   excludes, configured }
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    // { time (ms), result (ok/failed/skipped), message, files, bytes,
    //   folder, lastOk (ms), lastOkFolder }; {} before the first
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int percent READ percent NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString engine READ engine CONSTANT)
    Q_PROPERTY(QString defaultDestDir READ defaultDestDir CONSTANT)

public:
    explicit BackupManager(QObject *parent = nullptr);
    ~BackupManager() override;

    QVariantMap settings() const { return m_settings; }
    QVariantMap status() const { return m_status; }
    // a backup of ours (Back Up Now) or one cron or the desktop started
    bool running() const { return m_process != nullptr || m_backgroundPid > 0; }
    int percent() const { return m_percent; }
    QString progressText() const { return m_progressText; }
    QString engine() const;
    QString defaultDestDir() const;

    // writes the settings and the crontab line: "" or what went wrong
    Q_INVOKABLE QString save(const QVariantMap &settings);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void backUpNow();
    Q_INVOKABLE void stop();
    // the drive belongs to root: make the backups' own folder on it (the
    // status's "fix") yours, with an administrator's password (pkexec);
    // nothing else on the drive changes. Ends with setUpFinished.
    Q_INVOKABLE void setUpDrive();
    // the crontab line the settings make ("" when off), for the applet
    Q_INVOKABLE QString cronLine(const QVariantMap &settings) const;
    Q_INVOKABLE QString logPath() const;
    Q_INVOKABLE bool openFolder(const QString &path) const;
    Q_INVOKABLE bool openLog() const;
    Q_INVOKABLE QString homePath() const;
    Q_INVOKABLE bool isDir(const QString &path) const;

signals:
    void settingsChanged();
    void statusChanged();
    void runningChanged();
    void progressChanged();
    // a Back Up Now ended: ok, and what it said last
    void finished(bool ok, const QString &message);
    void setUpFinished(bool ok, const QString &message);

private:
    QString confPath() const;
    QString stateDir() const;
    void loadSettings();
    void loadStatus();
    QString writeCrontab(const QVariantMap &settings);

    QVariantMap m_settings;
    QVariantMap m_status;
    QPointer<QProcess> m_process;
    int m_percent = -1;
    QString m_progressText;
    QFileSystemWatcher m_watcher;
    // a backup running that something else started (the status's pid):
    // its progress is read from rsync's output every second
    qint64 m_backgroundPid = 0;
    QTimer m_poll;
    void readProgress();
};
