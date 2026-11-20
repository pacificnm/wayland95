#pragma once

#include <QDBusContext>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QDBusPendingCallWatcher;

// The programs playing sound that say so on the session bus (MPRIS:
// org.mpris.MediaPlayer2.*: Firefox, Chromium, VLC, a Wayland Studio app's
// MediaPlayer with MediaKeys...), for the media keys and the sound menu.
// `current` is the one the keys act on: the one playing (the one that
// started last, if several are), else the one that was playing last.
//
//   MediaPlayers.available  title  artist  album  artUrl  status ("Playing", "Paused", "Stopped")
//   MediaPlayers.playPause()  play()  pause()  stop()  next()  previous()  raise()
//
// Exposed to QML as the singleton `MediaPlayers`.
class MediaPlayers : public QObject, protected QDBusContext
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString current READ current NOTIFY changed)
    Q_PROPERTY(QString identity READ identity NOTIFY changed)
    Q_PROPERTY(QString desktopEntry READ desktopEntry NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString artUrl READ artUrl NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY changed)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY changed)
    Q_PROPERTY(QStringList players READ players NOTIFY changed)

public:
    explicit MediaPlayers(QObject *parent = nullptr);

    bool available() const { return !m_current.isEmpty(); }
    QString current() const { return m_current; }
    QString identity() const { return value(QStringLiteral("Identity")).toString(); }
    QString desktopEntry() const { return value(QStringLiteral("DesktopEntry")).toString(); }
    QString title() const;
    QString artist() const;
    QString album() const;
    QString artUrl() const;
    QString status() const { return value(QStringLiteral("PlaybackStatus")).toString(); }
    bool canGoNext() const { return value(QStringLiteral("CanGoNext")).toBool(); }
    bool canGoPrevious() const { return value(QStringLiteral("CanGoPrevious")).toBool(); }
    QStringList players() const { return m_order; }

    Q_INVOKABLE void playPause() { call(QStringLiteral("PlayPause")); }
    Q_INVOKABLE void play() { call(QStringLiteral("Play")); }
    Q_INVOKABLE void pause() { call(QStringLiteral("Pause")); }
    Q_INVOKABLE void stop() { call(QStringLiteral("Stop")); }
    Q_INVOKABLE void next() { call(QStringLiteral("Next")); }
    Q_INVOKABLE void previous() { call(QStringLiteral("Previous")); }
    Q_INVOKABLE void raise();

signals:
    void changed();

private slots:
    void propertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &invalidated);
    void ownerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    void add(const QString &name);
    void remove(const QString &name);
    void read(const QString &name);
    void choose();
    QVariant value(const QString &key) const;
    QVariantMap metadata() const;
    void call(const QString &method);

    // well-known name -> its properties (the root's and the Player's, together)
    QHash<QString, QVariantMap> m_props;
    QHash<QString, QString> m_owners;     // unique name -> well-known name
    QStringList m_order;                  // the ones that played last, first
    QString m_current;
};
