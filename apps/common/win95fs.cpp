#include "win95fs.h"

#include <QDir>
#include <QFileInfo>

Win95Fs::Win95Fs(QObject *parent)
    : QObject(parent)
{
}

QString Win95Fs::homePath() const
{
    return QDir::homePath();
}

bool Win95Fs::exists(const QString &path) const
{
    return !path.isEmpty() && QFileInfo::exists(path);
}

bool Win95Fs::isDir(const QString &path) const
{
    return !path.isEmpty() && QFileInfo(path).isDir();
}

bool Win95Fs::isFile(const QString &path) const
{
    return !path.isEmpty() && QFileInfo(path).isFile();
}

QString Win95Fs::parentDir(const QString &path) const
{
    return QFileInfo(QDir::cleanPath(path)).absolutePath();
}

QString Win95Fs::fileName(const QString &path) const
{
    return QFileInfo(path).fileName();
}

QString Win95Fs::suffix(const QString &path) const
{
    return QFileInfo(path).suffix();
}

QString Win95Fs::displayName(const QString &dir) const
{
    const QString clean = QDir::cleanPath(dir);
    if (clean == QLatin1String("/"))
        return clean;
    return QFileInfo(clean).fileName();
}

QString Win95Fs::resolve(const QString &dir, const QString &name) const
{
    QString n = name.trimmed();
    if (n == QLatin1String("~") || n.startsWith(QLatin1String("~/")))
        n = QDir::homePath() + n.mid(1);
    if (QDir::isAbsolutePath(n))
        return QDir::cleanPath(n);
    return QDir::cleanPath(QDir(dir).filePath(n));
}

QUrl Win95Fs::toUrl(const QString &path) const
{
    return QUrl::fromLocalFile(path);
}
