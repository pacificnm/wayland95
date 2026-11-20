#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class MprisService;

// A MediaPlayer with MediaKeys, as the desktop sees it: an MPRIS player
// (org.mpris.MediaPlayer2.<app>.instance<pid>_<n> on the session bus), the
// way Firefox or VLC are. The keyboard's play, pause, next and previous
// keys and the sound menu by the clock find it there, show what it's
// playing (title, artist, album, cover art) and send it play, pause,
// next...; each comes out as a signal for the MediaPlayer to act on.
//
// W95APP_NO_MPRIS=1 keeps it off the bus (the tests', or a test run's).
class MprisService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged)
    Q_PROPERTY(QString serviceName READ serviceName NOTIFY registeredChanged)
    Q_PROPERTY(QString identity MEMBER m_identity NOTIFY changed)
    Q_PROPERTY(QString desktopEntry MEMBER m_desktopEntry NOTIFY changed)
    Q_PROPERTY(QString playbackStatus READ playbackStatus WRITE setPlaybackStatus NOTIFY changed)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY changed)
    Q_PROPERTY(QString artist READ artist WRITE setArtist NOTIFY changed)
    Q_PROPERTY(QString album READ album WRITE setAlbum NOTIFY changed)
    Q_PROPERTY(QString artUrl READ artUrl WRITE setArtUrl NOTIFY changed)
    Q_PROPERTY(qint64 length READ length WRITE setLength NOTIFY changed)   // ms
    Q_PROPERTY(qint64 position MEMBER m_position)                          // ms (asked for, not announced)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY changed)     // 0 to 1
    Q_PROPERTY(bool canGoNext MEMBER m_canGoNext NOTIFY changed)
    Q_PROPERTY(bool canGoPrevious MEMBER m_canGoPrevious NOTIFY changed)

public:
    explicit MprisService(QObject *parent = nullptr);
    ~MprisService() override;

    bool active() const { return m_active; }
    void setActive(bool on);
    bool registered() const { return !m_service.isEmpty(); }
    QString serviceName() const { return m_service; }
    QString playbackStatus() const { return m_status; }
    void setPlaybackStatus(const QString &s);
    QString title() const { return m_title; }
    void setTitle(const QString &s);
    QString artist() const { return m_artist; }
    void setArtist(const QString &s);
    QString album() const { return m_album; }
    void setAlbum(const QString &s);
    QString artUrl() const { return m_artUrl; }
    void setArtUrl(const QString &s);
    qint64 length() const { return m_length; }
    void setLength(qint64 ms);
    qreal volume() const { return m_volume; }
    void setVolume(qreal v);

    // a picture (a QImage, as Qt's media tags give a cover) as a file in
    // the program's cache: its file:// URL ("" if there's none). The same
    // picture is the same file.
    Q_INVOKABLE static QString imageUrl(const QVariant &image);

    // (the adaptors')
    QVariantMap metadata() const;
    qint64 positionUs() const { return m_position * 1000; }
    bool canGoNext() const { return m_canGoNext; }
    bool canGoPrevious() const { return m_canGoPrevious; }
    Q_INVOKABLE void seeked(qint64 ms);

signals:
    void activeChanged();
    void registeredChanged();
    void changed();
    void playRequested();
    void pauseRequested();
    void playPauseRequested();
    void stopRequested();
    void nextRequested();
    void previousRequested();
    void seekRequested(qint64 positionMs);   // where to go (from the start)
    void volumeRequested(qreal volume);
    void raiseRequested();

private:
    void registerService();
    void unregisterService();
    void announce(const QString &interface, const QVariantMap &changed);
    void metadataChanged();

    bool m_active = false;
    QString m_service;
    QString m_identity, m_desktopEntry;
    QString m_status = QStringLiteral("Stopped");
    QString m_title, m_artist, m_album, m_artUrl;
    qint64 m_length = 0, m_position = 0;
    qreal m_volume = 1.0;
    bool m_canGoNext = true, m_canGoPrevious = true;
    int m_track = 0;
    QObject *m_holder = nullptr;
};

// org.mpris.MediaPlayer2
class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ none)
    Q_PROPERTY(QStringList SupportedMimeTypes READ none)
public:
    MprisRootAdaptor(QObject *holder, MprisService *service);
    bool canQuit() const { return false; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const;
    QString desktopEntry() const;
    QStringList none() const { return {}; }
public slots:
    void Raise();
    void Quit() {}
private:
    MprisService *m_s;
};

// org.mpris.MediaPlayer2.Player
class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(bool Shuffle READ shuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ yes)
    Q_PROPERTY(bool CanPause READ yes)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ yes)
public:
    MprisPlayerAdaptor(QObject *holder, MprisService *service);
    QString playbackStatus() const;
    QString loopStatus() const { return QStringLiteral("None"); }
    double rate() const { return 1.0; }
    bool shuffle() const { return false; }
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canSeek() const;
    bool yes() const { return true; }
public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs);
    void OpenUri(const QString &) {}
signals:
    void Seeked(qlonglong positionUs);
private:
    MprisService *m_s;
};
