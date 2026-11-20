#include "appmpris.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace {
const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
int instances = 0;
}

MprisService::MprisService(QObject *parent)
    : QObject(parent)
{
}

MprisService::~MprisService()
{
    unregisterService();
}

void MprisService::setActive(bool on)
{
    if (on == m_active)
        return;
    m_active = on;
    if (on)
        registerService();
    else
        unregisterService();
    emit activeChanged();
}

void MprisService::registerService()
{
    if (!m_service.isEmpty() || qEnvironmentVariableIntValue("W95APP_NO_MPRIS"))
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    QString app = m_desktopEntry.isEmpty() ? QCoreApplication::applicationName() : m_desktopEntry;
    app.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
    if (app.isEmpty() || app.at(0).isDigit())
        app.prepend(QLatin1Char('a'));
    // (each its own: several players, or the program twice)
    const QString name = QStringLiteral("org.mpris.MediaPlayer2.%1.instance%2_%3")
                             .arg(app).arg(QCoreApplication::applicationPid()).arg(++instances);
    m_holder = new QObject(this);
    new MprisRootAdaptor(m_holder, this);
    new MprisPlayerAdaptor(m_holder, this);
    // (one object path per connection: a second player in the program gets a
    // connection of its own)
    QDBusConnection conn = instances == 1 ? bus
        : QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("w95mpris%1").arg(instances));
    if (!conn.registerObject(kPath, m_holder, QDBusConnection::ExportAdaptors) || !conn.registerService(name)) {
        qWarning("MediaPlayer: MediaKeys: %s", qPrintable(conn.lastError().message()));
        delete m_holder;
        m_holder = nullptr;
        return;
    }
    m_holder->setProperty("w95conn", conn.name());
    m_service = name;
    emit registeredChanged();
}

void MprisService::unregisterService()
{
    if (m_service.isEmpty())
        return;
    QDBusConnection conn(m_holder->property("w95conn").toString());
    conn.unregisterService(m_service);
    conn.unregisterObject(kPath);
    delete m_holder;
    m_holder = nullptr;
    m_service.clear();
    emit registeredChanged();
}

void MprisService::announce(const QString &interface, const QVariantMap &changedProps)
{
    if (m_service.isEmpty())
        return;
    QDBusMessage signal = QDBusMessage::createSignal(kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                     QStringLiteral("PropertiesChanged"));
    signal << interface << changedProps << QStringList();
    QDBusConnection(m_holder->property("w95conn").toString()).send(signal);
}

QVariantMap MprisService::metadata() const
{
    QVariantMap m;
    m.insert(QStringLiteral("mpris:trackid"),
             QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/wayland95/track/%1").arg(m_track))));
    if (m_length > 0)
        m.insert(QStringLiteral("mpris:length"), qlonglong(m_length * 1000));
    m.insert(QStringLiteral("xesam:title"), m_title);
    if (!m_artist.isEmpty())
        m.insert(QStringLiteral("xesam:artist"), QStringList{ m_artist });
    if (!m_album.isEmpty())
        m.insert(QStringLiteral("xesam:album"), m_album);
    if (!m_artUrl.isEmpty())
        m.insert(QStringLiteral("mpris:artUrl"), m_artUrl);
    return m;
}

void MprisService::metadataChanged()
{
    emit changed();
    announce(QStringLiteral("org.mpris.MediaPlayer2.Player"), { { QStringLiteral("Metadata"), metadata() } });
}

void MprisService::setPlaybackStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit changed();
    announce(QStringLiteral("org.mpris.MediaPlayer2.Player"), { { QStringLiteral("PlaybackStatus"), s } });
}

void MprisService::setTitle(const QString &s)
{
    if (s == m_title)
        return;
    m_title = s;
    ++m_track;
    metadataChanged();
}

