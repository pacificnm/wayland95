#include "photoeditor.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QTransform>
#include <QtConcurrent/QtConcurrentMap>
#include <cmath>
#include <numeric>

#include "exif.h"

namespace {

// undo keeps whole pictures: at most this many bytes of them (and steps)
constexpr qint64 UndoBytes = 768ll * 1024 * 1024;
constexpr int UndoSteps = 40;
constexpr int PreviewSide = 1600;

inline int clamp255(double v)
{
    return v <= 0 ? 0 : v >= 255 ? 255 : int(v + 0.5);
}

// every row of `img`, through `fn(QRgb *line, int width)`, on all cores
template<typename Fn>
void forRows(QImage &img, Fn fn)
{
    std::vector<int> rows(img.height());
    std::iota(rows.begin(), rows.end(), 0);
    const int w = img.width();
    uchar *bits = img.bits(); // (detaches here, once, not in the threads)
    const qsizetype bpl = img.bytesPerLine();
    QtConcurrent::blockingMap(rows, [&](int y) { fn(reinterpret_cast<QRgb *>(bits + y * bpl), w); });
}

QImage editable(const QImage &src)
{
    return src.convertToFormat(src.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
}

} // namespace

PhotoEditor *PhotoEditor::s_instance = nullptr;

PhotoEditor::PhotoEditor(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
}

PhotoEditor *PhotoEditor::instance()
{
    if (!s_instance)
        s_instance = new PhotoEditor;
    return s_instance;
}

PhotoEditor *PhotoEditor::create(QQmlEngine *, QJSEngine *)
{
    return instance();
}

QString PhotoEditor::imageUrl() const
{
    return m_image.isNull() ? QString() : QStringLiteral("image://photo/%1").arg(m_revision);
}

QImage PhotoEditor::displayImage() const
{
    return m_preview.isNull() ? m_image : m_preview;
}

void PhotoEditor::bump()
{
    ++m_revision;
    emit changed();
}

QString PhotoEditor::load(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull())
        return QStringLiteral("Cannot open '%1': %2").arg(QFileInfo(path).fileName(), reader.errorString());
    m_path = QFileInfo(path).absoluteFilePath();
    m_image = editable(img);
    m_preview = QImage();
    m_previewBase = QImage();
    m_undo.clear();
    m_redo.clear();
    m_savedAt = 0;
    m_savedLost = false;
    m_exif = Exif::read(path).segment;
    emit loadedChanged();
    bump();
    return QString();
}

void PhotoEditor::close()
{
    if (m_image.isNull() && m_path.isEmpty())
        return;
    m_path.clear();
    m_image = QImage();
    m_preview = QImage();
    m_previewBase = QImage();
    m_undo.clear();
    m_redo.clear();
    m_savedAt = 0;
    m_savedLost = false;
    m_exif.clear();
    emit loadedChanged();
    bump();
}

void PhotoEditor::apply(const QImage &result, const QString &what)
{
    if (result.isNull() || m_image.isNull())
        return;
    // the saved version was undone past: a new edit loses it for good
    if (m_savedAt > m_undo.size())
        m_savedLost = true;
    m_undo.append({m_image, what});
    m_redo.clear();
    m_image = editable(result);
    m_preview = QImage();
    m_previewBase = QImage();
    trimUndo();
    bump();
}

void PhotoEditor::trimUndo()
{
    qint64 bytes = 0;
    for (const Step &s : std::as_const(m_undo))
        bytes += s.image.sizeInBytes();
    while (m_undo.size() > 1 && (m_undo.size() > UndoSteps || bytes > UndoBytes)) {
        bytes -= m_undo.first().image.sizeInBytes();
        m_undo.removeFirst();
        if (--m_savedAt < 0)
            m_savedLost = true;
    }
}

void PhotoEditor::undo()
{
    if (m_undo.isEmpty())
        return;
    Step s = m_undo.takeLast();
    m_redo.append({m_image, s.what});
    m_image = s.image;
    m_preview = QImage();
    m_previewBase = QImage();
    bump();
}

void PhotoEditor::redo()
{
    if (m_redo.isEmpty())
        return;
    Step s = m_redo.takeLast();
    m_undo.append({m_image, s.what});
    m_image = s.image;
    m_preview = QImage();
    m_previewBase = QImage();
    bump();
}

