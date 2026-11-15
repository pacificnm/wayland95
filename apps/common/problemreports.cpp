#include "problemreports.h"
#include "crashhandler.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QGuiApplication>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <algorithm>

#ifndef WAYLAND95_ISSUES_URL
#define WAYLAND95_ISSUES_URL "https://github.com/pacificnm/wayland95/issues/new"
#endif

ProblemReports::ProblemReports(QObject *parent)
    : QObject(parent)
{
    m_dir = QString::fromLocal8Bit(crashReportDir());
    if (m_dir.isEmpty()) {
        // a program without the crash handler (shouldn't happen)
        QString state = qEnvironmentVariable("XDG_STATE_HOME");
        if (state.isEmpty())
            state = QDir::homePath() + QStringLiteral("/.local/state");
        m_dir = state + QStringLiteral("/wayland95/crashes");
    }
    QDir().mkpath(m_dir);
    m_watcher.addPath(m_dir);
    // a report is written in one go, but give the session script a moment
    // to add the log to the compositor's
    auto *delay = new QTimer(this);
    delay->setSingleShot(true);
    delay->setInterval(1500);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, delay, qOverload<>(&QTimer::start));
    connect(delay, &QTimer::timeout, this, &ProblemReports::reload);
    reload();
}

QString ProblemReports::displayName(const QString &program)
{
    static const QHash<QString, QString> names {
        { QStringLiteral("win95compositor"), QStringLiteral("Wayland 95 desktop") },
        { QStringLiteral("win95files"), QStringLiteral("File Manager") },
        { QStringLiteral("win95notepad"), QStringLiteral("Notepad") },
        { QStringLiteral("win95control"), QStringLiteral("Control Panel") },
        { QStringLiteral("win95terminal"), QStringLiteral("Terminal") },
        { QStringLiteral("win95sysmon"), QStringLiteral("System Monitor") },
        { QStringLiteral("win95volume"), QStringLiteral("Volume Control") },
        { QStringLiteral("win95portal"), QStringLiteral("File dialogs (portal)") },
        { QStringLiteral("win95imagewriter"), QStringLiteral("Disk Image Writer") },
        { QStringLiteral("win95notify"), QStringLiteral("win95notify") },
    };
    return names.value(program, program);
}

QVariantMap ProblemReports::parse(const QString &file) const
{
    QFile f(m_dir + QLatin1Char('/') + file);
    QVariantMap m;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return m;
    QString program, version, signal, exit;
    qint64 time = 0;
    // the header: "Key: value" lines up to the first blank one
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty())
            break;
        const QString key = line.section(QLatin1Char(':'), 0, 0);
        const QString value = line.section(QLatin1Char(':'), 1).trimmed();
        if (key == QLatin1String("Program")) program = value;
        else if (key == QLatin1String("Version")) version = value;
        else if (key == QLatin1String("Signal")) signal = value.section(QLatin1Char(' '), 1);
        else if (key == QLatin1String("Exit")) exit = value;
        else if (key == QLatin1String("Time")) time = value.toLongLong() * 1000;
    }
    if (program.isEmpty())
        program = file.section(QLatin1Char('-'), 0, 0);
    if (!time)
        time = QFileInfo(f).lastModified().toMSecsSinceEpoch();
    const QString name = displayName(program);
    const QString how = !signal.isEmpty() ? QStringLiteral("crashed: ") + signal
                      : !exit.isEmpty() ? QStringLiteral("stopped unexpectedly: ") + exit
                      : QStringLiteral("crashed");
    m = {
        { QStringLiteral("file"), file },
        { QStringLiteral("key"), file },
        { QStringLiteral("program"), program },
        { QStringLiteral("name"), name },
        { QStringLiteral("version"), version },
        { QStringLiteral("time"), time },
        { QStringLiteral("signal"), signal },
        { QStringLiteral("summary"), name + QLatin1Char(' ') + how },
    };
    return m;
}

