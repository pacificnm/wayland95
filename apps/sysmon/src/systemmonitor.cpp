#include "systemmonitor.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <cerrno>
#include <cstring>
#include <csignal>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

// whole disks only, so partitions aren't counted twice
const QRegularExpression wholeDisk(
    QStringLiteral("^(sd[a-z]+|vd[a-z]+|hd[a-z]+|xvd[a-z]+|nvme\\d+n\\d+|mmcblk\\d+)$"));

} // namespace

SystemMonitor::SystemMonitor(QObject *parent)
    : QObject(parent)
{
    m_myUid = ::getuid();
    m_coreCount = qMax(1, int(readCpu().size()) - 1);
    connect(&m_timer, &QTimer::timeout, this, &SystemMonitor::sample);
    sample(); // the baseline; rates start with the next sample
    m_timer.start(m_interval);
}

void SystemMonitor::setInterval(int ms)
{
    if (ms == m_interval)
        return;
    m_interval = ms;
    if (ms > 0)
        m_timer.start(ms);
    else
        m_timer.stop();
    emit intervalChanged();
}

void SystemMonitor::setProcessesWanted(bool wanted)
{
    if (wanted == m_processesWanted)
        return;
    m_processesWanted = wanted;
    emit processesWantedChanged();
    if (wanted)
        refresh();
}

void SystemMonitor::refresh()
{
    sample();
}

QList<SystemMonitor::CpuTimes> SystemMonitor::readCpu() const
{
    QList<CpuTimes> list;
    const QList<QByteArray> lines = readFile(QStringLiteral("/proc/stat")).split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("cpu"))
            break;
        const QList<QByteArray> f = line.simplified().split(' ');
        CpuTimes t;
        quint64 idle = 0;
        for (int i = 1; i < f.size(); ++i) {
            const quint64 v = f.at(i).toULongLong();
            if (i <= 8) // user nice system idle iowait irq softirq steal
                t.total += v;
            if (i == 4 || i == 5) // idle, iowait
                idle += v;
        }
        t.busy = t.total - idle;
        list << t;
    }
    return list;
}

void SystemMonitor::push(const QString &key, qreal value)
{
    QList<qreal> &h = m_history[key];
    h << value;
    while (h.size() > historyLength())
        h.removeFirst();
}

QVariantMap SystemMonitor::history() const
{
    QVariantMap map;
    for (auto it = m_history.cbegin(); it != m_history.cend(); ++it) {
        QVariantList list;
        for (qreal v : it.value())
            list << v;
        map.insert(it.key(), list);
    }
    return map;
}

void SystemMonitor::sample()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const double seconds = m_lastSampleMs > 0 ? (nowMs - m_lastSampleMs) / 1000.0 : 0;
    m_lastSampleMs = nowMs;
    QVariantMap now;

    // CPU: busy share of the time since the last sample
    const QList<CpuTimes> cpu = readCpu();
    quint64 totalDelta = 0;
    if (!cpu.isEmpty()) {
        QVariantList cores;
        for (int i = 0; i < cpu.size(); ++i) {
            qreal pct = 0;
            if (i < m_lastCpu.size()) {
                const quint64 dt = cpu[i].total - m_lastCpu[i].total;
                const quint64 db = cpu[i].busy - m_lastCpu[i].busy;
                pct = dt > 0 ? qBound(0.0, 100.0 * db / dt, 100.0) : 0;
                if (i == 0)
                    totalDelta = dt;
            }
            if (i == 0)
                now.insert(QStringLiteral("cpu"), pct);
            else
                cores << pct;
        }
        now.insert(QStringLiteral("cores"), cores);
    }
    m_lastCpu = cpu;

    // memory (kB in /proc/meminfo)
    QHash<QByteArray, quint64> mem;
    for (const QByteArray &line : readFile(QStringLiteral("/proc/meminfo")).split('\n')) {
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
            mem.insert(line.left(colon), line.mid(colon + 1).simplified().split(' ').value(0).toULongLong() * 1024);
    }
    const quint64 memTotal = mem.value("MemTotal");
    const quint64 memUsed = memTotal - mem.value("MemAvailable", mem.value("MemFree"));
    const quint64 swapTotal = mem.value("SwapTotal");
    const quint64 swapUsed = swapTotal - mem.value("SwapFree");
    now.insert(QStringLiteral("memTotal"), double(memTotal));
    now.insert(QStringLiteral("memUsed"), double(memUsed));
    now.insert(QStringLiteral("swapTotal"), double(swapTotal));
    now.insert(QStringLiteral("swapUsed"), double(swapUsed));

    // network: all interfaces but loopback
    quint64 rx = 0, tx = 0;
    for (const QByteArray &line : readFile(QStringLiteral("/proc/net/dev")).split('\n')) {
        const qsizetype colon = line.indexOf(':');
        if (colon < 0)
            continue;
        if (line.left(colon).trimmed() == "lo")
            continue;
        const QList<QByteArray> f = line.mid(colon + 1).simplified().split(' ');
        rx += f.value(0).toULongLong();
        tx += f.value(8).toULongLong();
    }

    // disks: sectors read/written, 512 bytes each
    quint64 rd = 0, wr = 0;
    for (const QByteArray &line : readFile(QStringLiteral("/proc/diskstats")).split('\n')) {
        const QList<QByteArray> f = line.simplified().split(' ');
        if (f.size() < 10 || !wholeDisk.match(QString::fromLatin1(f.at(2))).hasMatch())
            continue;
        rd += f.at(5).toULongLong() * 512;
        wr += f.at(9).toULongLong() * 512;
    }

    auto rate = [seconds, this](quint64 value, quint64 last) {
        return (m_haveLast && seconds > 0 && value >= last) ? (value - last) / seconds : 0.0;
    };
    now.insert(QStringLiteral("netRx"), rate(rx, m_lastNetRx));
    now.insert(QStringLiteral("netTx"), rate(tx, m_lastNetTx));
    now.insert(QStringLiteral("diskRead"), rate(rd, m_lastDiskRead));
    now.insert(QStringLiteral("diskWrite"), rate(wr, m_lastDiskWrite));
    m_lastNetRx = rx;
    m_lastNetTx = tx;
    m_lastDiskRead = rd;
    m_lastDiskWrite = wr;

    // processes and threads, uptime, load
    const QList<QByteArray> load = readFile(QStringLiteral("/proc/loadavg")).simplified().split(' ');
    now.insert(QStringLiteral("load"), QString::fromLatin1(load.mid(0, 3).join(' ')));
    const QList<QByteArray> running = load.value(3).split('/');
    now.insert(QStringLiteral("threads"), running.value(1).toInt());
    now.insert(QStringLiteral("uptime"), readFile(QStringLiteral("/proc/uptime")).split(' ').value(0).toDouble());
    int processCount = 0;
    for (const QString &name : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        name.toInt(&isPid);
        if (isPid)
            ++processCount;
    }
    now.insert(QStringLiteral("processes"), processCount);

    if (m_haveLast) {
        push(QStringLiteral("cpu"), now.value(QStringLiteral("cpu")).toReal());
        push(QStringLiteral("mem"), memTotal ? 100.0 * memUsed / memTotal : 0);
        push(QStringLiteral("swap"), swapTotal ? 100.0 * swapUsed / swapTotal : 0);
        push(QStringLiteral("netRx"), now.value(QStringLiteral("netRx")).toReal());
        push(QStringLiteral("netTx"), now.value(QStringLiteral("netTx")).toReal());
        push(QStringLiteral("diskRead"), now.value(QStringLiteral("diskRead")).toReal());
        push(QStringLiteral("diskWrite"), now.value(QStringLiteral("diskWrite")).toReal());
    }
    m_haveLast = true;
    m_now = now;
    emit updated();

    if (m_processesWanted)
        sampleProcesses(totalDelta);
}

