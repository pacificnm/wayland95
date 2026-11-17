#include "backup.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <csignal>
#include <unistd.h>

namespace {

const QString CronMark = QStringLiteral("# wayland95-backup");

QString program(const QString &name)
{
    // next to this program (a build tree) first, then the PATH
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(name);
    if (QFileInfo(beside).isExecutable())
        return beside;
    return QStandardPaths::findExecutable(name);
}

QString userName()
{
    const QString u = qEnvironmentVariable("USER");
    return u.isEmpty() ? QDir::home().dirName() : u;
}

} // namespace

BackupManager::BackupManager(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(stateDir());
    m_watcher.addPath(stateDir());
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &BackupManager::loadStatus);
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        loadStatus(); // (it may have ended without the folder telling)
        readProgress();
    });
    reload();
}

BackupManager::~BackupManager()
{
    stop();
}

QString BackupManager::confPath() const
{
    return qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + QStringLiteral("/.config")) + QStringLiteral("/wayland95/backup.conf");
}

QString BackupManager::stateDir() const
{
    return qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + QStringLiteral("/.local/state")) + QStringLiteral("/wayland95");
}

QString BackupManager::logPath() const
{
    return stateDir() + QStringLiteral("/backup.log");
}

QString BackupManager::engine() const
{
    return program(QStringLiteral("wayland95-backup"));
}

QString BackupManager::defaultDestDir() const
{
    return QStringLiteral("Wayland 95 Backups/%1@%2").arg(userName(), QHostInfo::localHostName());
}

QString BackupManager::homePath() const
{
    return QDir::homePath();
}

bool BackupManager::isDir(const QString &path) const
{
    return QFileInfo(path).isDir();
}

void BackupManager::reload()
{
    loadSettings();
    loadStatus();
}

void BackupManager::loadSettings()
{
    QVariantMap s;
    QStringList folders, excludes;
    QFile f(confPath());
    const bool configured = f.open(QIODevice::ReadOnly | QIODevice::Text);
    if (configured) {
        for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const QString key = line.left(eq), value = line.mid(eq + 1);
            if (key == QLatin1String("FOLDER")) folders << value;
            else if (key == QLatin1String("EXCLUDE")) excludes << value;
            else if (key == QLatin1String("DRIVE_UUID")) s[QStringLiteral("driveUuid")] = value;
            else if (key == QLatin1String("DRIVE_NAME")) s[QStringLiteral("driveName")] = value;
            else if (key == QLatin1String("DEST_DIR")) s[QStringLiteral("destDir")] = value;
            else if (key == QLatin1String("KEEP")) s[QStringLiteral("keep")] = value.toInt();
            else if (key == QLatin1String("SCHEDULE")) s[QStringLiteral("schedule")] = value;
            else if (key == QLatin1String("TIME")) s[QStringLiteral("time")] = value;
            else if (key == QLatin1String("DAY")) s[QStringLiteral("day")] = value.toInt();
            else if (key == QLatin1String("WHEN_PLUGGED")) s[QStringLiteral("whenPlugged")] = value != QLatin1String("no");
        }
    } else {
        // to start with: the usual folders that are there
        for (const char *d : {"Documents", "Pictures", "Desktop", "Music", "Videos"}) {
            const QString p = QDir::homePath() + QLatin1Char('/') + QLatin1String(d);
            if (QFileInfo(p).isDir())
                folders << p;
        }
        excludes << QStringLiteral(".cache/") << QStringLiteral("node_modules/") << QStringLiteral("*.tmp")
                 << QStringLiteral(".Trash-*/");
    }
    if (!s.contains(QStringLiteral("destDir")))
        s[QStringLiteral("destDir")] = defaultDestDir();
    if (!s.contains(QStringLiteral("keep")) || s.value(QStringLiteral("keep")).toInt() < 1)
        s[QStringLiteral("keep")] = 10;
    if (!s.contains(QStringLiteral("schedule")))
        s[QStringLiteral("schedule")] = configured ? QStringLiteral("off") : QStringLiteral("daily");
    if (!s.contains(QStringLiteral("time")))
        s[QStringLiteral("time")] = QStringLiteral("21:00");
    if (!s.contains(QStringLiteral("day")))
        s[QStringLiteral("day")] = 0;
    if (!s.contains(QStringLiteral("whenPlugged")))
        s[QStringLiteral("whenPlugged")] = true;
    s[QStringLiteral("folders")] = folders;
    s[QStringLiteral("excludes")] = excludes;
    s[QStringLiteral("configured")] = configured;
    m_settings = s;
    emit settingsChanged();
}

