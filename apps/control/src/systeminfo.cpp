#include "systeminfo.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QTimeZone>
#include <QtGlobal>

#include <pwd.h>
#include <unistd.h>

namespace {

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

// "MemTotal:  16314172 kB" -> bytes
qint64 meminfoValue(const QString &meminfo, const QString &key)
{
    for (const QString &line : meminfo.split(QLatin1Char('\n'))) {
        if (!line.startsWith(key + QLatin1Char(':')))
            continue;
        const QStringList parts = line.mid(key.size() + 1).simplified().split(QLatin1Char(' '));
        return parts.isEmpty() ? 0 : parts.first().toLongLong() * 1024;
    }
    return 0;
}

QString formatBytes(qint64 bytes)
{
    const double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0)
        return QStringLiteral("%1 GB").arg(QString::number(gb, 'f', 1));
    return QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
}

} // namespace

SystemInfo::SystemInfo(QObject *parent)
    : QObject(parent)
{
}

QString SystemInfo::osName() const
{
    return QSysInfo::prettyProductName();
}

QString SystemInfo::kernel() const
{
    return QStringLiteral("%1 %2 (%3)").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion(),
                                            QSysInfo::currentCpuArchitecture());
}

QString SystemInfo::cpu() const
{
    const QString cpuinfo = readFile(QStringLiteral("/proc/cpuinfo"));
    QString model;
    int cores = 0;
    for (const QString &line : cpuinfo.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("model name"))) {
            ++cores;
            if (model.isEmpty())
                model = line.section(QLatin1Char(':'), 1).trimmed();
        }
    }
    if (model.isEmpty())
        model = QSysInfo::currentCpuArchitecture();
    return cores > 1 ? QStringLiteral("%1 (%2 threads)").arg(model).arg(cores) : model;
}

QString SystemInfo::memory() const
{
    const qint64 total = meminfoValue(readFile(QStringLiteral("/proc/meminfo")), QStringLiteral("MemTotal"));
    return QStringLiteral("%1 RAM").arg(formatBytes(total));
}

QString SystemInfo::hostName() const
{
    return QSysInfo::machineHostName();
}

QString SystemInfo::userName() const
{
    // the full name from the GECOS field, falling back to the login name
    if (const passwd *pw = getpwuid(getuid())) {
        const QString gecos = QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed();
        if (!gecos.isEmpty())
            return gecos;
        return QString::fromLocal8Bit(pw->pw_name);
    }
    return qEnvironmentVariable("USER");
}

QString SystemInfo::qtVersion() const
{
    return QString::fromLatin1(qVersion());
}

QString SystemInfo::timeZoneId() const
{
    return QString::fromUtf8(QTimeZone::systemTimeZoneId());
}

QString SystemInfo::timeZoneName() const
{
    const QTimeZone zone = QTimeZone::systemTimeZone();
    const QDateTime now = QDateTime::currentDateTime();
    const int offset = zone.offsetFromUtc(now);
    const QString sign = offset < 0 ? QStringLiteral("-") : QStringLiteral("+");
    const int abs = qAbs(offset);
    return QStringLiteral("(UTC%1%2:%3) %4")
        .arg(sign)
        .arg(abs / 3600, 2, 10, QLatin1Char('0'))
        .arg((abs % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(zone.displayName(now, QTimeZone::LongName));
}

QVariantMap SystemInfo::performance() const
{
    QVariantMap result;
    const QString meminfo = readFile(QStringLiteral("/proc/meminfo"));
    const qint64 total = meminfoValue(meminfo, QStringLiteral("MemTotal"));
    const qint64 available = meminfoValue(meminfo, QStringLiteral("MemAvailable"));
    const qint64 swapTotal = meminfoValue(meminfo, QStringLiteral("SwapTotal"));
    const qint64 swapFree = meminfoValue(meminfo, QStringLiteral("SwapFree"));
    const int freePercent = total > 0 ? int(available * 100 / total) : 0;
    result[QStringLiteral("memoryFreePercent")] = freePercent;
    result[QStringLiteral("memoryText")] =
        QStringLiteral("%1 free of %2 (%3% free)").arg(formatBytes(available), formatBytes(total)).arg(freePercent);
    result[QStringLiteral("swapText")] = swapTotal > 0
        ? QStringLiteral("%1 free of %2").arg(formatBytes(swapFree), formatBytes(swapTotal))
        : QStringLiteral("None");

    const qint64 uptime = qint64(readFile(QStringLiteral("/proc/uptime")).section(QLatin1Char(' '), 0, 0).toDouble());
    const qint64 days = uptime / 86400, hours = (uptime % 86400) / 3600, minutes = (uptime % 3600) / 60;
    result[QStringLiteral("uptimeText")] = days > 0
        ? QStringLiteral("%1 day(s), %2 hour(s), %3 minute(s)").arg(days).arg(hours).arg(minutes)
        : QStringLiteral("%1 hour(s), %2 minute(s)").arg(hours).arg(minutes);

    const QStringList load = readFile(QStringLiteral("/proc/loadavg")).split(QLatin1Char(' '));
    result[QStringLiteral("loadText")] = load.size() >= 3
        ? QStringLiteral("%1, %2, %3 (1, 5, 15 minutes)").arg(load.at(0), load.at(1), load.at(2))
        : QString();
    return result;
}

QVariantList SystemInfo::keyboardLayouts() const
{
    // /usr/share/X11/xkb/rules/evdev.lst has a "! layout" section of
    // "  code    Description" lines
    QVariantList layouts;
    const QString lst = readFile(QStringLiteral("/usr/share/X11/xkb/rules/evdev.lst"));
    bool inLayouts = false;
    for (const QString &line : lst.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1Char('!'))) {
            inLayouts = line.trimmed() == QLatin1String("! layout");
            continue;
        }
        if (!inLayouts || line.trimmed().isEmpty())
            continue;
        const QString trimmed = line.trimmed();
        const int space = trimmed.indexOf(QLatin1Char(' '));
        if (space <= 0)
            continue;
        layouts << QVariantMap{
            {QStringLiteral("code"), trimmed.left(space)},
            {QStringLiteral("name"), trimmed.mid(space).trimmed()},
        };
    }
    std::sort(layouts.begin(), layouts.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(
                   b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    return layouts;
}

QStringList SystemInfo::wallpapers() const
{
    const QStringList roots = {
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        QStringLiteral("/usr/share/wallpapers"),
        QStringLiteral("/usr/share/backgrounds"),
    };
    const QStringList filters = {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                 QStringLiteral("*.bmp"), QStringLiteral("*.webp")};
    QStringList found;
    QSet<QString> seen;
    for (const QString &root : roots) {
        if (!QFileInfo(root).isDir())
            continue;
        QDirIterator it(root, filters, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        int perRoot = 0;
        while (it.hasNext() && perRoot < 150) {
            const QString path = it.next();
            const QString canonical = QFileInfo(path).canonicalFilePath();
            if (seen.contains(canonical))
                continue;
            // KDE wallpaper packages ship many sizes of one picture; the
            // screenshot/preview images aren't wallpapers
            if (path.contains(QLatin1String("/screenshot")))
                continue;
            seen.insert(canonical);
            found << path;
            ++perRoot;
        }
    }
    std::sort(found.begin(), found.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).fileName().localeAwareCompare(QFileInfo(b).fileName()) < 0;
    });
    return found;
}