void ProblemReports::reload()
{
    const QStringList files = QDir(m_dir).entryList({ QStringLiteral("*.crash") }, QDir::Files);
    const QSet<QString> seen = readSeen();
    QList<QVariantMap> list;
    QList<QVariantMap> added;
    const bool first = m_known.isEmpty() && m_reports.isEmpty();
    QSet<QString> known;
    for (const QString &file : files) {
        QVariantMap r = parse(file);
        if (r.isEmpty())
            continue;
        r.insert(QStringLiteral("seen"), seen.contains(file));
        known.insert(file);
        if (!first && !m_known.contains(file))
            added << r;
        list << r;
    }
    std::sort(list.begin(), list.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("time")).toLongLong() > b.value(QStringLiteral("time")).toLongLong();
    });
    m_known = known;
    m_reports.clear();
    for (const QVariantMap &r : std::as_const(list))
        m_reports << r;
    emit reportsChanged();
    for (const QVariantMap &r : std::as_const(added))
        emit reportAdded(r);
}

QString ProblemReports::text(const QString &file) const
{
    if (file.contains(QLatin1Char('/')))
        return QString();
    QFile f(m_dir + QLatin1Char('/') + file);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

bool ProblemReports::remove(const QString &file)
{
    if (file.contains(QLatin1Char('/')))
        return false;
    const bool ok = QFile::remove(m_dir + QLatin1Char('/') + file);
    reload();
    return ok;
}

void ProblemReports::removeAll()
{
    for (const QString &file : QDir(m_dir).entryList({ QStringLiteral("*.crash") }, QDir::Files))
        QFile::remove(m_dir + QLatin1Char('/') + file);
    QFile::remove(m_dir + QStringLiteral("/.seen"));
    reload();
}

QSet<QString> ProblemReports::readSeen() const
{
    QFile f(m_dir + QStringLiteral("/.seen"));
    QSet<QString> seen;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const QByteArray &line : f.readAll().split('\n'))
            if (!line.isEmpty())
                seen.insert(QString::fromUtf8(line));
    return seen;
}

void ProblemReports::writeSeen(const QSet<QString> &seen)
{
    QSaveFile f(m_dir + QStringLiteral("/.seen"));
    if (!f.open(QIODevice::WriteOnly))
        return;
    for (const QString &s : seen)
        if (QFileInfo::exists(m_dir + QLatin1Char('/') + s))
            f.write(s.toUtf8() + '\n');
    f.commit();
}

QVariantList ProblemReports::unseen() const
{
    QVariantList out;
    for (const QVariant &r : m_reports)
        if (!r.toMap().value(QStringLiteral("seen")).toBool())
            out << r;
    return out;
}

void ProblemReports::markSeen(const QString &file)
{
    QSet<QString> seen = readSeen();
    seen.insert(file);
    writeSeen(seen);
    reload();
}

void ProblemReports::markAllSeen()
{
    QSet<QString> seen = readSeen();
    for (const QString &f : std::as_const(m_known))
        seen.insert(f);
    writeSeen(seen);
    reload();
}

QString ProblemReports::issueUrl(const QString &file) const
{
    const QVariantMap r = parse(file);
    QString body = text(file);
    // a URL can only be so long: the start of the report (header and
    // backtrace) matters most
    constexpr int Max = 6000;
    if (body.size() > Max)
        body = body.left(Max) + QStringLiteral("\n... (cut short; the whole report is in ~/.local/state/wayland95/crashes/")
               + file + QStringLiteral(")");
    const QString intro = QStringLiteral("**What were you doing when it happened?**\n\n\n\n**Problem report**\n\n```\n");
    const QString title = QStringLiteral("%1 (%2)")
            .arg(r.value(QStringLiteral("summary")).toString(), r.value(QStringLiteral("version")).toString());
    // every reserved character escaped ('+' would otherwise read as a space)
    return QStringLiteral(WAYLAND95_ISSUES_URL) + QStringLiteral("?title=")
           + QString::fromLatin1(QUrl::toPercentEncoding(title))
           + QStringLiteral("&body=")
           + QString::fromLatin1(QUrl::toPercentEncoding(intro + body + QStringLiteral("\n```\n")));
}

void ProblemReports::copy(const QString &file) const
{
    if (QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(text(file));
}
