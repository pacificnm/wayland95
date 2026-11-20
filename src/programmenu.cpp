#include "programmenu.h"

#include "desktopentry.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QtDebug>

#include <algorithm>

namespace {

// freedesktop main categories, most specific first, to Win95-ish folders
QString folderFor(const QStringList &cats)
{
    static const QList<std::pair<const char *, const char *>> map = {
        { "Game", "Games" }, { "Development", "Programming" }, { "Office", "Office" },
        { "Graphics", "Graphics" }, { "AudioVideo", "Multimedia" }, { "Audio", "Multimedia" },
        { "Video", "Multimedia" }, { "Network", "Internet" }, { "Education", "Education" },
        { "Science", "Science" }, { "Utility", "Accessories" }, { "Settings", "System Tools" },
        { "System", "System Tools" },
    };
    for (const auto &[from, to] : map)
        if (cats.contains(QLatin1String(from)))
            return QLatin1String(to);
    return QStringLiteral("Other");
}

} // namespace

ProgramMenu::ProgramMenu(QObject *parent)
    : QObject(parent)
{
    // installs touch several files in a row: read them once it settles
    m_reload.setSingleShot(true);
    m_reload.setInterval(800);
    connect(&m_reload, &QTimer::timeout, this, &ProgramMenu::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_reload.start(); });
    reload();
}

void ProgramMenu::watch()
{
    // the applications dirs, and their parents for dirs that don't exist
    // yet (~/.local/share/flatpak/exports/... appears with the first app)
    QStringList paths;
    for (const QString &dir : DesktopEntry::applicationDirs()) {
        QString d = dir;
        while (!d.isEmpty() && !QFileInfo::exists(d))
            d = QFileInfo(d).path();
        if (!d.isEmpty() && d != QLatin1String("/") && !paths.contains(d))
            paths << d;
    }
    const QStringList old = m_watcher.directories();
    if (!old.isEmpty())
        m_watcher.removePaths(old);
    m_watcher.addPaths(paths);
}

void ProgramMenu::reload()
{
    QMap<QString, QList<QVariantMap>> folders;
    m_paths.clear();
    for (const DesktopEntry &e : DesktopEntry::all()) {
        m_paths.insert(e.id, e.path);
        folders[folderFor(e.categories)] << QVariantMap{
            { QStringLiteral("label"), e.name },
            { QStringLiteral("iconSource"), appIconUrl(e.icon) },
            { QStringLiteral("desktopId"), e.id },
            { QStringLiteral("comment"), e.comment },
        };
    }

    QVariantList list;
    for (auto it = folders.begin(); it != folders.end(); ++it) {
        auto &items = it.value();
        std::sort(items.begin(), items.end(), [](const QVariantMap &a, const QVariantMap &b) {
            return a.value(QStringLiteral("label")).toString().compare(b.value(QStringLiteral("label")).toString(), Qt::CaseInsensitive) < 0;
        });
        QVariantList v;
        for (const QVariantMap &m : std::as_const(items))
            v << m;
        list << QVariantMap{ { QStringLiteral("label"), it.key() }, { QStringLiteral("items"), v } };
    }
    m_folders = list;
    watch();
    emit changed();
}

bool ProgramMenu::launch(const QString &desktopId)
{
    return start(desktopId, false);
}

bool ProgramMenu::launchAsRoot(const QString &desktopId)
{
    return start(desktopId, true);
}

bool ProgramMenu::start(const QString &desktopId, bool asRoot)
{
    const QString path = m_paths.value(desktopId);
    if (path.isEmpty())
        return false;
    return m_launcher.launchEntry(DesktopEntry::read(path, desktopId), asRoot);
}

QString ProgramMenu::iconFor(const QString &appId) const
{
    if (appId.isEmpty())
        return QString();
    // "org.gnome.Foo" -> org.gnome.Foo.desktop; "Code" (an X11 class) -> code.desktop
    for (const QString &id : { appId, appId.toLower() }) {
        const QString path = m_paths.value(id + QStringLiteral(".desktop"));
        if (!path.isEmpty()) {
            const QString url = appIconUrl(DesktopEntry::read(path).icon);
            if (!url.isEmpty())
                return url;
        }
    }
    QString url = appIconUrl(appId);
    return url.isEmpty() ? appIconUrl(appId.toLower()) : url;
}