void BackupManager::loadStatus()
{
    QVariantMap st;
    QFile f(stateDir() + QStringLiteral("/backup.status"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const QString key = line.left(eq), value = line.mid(eq + 1);
            if (key == QLatin1String("time")) st[QStringLiteral("time")] = value.toLongLong() * 1000;
            else if (key == QLatin1String("last_ok")) st[QStringLiteral("lastOk")] = value.toLongLong() * 1000;
            else if (key == QLatin1String("last_ok_folder")) st[QStringLiteral("lastOkFolder")] = value;
            else if (key == QLatin1String("files")) st[QStringLiteral("files")] = value.toLongLong();
            else if (key == QLatin1String("bytes")) st[QStringLiteral("bytes")] = value.toLongLong();
            else st[key] = value;
        }
    }
    if (st != m_status) {
        m_status = st;
        emit statusChanged();
    }
    // running, started by cron or the desktop (not Back Up Now here)?
    qint64 pid = 0;
    if (!m_process && st.value(QStringLiteral("result")) == QLatin1String("running")) {
        pid = st.value(QStringLiteral("pid")).toLongLong();
        if (pid <= 0 || !QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid)))
            pid = 0; // it died without saying (killed outright)
    }
    if (pid != m_backgroundPid) {
        m_backgroundPid = pid;
        if (pid) {
            m_percent = 0;
            const QDateTime started = QDateTime::fromSecsSinceEpoch(st.value(QStringLiteral("started")).toLongLong());
            m_progressText = QStringLiteral("Backing up (started %1)...").arg(QLocale::system().toString(started.time(), QLocale::ShortFormat));
            m_poll.start();
            readProgress();
        } else {
            m_poll.stop();
            m_percent = -1;
            m_progressText.clear();
        }
        emit runningChanged();
        emit progressChanged();
    }
}

