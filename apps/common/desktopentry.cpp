#include "desktopentry.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QMutex>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

namespace {

QStringList dataDirs()
{
    QStringList dirs;
    const QString env = qEnvironmentVariable("XDG_DATA_DIRS", QStringLiteral("/usr/local/share:/usr/share"));
    for (const QString &d : env.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        if (!d.contains(QLatin1String("flatpak")) && !dirs.contains(d))
            dirs << d;
    return dirs;
}

QStringList splitList(const QString &value)
{
    QStringList out;
    for (const QString &v : value.split(QLatin1Char(';'), Qt::SkipEmptyParts))
        out << v.trimmed();
    return out;
}

// the spec's escapes in string values
QString unescape(const QString &v)
{
    QString out;
    out.reserve(v.size());
    for (int i = 0; i < v.size(); ++i) {
        if (v.at(i) == QLatin1Char('\\') && i + 1 < v.size()) {
            const QChar c = v.at(++i);
            out += c == QLatin1Char('s') ? QChar(' ') : c == QLatin1Char('n') ? QChar('\n')
                 : c == QLatin1Char('t') ? QChar('\t') : c == QLatin1Char('r') ? QChar('\r') : c;
        } else {
            out += v.at(i);
        }
    }
    return out;
}

} // namespace

DesktopEntry DesktopEntry::read(const QString &path, const QString &id)
{
    DesktopEntry e;
    e.path = path;
    e.id = id.isEmpty() ? QFileInfo(path).fileName() : id;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return e;
    bool inEntry = false;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.startsWith('[')) {
            inEntry = line == "[Desktop Entry]";
            continue;
        }
        if (!inEntry || line.startsWith('#'))
            continue;
        const int eq = line.indexOf('=');
        if (eq < 0)
            continue;
        const QByteArray key = line.left(eq).trimmed();
        const QString value = QString::fromUtf8(line.mid(eq + 1).trimmed());
        if (key == "Name") e.name = unescape(value);
        else if (key == "GenericName") e.genericName = unescape(value);
        else if (key == "Comment") e.comment = unescape(value);
        else if (key == "Icon") e.icon = value;
        else if (key == "Exec") e.exec = value;
        else if (key == "TryExec") e.tryExec = value;
        else if (key == "Categories") e.categories = splitList(value);
        else if (key == "Keywords") e.keywords = splitList(value);
        else if (key == "OnlyShowIn") e.onlyShowIn = splitList(value);
        else if (key == "NotShowIn") e.notShowIn = splitList(value);
        else if (key == "Type") e.application = value == QLatin1String("Application");
        else if (key == "Terminal") e.terminal = value == QLatin1String("true");
        else if (key == "NoDisplay") e.noDisplay = value == QLatin1String("true");
        else if (key == "Hidden") e.hidden = value == QLatin1String("true");
        else if (key == "X-GNOME-Autostart-enabled") e.autostartOff = value == QLatin1String("false");
    }
    return e;
}

bool DesktopEntry::visible() const
{
    if (!application || noDisplay || hidden || name.isEmpty() || exec.isEmpty())
        return false;
    // only for some other desktop (KDE's or GNOME's own settings, say)
    if (!onlyShowIn.isEmpty() && !onlyShowIn.contains(QLatin1String("Wayland95")))
        return false;
    if (notShowIn.contains(QLatin1String("Wayland95")))
        return false;
    if (!tryExec.isEmpty()) {
        const QFileInfo t(tryExec);
        if (t.isAbsolute() ? !t.isExecutable() : QStandardPaths::findExecutable(tryExec).isEmpty())
            return false;
    }
    return true;
}