void PhotoEditor::revert()
{
    if (m_path.isEmpty() || !modified())
        return;
    QImageReader reader(m_path);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (!img.isNull())
        apply(img, QStringLiteral("Revert"));
    // (the file as it is now: as saved)
    m_savedAt = int(m_undo.size());
    m_savedLost = false;
    emit changed();
}

void PhotoEditor::rotate(int degrees)
{
    if (m_image.isNull())
        return;
    apply(m_image.transformed(QTransform().rotate(degrees)),
          degrees == 180 ? QStringLiteral("Rotate 180°") : degrees < 0 ? QStringLiteral("Rotate Left")
                                                                     : QStringLiteral("Rotate Right"));
}

void PhotoEditor::flip(bool horizontal)
{
    if (m_image.isNull())
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    const QImage result = m_image.flipped(horizontal ? Qt::Horizontal : Qt::Vertical);
#else
    const QImage result = m_image.mirrored(horizontal, !horizontal);
#endif
    apply(result, horizontal ? QStringLiteral("Flip Horizontal") : QStringLiteral("Flip Vertical"));
}

void PhotoEditor::crop(int x, int y, int w, int h)
{
    const QRect r = QRect(x, y, w, h).intersected(m_image.rect());
    if (r.width() < 1 || r.height() < 1 || r == m_image.rect())
        return;
    apply(m_image.copy(r), QStringLiteral("Crop"));
}

void PhotoEditor::resize(int w, int h)
{
    if (m_image.isNull() || w < 1 || h < 1 || w > 30000 || h > 30000 || QSize(w, h) == m_image.size())
        return;
    apply(m_image.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), QStringLiteral("Resize"));
}

QImage PhotoEditor::adjusted(const QImage &src, int brightness, int contrast, int saturation)
{
    QImage img = editable(src);
    // brightness and contrast: one lookup table for all three channels
    const double b = brightness * 1.28;
    const double c = contrast * 1.28;
    const double cf = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));
    uchar lut[256];
    for (int v = 0; v < 256; ++v)
        lut[v] = uchar(clamp255((v - 128) * cf + 128 + b));
    const double sf = 1.0 + saturation / 100.0;
    forRows(img, [&](QRgb *line, int w) {
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            double r = lut[qRed(p)], g = lut[qGreen(p)], bl = lut[qBlue(p)];
            if (saturation != 0) {
                const double l = 0.299 * r + 0.587 * g + 0.114 * bl;
                r = l + (r - l) * sf;
                g = l + (g - l) * sf;
                bl = l + (bl - l) * sf;
            }
            line[x] = qRgba(clamp255(r), clamp255(g), clamp255(bl), qAlpha(p));
        }
    });
    return img;
}

QImage PhotoEditor::grayscaled(const QImage &src)
{
    QImage img = editable(src);
    forRows(img, [](QRgb *line, int w) {
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            const int l = clamp255(0.299 * qRed(p) + 0.587 * qGreen(p) + 0.114 * qBlue(p));
            line[x] = qRgba(l, l, l, qAlpha(p));
        }
    });
    return img;
}

QImage PhotoEditor::sepiaToned(const QImage &src)
{
    QImage img = editable(src);
    forRows(img, [](QRgb *line, int w) {
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            const int r = qRed(p), g = qGreen(p), b = qBlue(p);
            line[x] = qRgba(clamp255(0.393 * r + 0.769 * g + 0.189 * b),
                            clamp255(0.349 * r + 0.686 * g + 0.168 * b),
                            clamp255(0.272 * r + 0.534 * g + 0.131 * b), qAlpha(p));
        }
    });
    return img;
}

