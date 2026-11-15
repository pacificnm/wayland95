#include "statusnotifier.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QtEndian>
#include <QtDebug>

namespace {

const QString WatcherService = QStringLiteral("org.kde.StatusNotifierWatcher");
const QString WatcherPath = QStringLiteral("/StatusNotifierWatcher");
const QString ItemInterface = QStringLiteral("org.kde.StatusNotifierItem");
const QString MenuInterface = QStringLiteral("com.canonical.dbusmenu");
const QString DefaultItemPath = QStringLiteral("/StatusNotifierItem");

QDBusConnection bus() { return QDBusConnection::sessionBus(); }

// "bus/path" or just "bus" -> (bus, path)
void splitService(const QString &service, QString *busName, QString *path)
{
    const qsizetype slash = service.indexOf(QLatin1Char('/'));
    if (slash > 0) {
        *busName = service.left(slash);
        *path = service.mid(slash);
    } else {
        *busName = service;
        *path = DefaultItemPath;
    }
}

struct Pixmap {
    int width = 0;
    int height = 0;
    QByteArray data;
};

// a(iiay): icons as ARGB32 in network byte order, in one or more sizes
QList<Pixmap> readPixmaps(const QDBusArgument &arg)
{
    QList<Pixmap> list;
    arg.beginArray();
    while (!arg.atEnd()) {
        Pixmap p;
        arg.beginStructure();
        arg >> p.width >> p.height >> p.data;
        arg.endStructure();
        list << p;
    }
    arg.endArray();
    return list;
}

QList<Pixmap> pixmapsFrom(const QVariant &v)
{
    if (!v.canConvert<QDBusArgument>())
        return {};
    return readPixmaps(v.value<QDBusArgument>());
}

// the size closest to what the taskbar shows (16, or 32 on HiDPI)
QImage bestPixmap(const QList<Pixmap> &pixmaps)
{
    const Pixmap *best = nullptr;
    for (const Pixmap &p : pixmaps) {
        if (p.width <= 0 || p.height <= 0 || p.data.size() < qsizetype(p.width) * p.height * 4)
            continue;
        if (!best || (best->width < 32 ? p.width > best->width : (p.width >= 32 && p.width < best->width)))
            best = &p;
    }
    if (!best)
        return {};
    QImage image(best->width, best->height, QImage::Format_ARGB32);
    const auto *src = reinterpret_cast<const quint32 *>(best->data.constData());
    for (int y = 0; y < best->height; ++y) {
        auto *line = reinterpret_cast<quint32 *>(image.scanLine(y));
        for (int x = 0; x < best->width; ++x)
            line[x] = qFromBigEndian(src[y * best->width + x]);
    }
    return image;
}

QImage themeIcon(const QString &name, const QString &themePath)
{
    if (name.isEmpty())
        return {};
    if (QFileInfo(name).isAbsolute())
        return QImage(name);
    if (!themePath.isEmpty()) {
        // programs that ship their own icons point at a folder of them
        QStringList paths = QIcon::themeSearchPaths();
        if (!paths.contains(themePath)) {
            paths << themePath;
            QIcon::setThemeSearchPaths(paths);
        }
        for (const char *ext : { ".png", ".svg", ".xpm" }) {
            const QString file = themePath + QLatin1Char('/') + name + QLatin1String(ext);
            if (QFileInfo::exists(file))
                return QImage(file);
        }
    }
    const QIcon icon = QIcon::fromTheme(name);
    return icon.isNull() ? QImage() : icon.pixmap(QSize(32, 32)).toImage();
}

// "_File" -> "File", "Save __as" -> "Save _as"
QString stripMnemonic(QString label)
{
    QString out;
    for (qsizetype i = 0; i < label.size(); ++i) {
        if (label.at(i) == QLatin1Char('_')) {
            if (i + 1 < label.size() && label.at(i + 1) == QLatin1Char('_'))
                out += label.at(++i);
            continue;
        }
        out += label.at(i);
    }
    return out;
}

// com.canonical.dbusmenu layout: (ia{sv}av), children nested in variants
QVariantMap readMenuNode(const QDBusArgument &arg)
{
    int id = 0;
    QVariantMap props;
    QVariantList children;
    arg.beginStructure();
    arg >> id >> props;
    arg.beginArray();
    while (!arg.atEnd()) {
        QDBusVariant child;
        arg >> child;
        const QVariant v = child.variant();
        if (v.canConvert<QDBusArgument>()) {
            const QVariantMap node = readMenuNode(v.value<QDBusArgument>());
            if (node.value(QStringLiteral("visible")).toBool())
                children << node;
        }
    }
    arg.endArray();
    arg.endStructure();

    const QString toggleType = props.value(QStringLiteral("toggle-type")).toString();
    return {
        { QStringLiteral("id"), id },
        { QStringLiteral("label"), stripMnemonic(props.value(QStringLiteral("label")).toString()) },
        { QStringLiteral("enabled"), props.value(QStringLiteral("enabled"), true).toBool() },
        { QStringLiteral("visible"), props.value(QStringLiteral("visible"), true).toBool() },
        { QStringLiteral("separator"), props.value(QStringLiteral("type")).toString() == QLatin1String("separator") },
        { QStringLiteral("checkable"), !toggleType.isEmpty() },
        { QStringLiteral("checked"), props.value(QStringLiteral("toggle-state")).toInt() == 1 },
        { QStringLiteral("children"), children },
    };
}

} // namespace