QStringList DesktopEntry::command() const
{
    // the spec's quoting: "..." with \" \` \$ \\ escapes; spaces separate
    QStringList args;
    QString current;
    bool quoted = false, any = false;
    const QString e = unescape(exec);
    for (int i = 0; i < e.size(); ++i) {
        const QChar c = e.at(i);
        if (quoted) {
            if (c == QLatin1Char('\\') && i + 1 < e.size()) {
                current += e.at(++i);
            } else if (c == QLatin1Char('"')) {
                quoted = false;
            } else {
                current += c;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
            any = true;
        } else if (c.isSpace()) {
            if (any || !current.isEmpty())
                args << current;
            current.clear();
            any = false;
        } else {
            current += c;
        }
    }
    if (any || !current.isEmpty())
        args << current;

    // field codes: files and URLs (we open none), the icon, name and file
    QStringList out;
    for (const QString &a : std::as_const(args)) {
        if (a == QLatin1String("%f") || a == QLatin1String("%F") || a == QLatin1String("%u") || a == QLatin1String("%U")
            || a == QLatin1String("%d") || a == QLatin1String("%D") || a == QLatin1String("%n") || a == QLatin1String("%N")
            || a == QLatin1String("%v") || a == QLatin1String("%m"))
            continue;
        if (a == QLatin1String("%i")) {
            if (!icon.isEmpty())
                out << QStringLiteral("--icon") << icon;
            continue;
        }
        QString v = a;
        v.replace(QLatin1String("%c"), name).replace(QLatin1String("%k"), path).replace(QLatin1String("%%"), QLatin1String("%"));
        out << v;
    }
    return out;
}

QStringList DesktopEntry::applicationDirs()
{
    QStringList dirs;
    const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME", QDir::homePath() + QStringLiteral("/.local/share"));
    dirs << dataHome + QStringLiteral("/applications")
         << dataHome + QStringLiteral("/flatpak/exports/share/applications")
         << QStringLiteral("/var/lib/flatpak/exports/share/applications");
    for (const QString &d : dataDirs())
        dirs << d + QStringLiteral("/applications");
    return dirs;
}

QList<DesktopEntry> DesktopEntry::all()
{
    QList<DesktopEntry> out;
    QSet<QString> seen;
    for (const QString &dir : applicationDirs()) {
        QDirIterator it(dir, { QStringLiteral("*.desktop") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            // the ID: path below the applications dir, '/' as '-'
            QString id = path.mid(dir.size() + 1);
            id.replace(QLatin1Char('/'), QLatin1Char('-'));
            if (seen.contains(id))
                continue; // an earlier directory's copy wins, hidden or not
            seen.insert(id);
            const DesktopEntry e = read(path, id);
            if (e.visible())
                out << e;
        }
    }
    return out;
}

QString appIconUrl(const QString &icon)
{
    if (icon.isEmpty())
        return QString();
    if (QFileInfo(icon).isAbsolute())
        return QFileInfo::exists(icon) ? QUrl::fromLocalFile(icon).toString() : QString();
    // only icons that exist: QML shows a stand-in for the rest
    return AppIconProvider::find(icon).isEmpty() ? QString() : QStringLiteral("image://appicon/") + icon;
}

// ---------------------------------------------------------------------------

AppIconProvider::AppIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QString AppIconProvider::find(const QString &name)
{
    static QMutex mutex;
    static QHash<QString, QString> cache;
    QMutexLocker lock(&mutex);
    if (auto it = cache.constFind(name); it != cache.cend())
        return *it;

    QString found;
    if (QFileInfo(name).isAbsolute()) {
        found = QFileInfo::exists(name) ? name : QString();
    } else {
        // rebuilt on each lookup miss... cheap: a handful of strings
        QStringList bases{ QDir::homePath() + QStringLiteral("/.local/share/icons"), QDir::homePath() + QStringLiteral("/.icons") };
        for (const QString &d : dataDirs())
            bases << d + QStringLiteral("/icons");
        bases << QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/icons")
              << QStringLiteral("/var/lib/flatpak/exports/share/icons");
        static const QStringList themes{ QStringLiteral("hicolor"), QStringLiteral("Adwaita"), QStringLiteral("breeze"),
                                         QStringLiteral("AdwaitaLegacy"), QStringLiteral("gnome"), QStringLiteral("oxygen/base") };
        static const QStringList sizes{ QStringLiteral("32x32"), QStringLiteral("48x48"), QStringLiteral("64x64"), QStringLiteral("scalable"),
                                        QStringLiteral("128x128"), QStringLiteral("256x256"), QStringLiteral("24x24"), QStringLiteral("22x22"),
                                        QStringLiteral("512x512"), QStringLiteral("16x16") };
        static const QStringList exts{ QStringLiteral(".png"), QStringLiteral(".svg"), QStringLiteral(".svgz"), QStringLiteral(".xpm") };
        auto tryFile = [&](const QString &stem) {
            for (const QString &ext : exts)
                if (QFileInfo::exists(stem + ext))
                    return stem + ext;
            return QString();
        };
        for (const QString &theme : themes) {
            for (const QString &base : std::as_const(bases)) {
                const QString dir = base + QLatin1Char('/') + theme;
                if (!QFileInfo::exists(dir))
                    continue;
                for (const QString &size : sizes)
                    if (found.isEmpty())
                        found = tryFile(QStringLiteral("%1/%2/apps/%3").arg(dir, size, name));
                // breeze's layout: apps/48/name.svg
                for (const char *size : { "48", "32", "64", "22" })
                    if (found.isEmpty())
                        found = tryFile(QStringLiteral("%1/apps/%2/%3").arg(dir, QLatin1String(size), name));
            }
            if (!found.isEmpty())
                break;
        }
        if (found.isEmpty())
            found = tryFile(QStringLiteral("/usr/share/pixmaps/") + name);
    }
    cache.insert(name, found);
    return found;
}

QImage AppIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QSize want = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(32, 32);
    QImage image;
    const QString file = find(id);
    if (!file.isEmpty()) {
        QImageReader reader(file);
        const QSize native = reader.size();
        if (native.isValid() && (native.width() > want.width() || file.endsWith(QLatin1String(".svg")) || file.endsWith(QLatin1String(".svgz"))))
            reader.setScaledSize(native.scaled(want, Qt::KeepAspectRatio));
        image = reader.read();
        if (!image.isNull() && (image.width() > want.width() || image.height() > want.height()))
            image = image.scaled(want, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size)
        *size = image.size();
    return image;
}
