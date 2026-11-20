#include "photofiles.h"

#include <QAtomicInt>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QLocale>
#include <QMimeDatabase>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <functional>

#include "exif.h"

namespace {

// the biggest picture shown at full size (bigger ones are scaled down to
// it): more than most graphics cards take as one texture
constexpr int MaxViewSide = 8192;
constexpr int ThumbSide = 256; // freedesktop.org "large"

// one image, made by `job` on a pool thread
class Response : public QQuickImageResponse, public QRunnable
{
public:
    explicit Response(std::function<QImage()> job) : m_job(std::move(job)) { setAutoDelete(false); }

    void run() override
    {
        // (finished() is due even when cancelled: the engine waits for it)
        if (!m_cancelled.loadRelaxed())
            m_image = m_job();
        emit finished();
    }
    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }
    QString errorString() const override
    {
        return m_image.isNull() && !m_cancelled.loadRelaxed() ? QStringLiteral("cannot read the picture") : QString();
    }
    void cancel() override { m_cancelled.storeRelaxed(1); }

private:
    std::function<QImage()> m_job;
    QImage m_image;
    QAtomicInt m_cancelled;
};

QString thumbnailDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/thumbnails/large");
}

} // namespace

PhotoFiles::PhotoFiles(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("win95-wayland"), QStringLiteral("win95photos"))
{
}

PhotoFiles *PhotoFiles::create(QQmlEngine *, QJSEngine *)
{
    return new PhotoFiles;
}

QStringList PhotoFiles::suffixes()
{
    static const QStringList list = [] {
        QStringList s;
        for (const QByteArray &f : QImageReader::supportedImageFormats()) {
            const QString name = QString::fromLatin1(f).toLower();
            // not photos: icons, cursors, the X bitmaps, SVG drawings
            static const QStringList skip{QStringLiteral("ico"), QStringLiteral("cur"), QStringLiteral("xbm"),
                                          QStringLiteral("xpm"), QStringLiteral("svg"), QStringLiteral("svgz"),
                                          QStringLiteral("pbm"), QStringLiteral("pgm"), QStringLiteral("icns")};
            if (!skip.contains(name))
                s.append(name);
        }
        if (s.contains(QStringLiteral("jpg")) && !s.contains(QStringLiteral("jpeg")))
            s.append(QStringLiteral("jpeg"));
        if (s.contains(QStringLiteral("tif")) || s.contains(QStringLiteral("tiff")))
            s << QStringLiteral("tif") << QStringLiteral("tiff");
        s.removeDuplicates();
        return s;
    }();
    return list;
}

bool PhotoFiles::isPhoto(const QString &path)
{
    return suffixes().contains(QFileInfo(path).suffix().toLower());
}

QStringList PhotoFiles::readPatterns() const
{
    QStringList p;
    for (const QString &s : suffixes())
        p.append(QStringLiteral("*.") + s);
    return p;
}

QStringList PhotoFiles::writeSuffixes() const
{
    QStringList s;
    for (const QByteArray &f : QImageWriter::supportedImageFormats())
        s.append(QString::fromLatin1(f).toLower());
    return s;
}

QString PhotoFiles::encodePath(const QString &path)
{
    return QString::fromLatin1(path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString PhotoFiles::decodePath(const QString &id)
{
    return QString::fromUtf8(QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding));
}

QString PhotoFiles::viewUrl(const QString &path) const
{
    return path.isEmpty() ? QString() : QStringLiteral("image://view/") + encodePath(path);
}

QString PhotoFiles::thumbUrl(const QString &path, qint64 stamp) const
{
    return QStringLiteral("image://thumb/%1/%2").arg(stamp).arg(encodePath(path));
}

QImage PhotoFiles::thumbnail(const QString &path, int size)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return QImage();
    const QString uri = QString::fromLatin1(QUrl::fromLocalFile(info.absoluteFilePath()).toEncoded());
    const QString mtime = QString::number(info.lastModified().toSecsSinceEpoch());
    const QString dir = thumbnailDir();
    const QString file = dir + QLatin1Char('/')
            + QString::fromLatin1(QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Md5).toHex())
            + QStringLiteral(".png");
    const bool cacheable = !info.absoluteFilePath().startsWith(dir);

    // one already made (by us or another program), if it's for this version
    if (cacheable) {
        QImageReader cached(file, "png");
        const QImage thumb = cached.read();
        if (!thumb.isNull() && thumb.text(QStringLiteral("Thumb::URI")) == uri
                && thumb.text(QStringLiteral("Thumb::MTime")) == mtime)
            return size > 0 && size < ThumbSide ? thumb.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                                : thumb;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    // (JPEG decodes straight at a fraction of its size: fast)
    if (full.isValid() && (full.width() > ThumbSide || full.height() > ThumbSide))
        reader.setScaledSize(full.scaled(ThumbSide, ThumbSide, Qt::KeepAspectRatio));
    QImage thumb = reader.read();
    if (thumb.isNull())
        return thumb;
    if (thumb.width() > ThumbSide || thumb.height() > ThumbSide)
        thumb = thumb.scaled(ThumbSide, ThumbSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (cacheable) {
        thumb.setText(QStringLiteral("Thumb::URI"), uri);
        thumb.setText(QStringLiteral("Thumb::MTime"), mtime);
        thumb.setText(QStringLiteral("Software"), QStringLiteral("Wayland 95 Photo Gallery"));
        if (full.isValid()) {
            thumb.setText(QStringLiteral("Thumb::Image::Width"), QString::number(full.width()));
            thumb.setText(QStringLiteral("Thumb::Image::Height"), QString::number(full.height()));
        }
        QDir().mkpath(dir);
        QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QSaveFile out(file);
        if (out.open(QIODevice::WriteOnly)) {
            out.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            if (thumb.save(&out, "png"))
                out.commit();
            else
                out.cancelWriting();
        }
    }
    if (size > 0 && size < ThumbSide)
        return thumb.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return thumb;
}

QSize PhotoFiles::imageSize(const QString &path) const
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize s = reader.size();
    // (the size as stored: turned on its side, it's the other way round)
    if (s.isValid() && (reader.transformation() & QImageIOHandler::TransformationRotate90))
        s.transpose();
    return s;
}

