#include "screengrab.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QStandardPaths>

ScreenGrab::ScreenGrab(QObject *parent)
    : QObject(parent)
{
}

QImage ScreenGrab::grab(QQuickWindow *window, const QRectF &rect) const
{
    if (!window)
        return {};
    QImage image = window->grabWindow();
    if (image.isNull() || rect.isEmpty())
        return image;
    // the picture is in device pixels
    const qreal dpr = window->effectiveDevicePixelRatio();
    const QRect pixels = QRectF(rect.x() * dpr, rect.y() * dpr, rect.width() * dpr, rect.height() * dpr)
                             .toAlignedRect().intersected(image.rect());
    return image.copy(pixels);
}

QString ScreenGrab::folder(const QString &name) const
{
    QStandardPaths::StandardLocation where = QStandardPaths::PicturesLocation;
    if (name == QLatin1String("Desktop"))
        where = QStandardPaths::DesktopLocation;
    else if (name == QLatin1String("Documents"))
        where = QStandardPaths::DocumentsLocation;
    QString dir = QStandardPaths::writableLocation(where);
    if (dir.isEmpty())
        dir = QDir::homePath();
    QDir().mkpath(dir);
    return dir;
}

QString ScreenGrab::save(QQuickWindow *window, const QRectF &rect, const QString &folder)
{
    const QImage image = grab(window, rect);
    if (image.isNull())
        return QString();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd 'at' hh.mm.ss"));
    QString path = folder + QStringLiteral("/Screenshot ") + stamp + QStringLiteral(".png");
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = folder + QStringLiteral("/Screenshot ") + stamp + QStringLiteral(" (%1).png").arg(n);
    return image.save(path, "PNG") ? path : QString();
}

QString ScreenGrab::copy(QWaylandCompositor *compositor, QQuickWindow *window, const QRectF &rect)
{
    const QImage image = grab(window, rect);
    if (image.isNull() || !compositor)
        return QString();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");

    auto *data = new QMimeData;
    data->setData(QStringLiteral("image/png"), png);
    compositor->overrideSelection(data);
    // (QWaylandCompositor copies what it needs)
    data->deleteLater();

    // for the thumbnail
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wayland95");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/clipboard-screenshot.png");
    image.save(path, "PNG");
    return path;
}
