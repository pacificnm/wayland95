#include "filechooser.h"

#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QQmlComponent>
#include <QUrl>
#include <QtDebug>

QDBusArgument &operator<<(QDBusArgument &arg, const PortalFilter &f)
{
    arg.beginStructure();
    arg << f.name;
    arg.beginArray(qMetaTypeId<std::pair<uint, QString>>());
    for (const auto &[type, pattern] : f.patterns) {
        arg.beginStructure();
        arg << type << pattern;
        arg.endStructure();
    }
    arg.endArray();
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalFilter &f)
{
    f = PortalFilter();
    arg.beginStructure();
    arg >> f.name;
    arg.beginArray();
    while (!arg.atEnd()) {
        uint type = 0;
        QString pattern;
        arg.beginStructure();
        arg >> type >> pattern;
        arg.endStructure();
        f.patterns << std::pair{ type, pattern };
    }
    arg.endArray();
    arg.endStructure();
    return arg;
}

namespace {

// a path sent as bytes (ay), NUL-terminated
QString bytesPath(const QVariant &v)
{
    QByteArray b = v.toByteArray();
    if (b.endsWith('\0'))
        b.chop(1);
    return QFile::decodeName(b);
}

QList<PortalFilter> filtersOf(const QVariant &v)
{
    QList<PortalFilter> out;
    if (v.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = v.value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            PortalFilter f;
            arg >> f;
            out << f;
        }
        arg.endArray();
    }
    return out;
}

PortalFilter filterOf(const QVariant &v)
{
    PortalFilter f;
    if (v.canConvert<QDBusArgument>())
        v.value<QDBusArgument>() >> f;
    return f;
}

// for the dialog: { label, patterns } with MIME types turned into globs
QVariantMap forQml(const PortalFilter &f)
{
    static QMimeDatabase mimes;
    QStringList globs;
    for (const auto &[type, pattern] : f.patterns) {
        if (type == 0)
            globs << pattern;
        else
            globs << mimes.mimeTypeForName(pattern).globPatterns();
    }
    if (globs.isEmpty())
        globs << QStringLiteral("*");
    globs.removeDuplicates();
    // "Images (*.png *.jpg)", like Win95's lists
    QString label = f.name;
    if (!label.contains(QLatin1Char('(')))
        label += QStringLiteral(" (") + globs.join(QLatin1Char(' ')) + QLatin1Char(')');
    return { { QStringLiteral("label"), label }, { QStringLiteral("patterns"), globs } };
}

} // namespace

// ---------------------------------------------------------------------------

PortalRequest::PortalRequest(FileChooserPortal *portal, const QString &handle)
    : QObject(portal)
    , m_portal(portal)
    , m_handle(handle)
{
}

void PortalRequest::Close() { m_portal->close(m_handle); }

// ---------------------------------------------------------------------------

FileChooserPortal::FileChooserPortal(QQmlEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    qRegisterMetaType<PortalFilter>();
    qDBusRegisterMetaType<PortalFilter>();
    qDBusRegisterMetaType<std::pair<uint, QString>>();
    qDBusRegisterMetaType<QList<PortalFilter>>();
}

uint FileChooserPortal::OpenFile(const QDBusObjectPath &handle, const QString &, const QString &, const QString &title,
                                 const QVariantMap &options, QVariantMap &)
{
    open(options.value(QStringLiteral("directory")).toBool() ? QStringLiteral("folder") : QStringLiteral("open"), handle, title, options);
    return 0;
}

uint FileChooserPortal::SaveFile(const QDBusObjectPath &handle, const QString &, const QString &, const QString &title,
                                 const QVariantMap &options, QVariantMap &)
{
    open(QStringLiteral("save"), handle, title, options);
    return 0;
}

uint FileChooserPortal::SaveFiles(const QDBusObjectPath &handle, const QString &, const QString &, const QString &title,
                                  const QVariantMap &options, QVariantMap &)
{
    // several files into one folder: choose the folder
    open(QStringLiteral("folder"), handle, title, options);
    return 0;
}