QString PhotoFiles::sizeText(qint64 bytes) const
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QVariantList PhotoFiles::properties(const QString &path) const
{
    QVariantList list;
    if (path.isEmpty() || !QFileInfo(path).isFile())
        return list;
    auto add = [&list](const QString &label, const QString &value) {
        if (!value.isEmpty())
            list.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}});
    };
    const QFileInfo info(path);
    const QLocale locale;
    add(QStringLiteral("Name:"), info.fileName());
    add(QStringLiteral("Type:"), QMimeDatabase().mimeTypeForFile(info).comment());
    add(QStringLiteral("Location:"), info.absolutePath());
    add(QStringLiteral("Size:"), QStringLiteral("%1 (%2 bytes)").arg(sizeText(info.size()), locale.toString(info.size())));
    const QSize s = imageSize(path);
    if (s.isValid()) {
        const double mp = double(s.width()) * s.height() / 1e6;
        add(QStringLiteral("Dimensions:"), QStringLiteral("%1 x %2 pixels%3").arg(s.width()).arg(s.height())
                .arg(mp >= 0.1 ? QStringLiteral(" (%1 megapixels)").arg(mp, 0, 'f', 1) : QString()));
    }
    add(QStringLiteral("Modified:"), locale.toString(info.lastModified(), QLocale::LongFormat));
    const ExifInfo exif = Exif::read(path);
    if (exif.valid) {
        if (exif.taken.isValid())
            add(QStringLiteral("Taken:"), locale.toString(exif.taken, QLocale::LongFormat));
        add(QStringLiteral("Camera:"), exif.camera());
        add(QStringLiteral("Exposure:"), exif.exposure);
        add(QStringLiteral("Aperture:"), exif.aperture);
        add(QStringLiteral("ISO speed:"), exif.iso);
        add(QStringLiteral("Focal length:"), exif.focalLength);
    }
    return list;
}

QString PhotoFiles::moveToTrash(const QString &path) const
{
    QFile f(path);
    if (!f.exists())
        return QStringLiteral("'%1' isn't there any more.").arg(QFileInfo(path).fileName());
    if (!f.moveToTrash())
        return QStringLiteral("Cannot delete '%1': %2").arg(QFileInfo(path).fileName(), f.errorString());
    return QString();
}

QString PhotoFiles::picturesFolder() const
{
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QFileInfo(pictures).isDir() ? pictures : QDir::homePath();
}

QVariant PhotoFiles::setting(const QString &key, const QVariant &fallback) const
{
    return m_settings.value(key, fallback);
}

void PhotoFiles::setSetting(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
}

// ---- providers --------------------------------------------------------------

ThumbnailProvider::ThumbnailProvider()
{
    m_pool.setMaxThreadCount(qBound(2, QThread::idealThreadCount(), 6));
}

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // "<stamp>/<path>"
    const QString path = PhotoFiles::decodePath(id.section(QLatin1Char('/'), 1));
    const int size = qMax(requestedSize.width(), requestedSize.height());
    auto *r = new Response([path, size] { return PhotoFiles::thumbnail(path, size); });
    m_pool.start(r);
    return r;
}

ViewProvider::ViewProvider()
{
    // (two: the one shown, and the next, read ahead)
    m_pool.setMaxThreadCount(2);
}

QQuickImageResponse *ViewProvider::requestImageResponse(const QString &id, const QSize &)
{
    const QString path = PhotoFiles::decodePath(id);
    auto *r = new Response([path] {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize s = reader.size();
        if (s.isValid() && (s.width() > MaxViewSide || s.height() > MaxViewSide))
            reader.setScaledSize(s.scaled(MaxViewSide, MaxViewSide, Qt::KeepAspectRatio));
        return reader.read();
    });
    m_pool.start(r);
    return r;
}