// ---------------------------------------------------------------------------
// StatusNotifierWatcher

StatusNotifierWatcher::StatusNotifierWatcher(TrayItemsModel *model)
    : QObject(model)
    , m_model(model)
    , m_serviceWatcher(new QDBusServiceWatcher(this))
{
    m_serviceWatcher->setConnection(bus());
    m_serviceWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &StatusNotifierWatcher::serviceGone);
}

void StatusNotifierWatcher::RegisterStatusNotifierItem(const QString &service)
{
    // Most programs pass their bus name; some (libappindicator) pass an
    // object path on their own connection.
    QString full = service;
    if (service.startsWith(QLatin1Char('/')))
        full = message().service() + service;
    QString busName, path;
    splitService(full, &busName, &path);
    if (m_items.contains(full))
        return;
    m_items << full;
    m_serviceWatcher->addWatchedService(busName);
    m_model->addItem(full);
    emit StatusNotifierItemRegistered(full);
}

void StatusNotifierWatcher::RegisterStatusNotifierHost(const QString &service)
{
    Q_UNUSED(service);
    emit StatusNotifierHostRegistered();
}

void StatusNotifierWatcher::serviceGone(const QString &name)
{
    m_serviceWatcher->removeWatchedService(name);
    for (qsizetype i = m_items.size() - 1; i >= 0; --i) {
        QString busName, path;
        splitService(m_items.at(i), &busName, &path);
        if (busName == name) {
            const QString gone = m_items.takeAt(i);
            m_model->removeItem(gone);
            emit StatusNotifierItemUnregistered(gone);
        }
    }
}

// ---------------------------------------------------------------------------
// TrayItemsModel

TrayItemsModel *TrayItemsModel::instance()
{
    static TrayItemsModel *model = new TrayItemsModel(QCoreApplication::instance());
    return model;
}

TrayItemsModel *TrayItemsModel::create(QQmlEngine *, QJSEngine *)
{
    TrayItemsModel *model = instance();
    QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    return model;
}

TrayItemsModel::TrayItemsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // Icon names need an icon theme. A full desktop sets one; on the bare
    // live system, use whichever common theme is installed.
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor")) {
        for (const char *theme : { "Adwaita", "breeze", "Papirus", "hicolor" }) {
            if (QDir(QStringLiteral("/usr/share/icons/") + QLatin1String(theme)).exists()) {
                QIcon::setThemeName(QLatin1String(theme));
                break;
            }
        }
    }
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));

    if (!bus().isConnected()) {
        qInfo("AppTray: no D-Bus session bus; other programs' tray icons are unavailable");
        return;
    }
    setupHost();
}

void TrayItemsModel::setupHost()
{
    QDBusConnection b = bus();
    m_hostName = QStringLiteral("org.kde.StatusNotifierHost-%1").arg(QCoreApplication::applicationPid());
    b.registerService(m_hostName);

    if (b.registerService(WatcherService)) {
        // we're the desktop: programs register with us
        m_watcher = new StatusNotifierWatcher(this);
        b.registerObject(WatcherPath, m_watcher,
                         QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals
                             | QDBusConnection::ExportAllProperties);
        return;
    }

    // Nested inside a desktop that already has a tray: show the same icons.
    b.connect(WatcherService, WatcherPath, WatcherService, QStringLiteral("StatusNotifierItemRegistered"), this,
              SLOT(onWatcherItemRegistered(QString)));
    b.connect(WatcherService, WatcherPath, WatcherService, QStringLiteral("StatusNotifierItemUnregistered"), this,
              SLOT(onWatcherItemUnregistered(QString)));
    QDBusMessage reg = QDBusMessage::createMethodCall(WatcherService, WatcherPath, WatcherService,
                                                      QStringLiteral("RegisterStatusNotifierHost"));
    reg << m_hostName;
    b.asyncCall(reg);

    QDBusMessage get = QDBusMessage::createMethodCall(WatcherService, WatcherPath,
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << WatcherService << QStringLiteral("RegisteredStatusNotifierItems");
    auto *w = new QDBusPendingCallWatcher(b.asyncCall(get, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *w;
        if (reply.isError())
            return;
        for (const QString &service : reply.value().variant().toStringList())
            addItem(service);
    });
}

void TrayItemsModel::onWatcherItemRegistered(const QString &service) { addItem(service); }
void TrayItemsModel::onWatcherItemUnregistered(const QString &service) { removeItem(service); }

int TrayItemsModel::indexOf(const QString &key) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).key == key)
            return i;
    return -1;
}

int TrayItemsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant TrayItemsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Item &item = m_items.at(index.row());
    switch (role) {
    case KeyRole: return item.key;
    case TitleRole: return item.title;
    case ToolTipRole: return item.toolTip.isEmpty() ? item.title : item.toolTip;
    case IconRole:
        return item.image.isNull() ? QString()
                                   : QStringLiteral("image://trayicon/%1/%2").arg(index.row()).arg(item.revision);
    case StatusRole: return item.status;
    case HasMenuRole: return !item.menuPath.isEmpty();
    }
    return {};
}

QHash<int, QByteArray> TrayItemsModel::roleNames() const
{
    return { { KeyRole, "key" }, { TitleRole, "title" }, { ToolTipRole, "description" },
             { IconRole, "iconSource" }, { StatusRole, "status" }, { HasMenuRole, "hasMenu" } };
}

QImage TrayItemsModel::icon(const QString &key) const
{
    // the image URL carries the row; the revision only busts QML's cache
    bool ok = false;
    const int row = key.section(QLatin1Char('/'), 0, 0).toInt(&ok);
    return ok && row >= 0 && row < m_items.size() ? m_items.at(row).image : QImage();
}

void TrayItemsModel::addItem(const QString &service)
{
    if (indexOf(service) >= 0)
        return;
    Item item;
    item.key = service;
    splitService(service, &item.bus, &item.path);
    item.owner = item.bus;
    if (!item.bus.startsWith(QLatin1Char(':'))) {
        const QDBusReply<QString> owner = bus().interface()->serviceOwner(item.bus);
        if (owner.isValid())
            item.owner = owner.value();
    }
    beginInsertRows({}, int(m_items.size()), int(m_items.size()));
    m_items << item;
    endInsertRows();

    for (const char *signal : { "NewTitle", "NewIcon", "NewAttentionIcon", "NewOverlayIcon", "NewToolTip",
                                "NewStatus", "NewMenu" }) {
        bus().connect(item.bus, item.path, ItemInterface, QLatin1String(signal), this,
                      SLOT(onItemSignal(QDBusMessage)));
    }
    fetchProperties(service);
}

void TrayItemsModel::removeItem(const QString &service)
{
    const int row = indexOf(service);
    if (row < 0)
        return;
    const Item item = m_items.at(row);
    for (const char *signal : { "NewTitle", "NewIcon", "NewAttentionIcon", "NewOverlayIcon", "NewToolTip",
                                "NewStatus", "NewMenu" }) {
        bus().disconnect(item.bus, item.path, ItemInterface, QLatin1String(signal), this,
                         SLOT(onItemSignal(QDBusMessage)));
    }
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    // rows after it moved: their image URLs (which carry the row) changed
    if (row < m_items.size()) {
        for (int i = row; i < m_items.size(); ++i)
            ++m_items[i].revision;
        emit dataChanged(index(row), index(int(m_items.size()) - 1), { IconRole });
    }
}

void TrayItemsModel::removeOwner(const QString &busName)
{
    for (qsizetype i = m_items.size() - 1; i >= 0; --i)
        if (m_items.at(i).bus == busName || m_items.at(i).owner == busName)
            removeItem(m_items.at(i).key);
}

void TrayItemsModel::onItemSignal(const QDBusMessage &message)
{
    for (const Item &item : std::as_const(m_items)) {
        if (item.path == message.path() && (item.owner == message.service() || item.bus == message.service())) {
            fetchProperties(item.key);
            return;
        }
    }
}

void TrayItemsModel::fetchProperties(const QString &key)
{
    const int row = indexOf(key);
    if (row < 0)
        return;
    const Item &item = m_items.at(row);
    QDBusMessage call = QDBusMessage::createMethodCall(item.bus, item.path,
                                                       QStringLiteral("org.freedesktop.DBus.Properties"),
                                                       QStringLiteral("GetAll"));
    call << ItemInterface;
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(call, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, key](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        if (!reply.isError())
            applyProperties(key, reply.value());
    });
}