void FileChooserPortal::open(const QString &mode, const QDBusObjectPath &handle, const QString &title, const QVariantMap &options)
{
    setDelayedReply(true);
    Request r;
    r.call = message();
    r.filters = filtersOf(options.value(QStringLiteral("filters")));

    QVariantList filters;
    for (const PortalFilter &f : std::as_const(r.filters))
        filters << forQml(f);
    int filterIndex = 0;
    if (options.contains(QStringLiteral("current_filter"))) {
        const PortalFilter current = filterOf(options.value(QStringLiteral("current_filter")));
        for (int i = 0; i < r.filters.size(); ++i)
            if (r.filters.at(i).name == current.name)
                filterIndex = i;
    }
    if (filters.isEmpty() && mode != QLatin1String("folder"))
        filters << QVariantMap{ { QStringLiteral("label"), QStringLiteral("All Files (*.*)") },
                                { QStringLiteral("patterns"), QStringList{ QStringLiteral("*") } } };

    QString folder = bytesPath(options.value(QStringLiteral("current_folder")));
    QString name = options.value(QStringLiteral("current_name")).toString();
    const QString file = bytesPath(options.value(QStringLiteral("current_file")));
    if (!file.isEmpty()) {
        folder = QFileInfo(file).absolutePath();
        name = QFileInfo(file).fileName();
    }
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        folder = QDir::homePath();

    QQmlComponent component(m_engine, QStringLiteral("Win95Portal"), QStringLiteral("FileChooserWindow"));
    QObject *window = component.createWithInitialProperties({
        { QStringLiteral("mode"), mode },
        { QStringLiteral("dialogTitle"), title },
        { QStringLiteral("acceptLabel"), options.value(QStringLiteral("accept_label")).toString().remove(QLatin1Char('_')) },
        { QStringLiteral("startFolder"), folder },
        { QStringLiteral("startName"), name },
        { QStringLiteral("filters"), filters },
        { QStringLiteral("filterIndex"), filterIndex },
        { QStringLiteral("multiple"), options.value(QStringLiteral("multiple")).toBool() },
    });
    if (!window) {
        qWarning().noquote() << "win95portal: can't make the dialog:" << component.errorString();
        QDBusConnection::sessionBus().send(r.call.createReply({ uint(2), QVariantMap() }));
        return;
    }
    const QString key = handle.path();
    r.window = window;
    // the window answers through done(); closing it is a cancel
    window->setProperty("requestKey", key);
    window->setProperty("portal", QVariant::fromValue<QObject *>(this));
    QObject::connect(window, &QObject::destroyed, this, [this, key] {
        if (m_requests.contains(key))
            finished(key, false, {}, 0);
    });

    r.object = new PortalRequest(this, key);
    QDBusConnection::sessionBus().registerObject(key, r.object, QDBusConnection::ExportAllSlots);
    m_requests.insert(key, r);
    emit idleChanged();
}

void FileChooserPortal::done(const QString &key, bool ok, const QStringList &paths, int filterIndex)
{
    if (m_requests.contains(key))
        finished(key, ok, paths, filterIndex);
}

void FileChooserPortal::close(const QString &handle)
{
    if (!m_requests.contains(handle))
        return;
    finished(handle, false, {}, 0);
}

void FileChooserPortal::finished(const QString &handle, bool ok, const QStringList &paths, int filterIndex)
{
    Request r = m_requests.take(handle);
    QDBusConnection::sessionBus().unregisterObject(handle);
    if (r.object)
        r.object->deleteLater();
    if (r.window)
        r.window->deleteLater();

    QVariantMap results;
    if (ok) {
        QStringList uris;
        for (const QString &p : paths)
            uris << QUrl::fromLocalFile(p).toString();
        results.insert(QStringLiteral("uris"), uris);
        if (filterIndex >= 0 && filterIndex < r.filters.size())
            results.insert(QStringLiteral("current_filter"), QVariant::fromValue(r.filters.at(filterIndex)));
    }
    // 0 = done, 1 = canceled by the user
    QDBusConnection::sessionBus().send(r.call.createReply({ uint(ok ? 0 : 1), results }));
    emit idleChanged();
}