QImage PhotoEditor::enhanced(const QImage &src)
{
    // auto levels: each channel stretched so its darkest and lightest
    // 0.5% become black and white (which also takes out a color cast),
    // then a little more color
    QImage img = editable(src);
    quint64 hist[3][256] = {};
    const int step = qMax(1, int(std::sqrt(double(img.width()) * img.height() / 1e6))); // ~1M samples
    quint64 samples = 0;
    for (int y = 0; y < img.height(); y += step) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += step) {
            ++hist[0][qRed(line[x])];
            ++hist[1][qGreen(line[x])];
            ++hist[2][qBlue(line[x])];
            ++samples;
        }
    }
    if (samples == 0)
        return img;
    uchar lut[3][256];
    for (int c = 0; c < 3; ++c) {
        const quint64 cut = samples / 200;
        int lo = 0, hi = 255;
        quint64 sum = 0;
        while (lo < 255 && (sum += hist[c][lo]) <= cut)
            ++lo;
        sum = 0;
        while (hi > 0 && (sum += hist[c][hi]) <= cut)
            --hi;
        // a nearly flat channel (a picture of the sky) is left alone, and
        // no channel is stretched more than 2.5 times
        if (hi - lo < 16) {
            lo = 0;
            hi = 255;
        } else if (hi - lo < 102) {
            const int mid = (lo + hi) / 2;
            lo = qMax(0, mid - 51);
            hi = qMin(255, lo + 102);
        }
        for (int v = 0; v < 256; ++v)
            lut[c][v] = uchar(clamp255((v - lo) * 255.0 / (hi - lo)));
    }
    forRows(img, [&](QRgb *line, int w) {
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            line[x] = qRgba(lut[0][qRed(p)], lut[1][qGreen(p)], lut[2][qBlue(p)], qAlpha(p));
        }
    });
    return adjusted(img, 0, 0, 12);
}

void PhotoEditor::adjust(int brightness, int contrast, int saturation)
{
    if (m_image.isNull())
        return;
    if (brightness == 0 && contrast == 0 && saturation == 0) {
        cancelPreview();
        return;
    }
    apply(adjusted(m_image, brightness, contrast, saturation), QStringLiteral("Light and Color"));
}

void PhotoEditor::preview(int brightness, int contrast, int saturation)
{
    if (m_image.isNull())
        return;
    if (m_previewBase.isNull())
        m_previewBase = (m_image.width() > PreviewSide || m_image.height() > PreviewSide)
                ? m_image.scaled(PreviewSide, PreviewSide, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                : m_image;
    m_preview = adjusted(m_previewBase, brightness, contrast, saturation);
    bump();
}

void PhotoEditor::cancelPreview()
{
    if (m_preview.isNull())
        return;
    m_preview = QImage();
    bump();
}

void PhotoEditor::grayscale()
{
    if (!m_image.isNull())
        apply(grayscaled(m_image), QStringLiteral("Grayscale"));
}

void PhotoEditor::sepia()
{
    if (!m_image.isNull())
        apply(sepiaToned(m_image), QStringLiteral("Sepia"));
}

void PhotoEditor::autoEnhance()
{
    if (!m_image.isNull())
        apply(enhanced(m_image), QStringLiteral("Auto-Enhance"));
}

QString PhotoEditor::save(const QString &path)
{
    if (m_image.isNull())
        return QStringLiteral("There is no picture to save.");
    const QFileInfo info(path);
    const QByteArray format = info.suffix().toLower().toLatin1();
    if (!QImageWriter::supportedImageFormats().contains(format == "jpeg" ? QByteArray("jpg") : format))
        return QStringLiteral("Photo Gallery can't save pictures of type '.%1'. "
                              "Choose JPEG (.jpg), PNG (.png) or BMP (.bmp).").arg(info.suffix());

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    const bool jpeg = format == "jpg" || format == "jpeg";
    if (jpeg)
        writer.setQuality(92);
    if (format == "png")
        writer.setCompression(6);
    // (JPEG has no transparency: what's see-through goes white)
    QImage out = m_image;
    if (jpeg && out.hasAlphaChannel()) {
        QImage flat(out.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter(&flat).drawImage(0, 0, out);
        out = flat;
    }
    if (!writer.write(out))
        return QStringLiteral("Cannot save '%1': %2").arg(info.fileName(), writer.errorString());
    buffer.close();
    if (jpeg)
        data = Exif::insertSegment(data, m_exif);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return QStringLiteral("Cannot save '%1': %2").arg(info.fileName(), file.errorString());

    m_path = info.absoluteFilePath();
    if (!jpeg)
        m_exif.clear();
    m_savedAt = int(m_undo.size());
    m_savedLost = false;
    emit loadedChanged();
    bump();
    return QString();
}

QImage EditorImageProvider::requestImage(const QString &, QSize *size, const QSize &)
{
    const QImage img = PhotoEditor::instance()->displayImage();
    if (size)
        *size = img.size();
    return img;
}