QString SystemMonitor::userName(uint uid)
{
    const auto it = m_userNames.constFind(uid);
    if (it != m_userNames.constEnd())
        return it.value();
    const passwd *pw = ::getpwuid(uid);
    const QString name = pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(uid);
    m_userNames.insert(uid, name);
    return name;
}

void SystemMonitor::sampleProcesses(quint64 totalDelta)
{
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    QHash<int, quint64> ticks;
    QVariantList list;
    for (const QString &entry : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        const int pid = entry.toInt(&isPid);
        if (!isPid)
            continue;
        const QString dir = QStringLiteral("/proc/") + entry;
        const QByteArray stat = readFile(dir + QStringLiteral("/stat"));
        // "pid (comm) state ..." - comm may itself contain spaces and ')'
        const qsizetype open = stat.indexOf('('), close = stat.lastIndexOf(')');
        if (open < 0 || close < open)
            continue;
        const QString comm = QString::fromUtf8(stat.mid(open + 1, close - open - 1));
        const QList<QByteArray> f = stat.mid(close + 2).split(' ');
        // after comm: state(0) ppid(1) ... utime(11) stime(12) ... rss(21)
        const quint64 procTicks = f.value(11).toULongLong() + f.value(12).toULongLong();
        const quint64 rss = f.value(21).toULongLong() * quint64(pageSize);
        ticks.insert(pid, procTicks);

        QByteArray cmdline = readFile(dir + QStringLiteral("/cmdline"));
        // kernel threads have no command line; they aren't programs
        if (cmdline.isEmpty())
            continue;
        cmdline.replace('\0', ' ');
        const QString command = QString::fromUtf8(cmdline).trimmed();
        // the full program name (comm is cut at 15 characters)
        QString name = command.section(QLatin1Char(' '), 0, 0).section(QLatin1Char('/'), -1);
        if (name.isEmpty() || !comm.startsWith(name.left(15)))
            name = comm;

        qreal cpu = 0;
        const auto last = m_lastProcTicks.constFind(pid);
        if (last != m_lastProcTicks.constEnd() && totalDelta > 0 && procTicks >= last.value())
            cpu = qBound(0.0, 100.0 * (procTicks - last.value()) / totalDelta, 100.0);

        const uint uid = QFileInfo(dir).ownerId();
        list << QVariantMap{
            { QStringLiteral("pid"), pid },
            { QStringLiteral("name"), name },
            { QStringLiteral("command"), command },
            { QStringLiteral("user"), userName(uid) },
            { QStringLiteral("cpu"), cpu },
            { QStringLiteral("memory"), double(rss) },
            { QStringLiteral("mine"), uid == m_myUid },
        };
    }
    m_lastProcTicks = ticks;
    m_processes = list;
    emit processesChanged();
}

QString SystemMonitor::endProcess(int pid, bool force)
{
    if (pid <= 1)
        return QStringLiteral("That process can't be ended.");
    if (::kill(pid, force ? SIGKILL : SIGTERM) == 0) {
        QTimer::singleShot(300, this, &SystemMonitor::refresh);
        return {};
    }
    if (errno == EPERM)
        return QStringLiteral("You don't have permission to end this process. It belongs to another user "
                              "or to the system.");
    if (errno == ESRCH)
        return QStringLiteral("The process has already ended.");
    return QString::fromLocal8Bit(strerror(errno));
}
