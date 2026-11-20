#pragma once

#include <QDateTime>
#include <QObject>
#include <QQuickAsyncImageProvider>
#include <QSettings>
#include <QStringList>
#include <QThreadPool>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class QJSEngine;
class QQmlEngine;

// Photo Gallery's file helpers, as the QML singleton `PhotoFiles`: which
// files are pictures, image:// addresses for them, their properties,
// Recycle Bin, and the program's own settings.
class PhotoFiles : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // the file types it reads ("*.jpg", ...) and writes ("jpg", ...)
    Q_PROPERTY(QStringList readPatterns READ readPatterns CONSTANT)
    Q_PROPERTY(QStringList writeSuffixes READ writeSuffixes CONSTANT)

public:
    explicit PhotoFiles(QObject *parent = nullptr);
    static PhotoFiles *create(QQmlEngine *, QJSEngine *);

    static bool isPhoto(const QString &path);
    static QStringList suffixes();
    QStringList readPatterns() const;
    QStringList writeSuffixes() const;

    // the picture itself, full size, loaded in the background
    Q_INVOKABLE QString viewUrl(const QString &path) const;
    // its thumbnail (stamp: the file's time, so a changed file looks new)
    Q_INVOKABLE QString thumbUrl(const QString &path, qint64 stamp = 0) const;

    // Name, Type, Location, Size, Dimensions, Modified, Taken, Camera...
    // as [{ label, value }] in the order to show them
    Q_INVOKABLE QVariantList properties(const QString &path) const;
    Q_INVOKABLE QSize imageSize(const QString &path) const;
    // to the Recycle Bin; "" or what went wrong
    Q_INVOKABLE QString moveToTrash(const QString &path) const;
    Q_INVOKABLE QString picturesFolder() const;
    // "1.2 MB"
    Q_INVOKABLE QString sizeText(qint64 bytes) const;

    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);

    // image:// ids carry the path base64url-encoded (a path's # or ? would
    // otherwise be taken as part of the address)
    static QString encodePath(const QString &path);
    static QString decodePath(const QString &id);
    // the freedesktop.org thumbnail for `path` (shared with other
    // programs, ~/.cache/thumbnails/large), made if missing or stale
    static QImage thumbnail(const QString &path, int size);

private:
    mutable QSettings m_settings;
};

// image://thumb/<stamp>/<path>: thumbnails, several at once in a thread pool
class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    ThumbnailProvider();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    QThreadPool m_pool;
};

// image://view/<path>: the full picture, upright, in the background
class ViewProvider : public QQuickAsyncImageProvider
{
public:
    ViewProvider();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    QThreadPool m_pool;
};