void TrayItemsModel::applyProperties(const QString &key, const QVariantMap &props)
{
    const int row = indexOf(key);
    if (row < 0)
        return;
    Item &item = m_items[row];
    item.id = props.value(QStringLiteral("Id")).toString();
    item.title = props.value(QStringLiteral("Title")).toString();
    if (item.title.isEmpty())
        item.title = item.id;
    item.status = props.value(QStringLiteral("Status")).toString();
    item.itemIsMenu = props.value(QStringLiteral("ItemIsMenu")).toBool();
    item.menuPath = props.value(QStringLiteral("Menu")).value<QDBusObjectPath>().path();
    if (item.menuPath == QLatin1String("/"))
        item.menuPath.clear();

    // ToolTip: (icon name, icon pixmaps, title, description)
    item.toolTip.clear();
    const QVariant tip = props.value(QStringLiteral("ToolTip"));
    if (tip.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = tip.value<QDBusArgument>();
        QString iconName, title, text;
        arg.beginStructure();
        arg >> iconName;
        readPixmaps(arg);
        arg >> title >> text;
        arg.endStructure();
        item.toolTip = text.isEmpty() ? title : title + QLatin1Char('\n') + text;
        item.toolTip.remove(QRegularExpression(QStringLiteral("<[^>]*>"))); // some send HTML
    }

    const bool attention = item.status == QLatin1String("NeedsAttention");
    const QString themePath = props.value(QStringLiteral("IconThemePath")).toString();
    QImage image;
    if (attention) {
        image = bestPixmap(pixmapsFrom(props.value(QStringLiteral("AttentionIconPixmap"))));
        if (image.isNull())
            image = themeIcon(props.value(QStringLiteral("AttentionIconName")).toString(), themePath);
    }
    if (image.isNull())
        image = themeIcon(props.value(QStringLiteral("IconName")).toString(), themePath);
    if (image.isNull())
        image = bestPixmap(pixmapsFrom(props.value(QStringLiteral("IconPixmap"))));
    item.image = image;
    ++item.revision;

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

void TrayItemsModel::callItem(const QString &key, const QString &method, const QVariantList &args)
{
    const int row = indexOf(key);
    if (row < 0)
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(m_items.at(row).bus, m_items.at(row).path, ItemInterface, method);
    call.setArguments(args);
    bus().asyncCall(call);
}

void TrayItemsModel::activate(const QString &key, int x, int y)
{
    const int row = indexOf(key);
    if (row >= 0 && m_items.at(row).itemIsMenu)
        requestMenu(key, x, y);
    else
        callItem(key, QStringLiteral("Activate"), { x, y });
}

void TrayItemsModel::secondaryActivate(const QString &key, int x, int y)
{
    callItem(key, QStringLiteral("SecondaryActivate"), { x, y });
}

void TrayItemsModel::scroll(const QString &key, int delta)
{
    callItem(key, QStringLiteral("Scroll"), { delta, QStringLiteral("vertical") });
}

void TrayItemsModel::requestMenu(const QString &key, int x, int y)
{
    const int row = indexOf(key);
    if (row < 0)
        return;
    const Item &item = m_items.at(row);
    if (item.menuPath.isEmpty()) {
        // no exported menu: the program draws its own
        callItem(key, QStringLiteral("ContextMenu"), { x, y });
        return;
    }
    QDBusMessage about = QDBusMessage::createMethodCall(item.bus, item.menuPath, MenuInterface,
                                                        QStringLiteral("AboutToShow"));
    about << 0;
    bus().asyncCall(about);

    QDBusMessage call = QDBusMessage::createMethodCall(item.bus, item.menuPath, MenuInterface,
                                                       QStringLiteral("GetLayout"));
    call << 0 << -1 << QStringList();
    auto *w = new QDBusPendingCallWatcher(bus().asyncCall(call, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, key, x, y](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
            callItem(key, QStringLiteral("ContextMenu"), { x, y });
            return;
        }
        const QVariantMap root = readMenuNode(reply.arguments().at(1).value<QDBusArgument>());
        emit menuReady(key, root.value(QStringLiteral("children")).toList(), x, y);
    });
}

void TrayItemsModel::menuItemClicked(const QString &key, int id)
{
    const int row = indexOf(key);
    if (row < 0 || m_items.at(row).menuPath.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(m_items.at(row).bus, m_items.at(row).menuPath, MenuInterface,
                                                       QStringLiteral("Event"));
    call << id << QStringLiteral("clicked") << QVariant::fromValue(QDBusVariant(QString())) << 0u;
    bus().asyncCall(call);
}

// ---------------------------------------------------------------------------

QImage TrayIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage image = TrayItemsModel::instance()->icon(id);
    if (!image.isNull() && requestedSize.isValid())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}
