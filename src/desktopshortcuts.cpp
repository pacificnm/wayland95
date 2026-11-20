#include "desktopshortcuts.h"

#include "desktopentry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtDebug>

#include <algorithm>

namespace {

// one Exec argument, quoted as the Desktop Entry spec says when it needs it
QString quoteArg(const QString &a)
{
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_./:~+=,@-]+$"));
    if (plain.match(a).hasMatch())
        return a;
    QString q = a;
    q.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
     .replace(QLatin1Char('"'), QLatin1String("\\\""))
     .replace(QLatin1Char('`'), QLatin1String("\\`"))
     .replace(QLatin1Char('$'), QLatin1String("\\$"));
    // (and the backslashes once more for the file's own escaping)
    q.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

// a value on one line, with the file's escapes
QString escapeValue(const QString &v)
{
    QString out = v;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('\n'), QLatin1String("\\n"));
    return out;
}

} // namespace

DesktopShortcuts::DesktopShortcuts(QObject *parent)
    : QObject(parent)
{
    // a copy is written in a few steps: read the folder once it settles
    m_reload.setSingleShot(true);
    m_reload.setInterval(300);
    connect(&m_reload, &QTimer::timeout, this, &DesktopShortcuts::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_reload.start(); });
    reload();
}

QString DesktopShortcuts::folder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
}

bool DesktopShortcuts::isShortcut(const QString &path) const
{
    const QFileInfo info(path);
    return info.suffix() == QLatin1String("desktop")
        && QDir(info.absolutePath()).canonicalPath() == QDir(folder()).canonicalPath();
}

void DesktopShortcuts::watch()
{
    // the folder, or (until it's made) the home folder it'll appear in
    const QString dir = QFileInfo::exists(folder()) ? folder() : QDir::homePath();
    const QStringList old = m_watcher.directories();
    if (old == QStringList{ dir })
        return;
    if (!old.isEmpty())
        m_watcher.removePaths(old);
    m_watcher.addPath(dir);
}

void DesktopShortcuts::reload()
{
    QVariantList list;
    const QDir dir(folder());
    for (const QFileInfo &f : dir.entryInfoList({ QStringLiteral("*.desktop") }, QDir::Files | QDir::Readable)) {
        const DesktopEntry e = DesktopEntry::read(f.absoluteFilePath());
        if (!e.application || e.exec.isEmpty() || e.hidden)
            continue;
        list << QVariantMap{
            { QStringLiteral("label"), e.name.isEmpty() ? f.completeBaseName() : e.name },
            { QStringLiteral("iconSource"), e.wayland95Icon.isEmpty() ? appIconUrl(e.icon) : QString() },
            { QStringLiteral("icon"), e.wayland95Icon },
            { QStringLiteral("path"), f.absoluteFilePath() },
        };
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("label")).toString()
                   .compare(b.toMap().value(QStringLiteral("label")).toString(), Qt::CaseInsensitive) < 0;
    });
    watch();
    if (list != m_shortcuts) {
        m_shortcuts = list;
        emit changed();
    }
}

QString DesktopShortcuts::write(const QString &fileName, const QByteArray &contents)
{
    const QDir dir(folder());
    if (!dir.exists() && !QDir().mkpath(dir.path()))
        return QStringLiteral("The Desktop folder (%1) can't be made.").arg(dir.path());

    // the same shortcut already there: nothing to do; another by that name:
    // "name-2.desktop" and so on
    const QFileInfo base(fileName);
    QString path = dir.filePath(fileName);
    for (int n = 2; QFileInfo::exists(path); ++n) {
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == contents)
            return QString();
        path = dir.filePath(base.completeBaseName() + QLatin1Char('-') + QString::number(n) + QStringLiteral(".desktop"));
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(contents) != contents.size() || !f.commit())
        return QStringLiteral("The shortcut can't be saved in %1.").arg(dir.path());
    // executable: what other desktops take as "this launcher is trusted"
    QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeUser);
    qInfo().noquote() << "DesktopShortcuts: added" << path;
    reload();
    return QString();
}

QString DesktopShortcuts::addProgram(const QString &desktopFile)
{
    QFile src(desktopFile);
    if (desktopFile.isEmpty() || !src.open(QIODevice::ReadOnly))
        return QStringLiteral("The program's shortcut can't be read.");
    return write(QFileInfo(desktopFile).fileName(), src.readAll());
}

QString DesktopShortcuts::addCommand(const QString &label, const QString &program,
                                     const QStringList &arguments, const QString &icon)
{
    QStringList exec{ quoteArg(program) };
    for (const QString &a : arguments)
        exec << quoteArg(a);
    QString contents = QStringLiteral("[Desktop Entry]\nType=Application\n");
    contents += QStringLiteral("Name=") + escapeValue(label) + QLatin1Char('\n');
    contents += QStringLiteral("Exec=") + exec.join(QLatin1Char(' ')) + QLatin1Char('\n');
    if (!icon.isEmpty())
        contents += QStringLiteral("X-Wayland95-Icon=") + icon + QLatin1Char('\n');
    contents += QStringLiteral("Terminal=false\n");

    // "wayland95-file-manager.desktop": named after the entry, as one
    // program (win95files) has several
    QString name = label.toLower();
    name.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    name.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (name.isEmpty())
        name = QFileInfo(program).fileName();
    return write(QStringLiteral("wayland95-") + name + QStringLiteral(".desktop"), contents.toUtf8());
}

bool DesktopShortcuts::open(const QString &path)
{
    return isShortcut(path) && m_launcher.launchEntry(DesktopEntry::read(path), false);
}

bool DesktopShortcuts::openAsRoot(const QString &path)
{
    return isShortcut(path) && m_launcher.launchEntry(DesktopEntry::read(path), true);
}

bool DesktopShortcuts::remove(const QString &path)
{
    if (!isShortcut(path))
        return false;
    const bool ok = QFile::moveToTrash(path);
    if (!ok)
        qWarning().noquote() << "DesktopShortcuts: can't move" << path << "to the Recycle Bin";
    reload();
    return ok;
}