void BackupManager::readProgress()
{
    // the last progress line rsync wrote (the engine keeps its output)
    QFile f(stateDir() + QStringLiteral("/backup.rsync"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    if (f.size() > 4096)
        f.seek(f.size() - 4096);
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    static const QRegularExpression progress(QStringLiteral("^\\s*([0-9,]+)\\s+(\\d+)%"));
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QRegularExpressionMatch m = progress.match(lines.at(i));
        if (!m.hasMatch())
            continue;
        const int percent = m.captured(2).toInt();
        if (percent != m_percent) {
            m_percent = percent;
            const QDateTime started = QDateTime::fromSecsSinceEpoch(m_status.value(QStringLiteral("started")).toLongLong());
            m_progressText = QStringLiteral("Backing up (started %1)... %2%")
                                 .arg(QLocale::system().toString(started.time(), QLocale::ShortFormat)).arg(percent);
            emit progressChanged();
        }
        return;
    }
}

QString BackupManager::cronLine(const QVariantMap &s) const
{
    const QString schedule = s.value(QStringLiteral("schedule")).toString();
    if (schedule != QLatin1String("hourly") && schedule != QLatin1String("daily") && schedule != QLatin1String("weekly"))
        return {};
    const QStringList hm = s.value(QStringLiteral("time")).toString().split(QLatin1Char(':'));
    const int hour = qBound(0, hm.value(0).toInt(), 23), minute = qBound(0, hm.value(1).toInt(), 59);
    QString when;
    if (schedule == QLatin1String("hourly"))
        when = QStringLiteral("%1 * * * *").arg(minute);
    else if (schedule == QLatin1String("daily"))
        when = QStringLiteral("%1 %2 * * *").arg(minute).arg(hour);
    else
        when = QStringLiteral("%1 %2 * * %3").arg(minute).arg(hour).arg(qBound(0, s.value(QStringLiteral("day")).toInt(), 6));
    QString path = engine();
    if (path.isEmpty())
        path = QStringLiteral("wayland95-backup");
    // (cron runs it with sh: quoted, in case the path has spaces)
    return when + QStringLiteral(" '") + path + QStringLiteral("' --scheduled ") + CronMark;
}

QString BackupManager::writeCrontab(const QVariantMap &s)
{
    QProcess read;
    read.start(QStringLiteral("crontab"), {QStringLiteral("-l")});
    if (!read.waitForStarted(5000))
        return QStringLiteral("cron isn't installed, so backups can't be scheduled (sudo apt install cron).");
    read.waitForFinished(10000);
    // (no crontab yet: exit 1 and "no crontab for USER")
    QStringList lines;
    for (const QString &l : QString::fromUtf8(read.readAllStandardOutput()).split(QLatin1Char('\n')))
        if (!l.contains(CronMark))
            lines << l;
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    const QString line = cronLine(s);
    if (!line.isEmpty())
        lines << line;
    QProcess write;
    write.start(QStringLiteral("crontab"), {QStringLiteral("-")});
    if (!write.waitForStarted(5000))
        return QStringLiteral("cron isn't installed, so backups can't be scheduled (sudo apt install cron).");
    write.write((lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    write.closeWriteChannel();
    write.waitForFinished(10000);
    if (write.exitCode() != 0)
        return QStringLiteral("The schedule couldn't be saved:\n%1").arg(QString::fromUtf8(write.readAllStandardError()).trimmed());
    return {};
}

QString BackupManager::save(const QVariantMap &s)
{
    QStringList out;
    out << QStringLiteral("# Control Panel > Backup (wayland95-backup reads this)");
    auto one = [](QString v) { return v.remove(QLatin1Char('\n')); };
    out << QStringLiteral("DRIVE_UUID=") + one(s.value(QStringLiteral("driveUuid")).toString())
        << QStringLiteral("DRIVE_NAME=") + one(s.value(QStringLiteral("driveName")).toString())
        << QStringLiteral("DEST_DIR=") + one(s.value(QStringLiteral("destDir")).toString())
        << QStringLiteral("KEEP=%1").arg(qMax(1, s.value(QStringLiteral("keep")).toInt()))
        << QStringLiteral("SCHEDULE=") + one(s.value(QStringLiteral("schedule")).toString())
        << QStringLiteral("TIME=") + one(s.value(QStringLiteral("time")).toString())
        << QStringLiteral("DAY=%1").arg(s.value(QStringLiteral("day")).toInt())
        << QStringLiteral("WHEN_PLUGGED=") + (s.value(QStringLiteral("whenPlugged")).toBool() ? QStringLiteral("yes") : QStringLiteral("no"));
    for (const QVariant &f : s.value(QStringLiteral("folders")).toList())
        out << QStringLiteral("FOLDER=") + one(f.toString());
    for (const QVariant &e : s.value(QStringLiteral("excludes")).toList())
        if (!e.toString().trimmed().isEmpty())
            out << QStringLiteral("EXCLUDE=") + one(e.toString().trimmed());

    QDir().mkpath(QFileInfo(confPath()).absolutePath());
    QSaveFile file(confPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("Cannot write %1.").arg(confPath());
    file.write((out.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    if (!file.commit())
        return QStringLiteral("Cannot write %1.").arg(confPath());
    const QString error = writeCrontab(s);
    loadSettings();
    return error;
}

void BackupManager::backUpNow()
{
    if (m_process)
        return;
    const QString path = engine();
    if (path.isEmpty()) {
        emit finished(false, QStringLiteral("wayland95-backup isn't installed."));
        return;
    }
    auto *p = new QProcess(this);
    m_process = p;
    // its own process group, so Stop also stops rsync under it
    p->setChildProcessModifier([] { ::setpgid(0, 0); });
    p->setProcessChannelMode(QProcess::SeparateChannels);
    m_percent = 0;
    m_progressText = QStringLiteral("Starting...");
    emit runningChanged();
    emit progressChanged();
    auto lastLine = std::make_shared<QString>();
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p, lastLine] {
        while (p->canReadLine()) {
            const QString line = QString::fromUtf8(p->readLine()).trimmed();
            if (line.isEmpty())
                continue;
            // rsync's --info=progress2: "  12,345,678  45%   3.21MB/s    0:00:12 (xfr#10, to-chk=5/40)"
            static const QRegularExpression progress(QStringLiteral("^\\s*([0-9,]+)\\s+(\\d+)%"));
            const QRegularExpressionMatch m = progress.match(line);
            if (m.hasMatch()) {
                m_percent = m.captured(2).toInt();
                m_progressText = QStringLiteral("Copying... %1%").arg(m_percent);
            } else {
                m_progressText = line;
                *lastLine = line;
            }
            emit progressChanged();
        }
    });
    connect(p, &QProcess::finished, this, [this, p, lastLine](int code, QProcess::ExitStatus) {
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        p->deleteLater();
        m_process = nullptr;
        m_percent = -1;
        m_progressText.clear();
        emit runningChanged();
        emit progressChanged();
        loadStatus();
        emit finished(code == 0, !lastLine->isEmpty() ? *lastLine : err);
    });
    p->start(path, {QStringLiteral("--now")});
}

void BackupManager::stop()
{
    if (!m_process && m_backgroundPid > 0) {
        // one cron or the desktop started: its process group, if it leads
        // one (the engine makes its own), else just it
        QFile stat(QStringLiteral("/proc/%1/stat").arg(m_backgroundPid));
        qint64 group = 0;
        if (stat.open(QIODevice::ReadOnly)) {
            const QString s = QString::fromUtf8(stat.readAll());
            const QStringList fields = s.mid(s.lastIndexOf(QLatin1Char(')')) + 2).split(QLatin1Char(' '));
            group = fields.value(2).toLongLong(); // state ppid pgrp
        }
        ::kill(group == m_backgroundPid ? -m_backgroundPid : m_backgroundPid, SIGTERM);
        return;
    }
    if (!m_process)
        return;
    const qint64 pid = m_process->processId();
    if (pid > 0)
        ::kill(-pid, SIGTERM);
    m_process->waitForFinished(3000);
}

void BackupManager::setUpDrive()
{
    const QString folder = m_status.value(QStringLiteral("fix")).toString();
    // only the folder the backup asked for: a plain path on a drive that's there
    if (folder.isEmpty() || !QDir::isAbsolutePath(folder) || folder.contains(QLatin1String("/../"))
        || !QFileInfo(QFileInfo(folder).absolutePath()).isDir()) {
        emit setUpFinished(false, QStringLiteral("The drive isn't there to set up. Plug it in, and try Back Up Now again."));
        return;
    }
    const QString pkexec = program(QStringLiteral("pkexec"));
    if (pkexec.isEmpty()) {
        emit setUpFinished(false, QStringLiteral("pkexec isn't installed (sudo apt install pkexec)."));
        return;
    }
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p, folder](int code, QProcess::ExitStatus) {
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        p->deleteLater();
        // 126/127: the password box was dismissed or the password wasn't right
        if (code == 126 || code == 127)
            emit setUpFinished(false, QStringLiteral("You need an administrator's password to set up the drive."));
        else if (code != 0)
            emit setUpFinished(false, err.isEmpty() ? QStringLiteral("The drive couldn't be set up.") : err);
        else
            emit setUpFinished(true, folder);
    });
    p->start(pkexec, {QStringLiteral("/usr/bin/install"), QStringLiteral("-d"), QStringLiteral("-m"), QStringLiteral("0755"),
                      QStringLiteral("-o"), QString::number(::getuid()), QStringLiteral("-g"), QString::number(::getgid()),
                      QStringLiteral("--"), folder});
}

bool BackupManager::openFolder(const QString &path) const
{
    const QString files = program(QStringLiteral("win95files"));
    return !files.isEmpty() && QProcess::startDetached(files, {path});
}

bool BackupManager::openLog() const
{
    const QString notepad = program(QStringLiteral("win95notepad"));
    return !notepad.isEmpty() && QFileInfo::exists(logPath()) && QProcess::startDetached(notepad, {logPath()});
}