void MprisService::setArtist(const QString &s) { if (s != m_artist) { m_artist = s; metadataChanged(); } }
void MprisService::setAlbum(const QString &s) { if (s != m_album) { m_album = s; metadataChanged(); } }
void MprisService::setArtUrl(const QString &s) { if (s != m_artUrl) { m_artUrl = s; metadataChanged(); } }
void MprisService::setLength(qint64 ms) { if (ms != m_length) { m_length = ms; metadataChanged(); } }

void MprisService::setVolume(qreal v)
{
    v = qBound<qreal>(0, v, 1);
    if (qFuzzyCompare(v + 1, m_volume + 1))
        return;
    m_volume = v;
    emit changed();
    announce(QStringLiteral("org.mpris.MediaPlayer2.Player"), { { QStringLiteral("Volume"), double(v) } });
}

void MprisService::seeked(qint64 ms)
{
    if (m_service.isEmpty())
        return;
    QDBusMessage signal = QDBusMessage::createSignal(kPath, QStringLiteral("org.mpris.MediaPlayer2.Player"), QStringLiteral("Seeked"));
    signal << qlonglong(ms * 1000);
    QDBusConnection(m_holder->property("w95conn").toString()).send(signal);
}

QString MprisService::imageUrl(const QVariant &value)
{
    QImage image = value.value<QImage>();
    if (image.isNull())
        return QString();
    if (image.width() > 600 || image.height() > 600)
        image = image.scaled(600, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QByteArray bits(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes());
    const QString key = QString::fromLatin1(QCryptographicHash::hash(bits, QCryptographicHash::Sha1).toHex().left(20));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/covers");
    const QString path = dir + QLatin1Char('/') + key + QStringLiteral(".png");
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(dir);
        if (!image.save(path, "PNG"))
            return QString();
    }
    return QUrl::fromLocalFile(path).toString();
}

// --- the adaptors ---------------------------------------------------------------

MprisRootAdaptor::MprisRootAdaptor(QObject *holder, MprisService *service)
    : QDBusAbstractAdaptor(holder), m_s(service) {}
QString MprisRootAdaptor::identity() const { return m_s->property("identity").toString(); }
QString MprisRootAdaptor::desktopEntry() const { return m_s->property("desktopEntry").toString(); }
void MprisRootAdaptor::Raise() { emit m_s->raiseRequested(); }

MprisPlayerAdaptor::MprisPlayerAdaptor(QObject *holder, MprisService *service)
    : QDBusAbstractAdaptor(holder), m_s(service) {}
QString MprisPlayerAdaptor::playbackStatus() const { return m_s->playbackStatus(); }
QVariantMap MprisPlayerAdaptor::metadata() const { return m_s->metadata(); }
double MprisPlayerAdaptor::volume() const { return m_s->volume(); }
void MprisPlayerAdaptor::setVolume(double v) { emit m_s->volumeRequested(qBound(0.0, v, 1.0)); }
qlonglong MprisPlayerAdaptor::position() const { return m_s->positionUs(); }
bool MprisPlayerAdaptor::canGoNext() const { return m_s->canGoNext(); }
bool MprisPlayerAdaptor::canGoPrevious() const { return m_s->canGoPrevious(); }
bool MprisPlayerAdaptor::canSeek() const { return m_s->length() > 0; }
void MprisPlayerAdaptor::Next() { emit m_s->nextRequested(); }
void MprisPlayerAdaptor::Previous() { emit m_s->previousRequested(); }
void MprisPlayerAdaptor::Pause() { emit m_s->pauseRequested(); }
void MprisPlayerAdaptor::PlayPause() { emit m_s->playPauseRequested(); }
void MprisPlayerAdaptor::Stop() { emit m_s->stopRequested(); }
void MprisPlayerAdaptor::Play() { emit m_s->playRequested(); }
void MprisPlayerAdaptor::Seek(qlonglong offsetUs)
{
    emit m_s->seekRequested(qMax<qint64>(0, m_s->positionUs() / 1000 + offsetUs / 1000));
}
void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &, qlonglong positionUs)
{
    if (positionUs >= 0 && (m_s->length() <= 0 || positionUs / 1000 <= m_s->length()))
        emit m_s->seekRequested(positionUs / 1000);
}
