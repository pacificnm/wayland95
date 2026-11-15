#include "notifications.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDateTime>
#include <QFileInfo>
#include <QIcon>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QUrl>

namespace {

constexpr int DefaultTimeoutMs = 7000;
constexpr int HistoryLength = 50;

// Bodies may use a little markup (b, i, u, a, br); anything else is shown
// as text. Plain text is escaped so a "<" doesn't vanish.
QString cleanBody(const QString &body)
{
    static const QRegularExpression anyTag(QStringLiteral("<[a-zA-Z/!?]"));
    if (!anyTag.match(body).hasMatch())
        return body.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    QString s = body;
    s.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    // drop images and anything not on the list, keep their text
    static const QRegularExpression img(QStringLiteral("<img[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    s.remove(img);
    static const QRegularExpression tag(QStringLiteral("</?([a-zA-Z0-9]+)[^>]*>"));
    QString out;
    qsizetype last = 0;
    auto it = tag.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        const QString name = m.captured(1).toLower();
        if (name == QLatin1String("b") || name == QLatin1String("i") || name == QLatin1String("u")
            || name == QLatin1String("a") || name == QLatin1String("br"))
            out += m.captured(0);
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

// hints "image-data": (width, height, rowstride, has alpha, bits, channels, bytes)
QImage imageFromData(const QVariant &v)
{
    if (!v.canConvert<QDBusArgument>())
        return {};
    const QDBusArgument arg = v.value<QDBusArgument>();
    int width = 0, height = 0, rowstride = 0, bits = 0, channels = 0;
    bool alpha = false;
    QByteArray data;
    arg.beginStructure();
    arg >> width >> height >> rowstride >> alpha >> bits >> channels >> data;
    arg.endStructure();
    if (width <= 0 || height <= 0 || bits != 8 || (channels != 3 && channels != 4)
        || data.size() < qsizetype(rowstride) * (height - 1) + width * channels)
        return {};
    const QImage view(reinterpret_cast<const uchar *>(data.constData()), width, height, rowstride,
                      channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    return view.copy(); // `data` goes away
}

QImage imageFromName(const QString &nameOrPath)
{
    if (nameOrPath.isEmpty())
        return {};
    QString path = nameOrPath;
    if (path.startsWith(QLatin1String("file://")))
        path = QUrl(path).toLocalFile();
    if (QFileInfo(path).isAbsolute())
        return QImage(path);
    const QIcon icon = QIcon::fromTheme(nameOrPath);
    return icon.isNull() ? QImage() : icon.pixmap(QSize(48, 48)).toImage();
}

} // namespace

// ---------------------------------------------------------------------------
// D-Bus interface

FreedesktopNotifications::FreedesktopNotifications(NotificationServer *server)
    : QObject(server)
    , m_server(server)
{
}

QStringList FreedesktopNotifications::GetCapabilities()
{
    return { QStringLiteral("actions"), QStringLiteral("body"), QStringLiteral("body-markup"),
             QStringLiteral("body-hyperlinks"), QStringLiteral("icon-static"), QStringLiteral("persistence"),
             QStringLiteral("sound") };
}

uint FreedesktopNotifications::Notify(const QString &appName, uint replacesId, const QString &appIcon,
                                      const QString &summary, const QString &body, const QStringList &actions,
                                      const QVariantMap &hints, int expireTimeout)
{
    return m_server->add(appName, replacesId, appIcon, summary, body, actions, hints, expireTimeout);
}

void FreedesktopNotifications::CloseNotification(uint id)
{
    m_server->close(id, 3);
}

QString FreedesktopNotifications::GetServerInformation(QString &vendor, QString &version, QString &specVersion)
{
    vendor = QStringLiteral("Wayland 95");
    version = QStringLiteral("0.1");
    specVersion = QStringLiteral("1.2");
    return QStringLiteral("Wayland 95");
}

// ---------------------------------------------------------------------------
// NotificationServer

NotificationServer *NotificationServer::instance()
{
    static NotificationServer *server = new NotificationServer(QCoreApplication::instance());
    return server;
}

NotificationServer *NotificationServer::create(QQmlEngine *, QJSEngine *)
{
    NotificationServer *server = instance();
    QQmlEngine::setObjectOwnership(server, QQmlEngine::CppOwnership);
    return server;
}

NotificationServer::NotificationServer(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.registerService(QStringLiteral("org.freedesktop.Notifications"))) {
        m_service = new FreedesktopNotifications(this);
        bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"), m_service,
                           QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
    } else {
        qInfo("Notifications: another notification service is running; programs' notifications go there");
    }
}

void NotificationServer::setQuiet(bool quiet)
{
    if (quiet == m_quiet)
        return;
    m_quiet = quiet;
    emit quietChanged();
}

int NotificationServer::popupIndex(uint id) const
{
    for (int i = 0; i < m_popups.size(); ++i)
        if (m_popups.at(i).toMap().value(QStringLiteral("id")).toUInt() == id)
            return i;
    return -1;
}

int NotificationServer::historyIndex(uint id) const
{
    for (int i = 0; i < m_history.size(); ++i)
        if (m_history.at(i).toMap().value(QStringLiteral("id")).toUInt() == id)
            return i;
    return -1;
}

int NotificationServer::unreadCount() const
{
    int n = 0;
    for (const QVariant &v : m_history)
        if (v.toMap().value(QStringLiteral("unread")).toBool())
            ++n;
    return n;
}

uint NotificationServer::notify(const QString &appName, const QString &summary, const QString &body,
                                const QString &icon, int urgency, int timeoutMs)
{
    return add(appName, 0, icon, summary, body, {}, { { QStringLiteral("urgency"), urgency } }, timeoutMs);
}

uint NotificationServer::notifyWithActions(const QString &appName, const QString &summary, const QString &body,
                                           const QStringList &actions, int urgency, int timeoutMs)
{
    return add(appName, 0, {}, summary, body, actions, { { QStringLiteral("urgency"), urgency } }, timeoutMs);
}

uint NotificationServer::add(const QString &appName, uint replacesId, const QString &appIcon,
                             const QString &summary, const QString &body, const QStringList &actions,
                             const QVariantMap &hints, int expireTimeout)
{
    const bool replacing = replacesId > 0 && (popupIndex(replacesId) >= 0 || historyIndex(replacesId) >= 0);
    const uint id = replacing ? replacesId : m_nextId++;
    const int urgency = qBound(0, hints.value(QStringLiteral("urgency"), 1).toInt(), 2);

    // the picture: image data, then an image path, then the program's icon
    QImage image;
    for (const char *key : { "image-data", "image_data", "icon_data" }) {
        if (image.isNull())
            image = imageFromData(hints.value(QLatin1String(key)));
    }
    for (const char *key : { "image-path", "image_path" }) {
        if (image.isNull())
            image = imageFromName(hints.value(QLatin1String(key)).toString());
    }
    if (image.isNull())
        image = imageFromName(appIcon);
    static int revision = 0;
    if (!image.isNull())
        m_images.insert(id, image);
    else
        m_images.remove(id);

    // actions come as [key, label, key, label...]
    QVariantList actionList;
    bool hasDefault = false;
    for (int i = 0; i + 1 < actions.size(); i += 2) {
        if (actions.at(i) == QLatin1String("default"))
            hasDefault = true;
        else
            actionList << QVariantMap{ { QStringLiteral("key"), actions.at(i) }, { QStringLiteral("label"), actions.at(i + 1) } };
    }
    m_resident.insert(id, hints.value(QStringLiteral("resident")).toBool());

    // critical ones stay until dismissed; 0 means "never expire" too
    const int timeout = urgency == 2 ? 0 : expireTimeout < 0 ? DefaultTimeoutMs : expireTimeout;
    QVariantMap n{
        { QStringLiteral("id"), id },
        { QStringLiteral("appName"), appName.isEmpty() ? QStringLiteral("Notification") : appName },
        { QStringLiteral("summary"), summary },
        { QStringLiteral("body"), cleanBody(body) },
        { QStringLiteral("icon"), image.isNull() ? QString()
                                                 : QStringLiteral("image://notification/%1/%2").arg(id).arg(++revision) },
        { QStringLiteral("urgency"), urgency },
        { QStringLiteral("timeout"), timeout },
        { QStringLiteral("actions"), actionList },
        { QStringLiteral("hasDefault"), hasDefault },
        { QStringLiteral("time"), QDateTime::currentMSecsSinceEpoch() },
        { QStringLiteral("silent"), hints.value(QStringLiteral("suppress-sound")).toBool() },
    };

    // the history (not for "transient" ones, which only matter right now)
    const bool transient = hints.value(QStringLiteral("transient")).toBool();
    const bool show = !m_quiet || urgency == 2;
    QVariantMap h = n;
    h.insert(QStringLiteral("unread"), !show);
    const int hi = historyIndex(id);
    if (hi >= 0)
        m_history[hi] = h;
    else if (!transient)
        m_history.prepend(h);
    while (m_history.size() > HistoryLength)
        m_history.removeLast();
    emit historyChanged();

    if (show) {
        const int pi = popupIndex(id);
        if (pi >= 0)
            m_popups[pi] = n;
        else
            m_popups << n;
        emit popupsChanged();
    }
    if (!replacing)
        emit arrived(n, show);
    return id;
}

void NotificationServer::close(uint id, uint reason)
{
    const int pi = popupIndex(id);
    if (pi >= 0) {
        m_popups.removeAt(pi);
        emit popupsChanged();
    }
    if (reason == 3) {
        // the program withdrew it: it's no longer news
        const int hi = historyIndex(id);
        if (hi >= 0) {
            m_history.removeAt(hi);
            emit historyChanged();
        }
    }
    if (m_service)
        emit m_service->NotificationClosed(id, reason);
}

void NotificationServer::markRead(uint id)
{
    const int hi = historyIndex(id);
    if (hi < 0)
        return;
    QVariantMap h = m_history.at(hi).toMap();
    if (!h.value(QStringLiteral("unread")).toBool())
        return;
    h.insert(QStringLiteral("unread"), false);
    m_history[hi] = h;
    emit historyChanged();
}

void NotificationServer::invokeAction(uint id, const QString &key)
{
    if (m_service)
        emit m_service->ActionInvoked(id, key);
    emit actionInvoked(id, key);
    markRead(id);
    if (!m_resident.value(id))
        close(id, 2);
}

void NotificationServer::dismiss(uint id)
{
    markRead(id);
    close(id, 2);
}

void NotificationServer::expire(uint id)
{
    // nobody saw it go: leave it unread
    const int hi = historyIndex(id);
    if (hi >= 0) {
        QVariantMap h = m_history.at(hi).toMap();
        h.insert(QStringLiteral("unread"), true);
        m_history[hi] = h;
        emit historyChanged();
    }
    close(id, 1);
}

void NotificationServer::markAllRead()
{
    for (QVariant &v : m_history) {
        QVariantMap h = v.toMap();
        h.insert(QStringLiteral("unread"), false);
        v = h;
    }
    emit historyChanged();
}

void NotificationServer::clearHistory()
{
    m_history.clear();
    emit historyChanged();
}

QImage NotificationServer::image(const QString &key) const
{
    return m_images.value(key.section(QLatin1Char('/'), 0, 0).toUInt());
}

QImage NotificationImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage image = NotificationServer::instance()->image(id);
    if (!image.isNull() && requestedSize.isValid())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}
