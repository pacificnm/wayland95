#include "exif.h"

#include <QFile>
#include <QtEndian>

namespace {

// a TIFF block (the EXIF data) in either byte order
struct Tiff
{
    QByteArray data;
    bool little = true;

    bool has(qint64 offset, qint64 size) const { return offset >= 0 && size >= 0 && offset + size <= data.size(); }
    quint16 u16(qint64 o) const
    {
        const auto *p = reinterpret_cast<const uchar *>(data.constData() + o);
        return little ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
    }
    quint32 u32(qint64 o) const
    {
        const auto *p = reinterpret_cast<const uchar *>(data.constData() + o);
        return little ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
    }
    void put16(qint64 o, quint16 v)
    {
        auto *p = reinterpret_cast<uchar *>(data.data() + o);
        if (little)
            qToLittleEndian(v, p);
        else
            qToBigEndian(v, p);
    }
    void put32(qint64 o, quint32 v)
    {
        auto *p = reinterpret_cast<uchar *>(data.data() + o);
        if (little)
            qToLittleEndian(v, p);
        else
            qToBigEndian(v, p);
    }
};

struct Entry
{
    quint16 tag = 0;
    quint16 type = 0;
    quint32 count = 0;
    qint64 at = 0; // where the entry itself is
};

int typeSize(quint16 type)
{
    switch (type) {
    case 1: case 2: case 6: case 7: return 1;  // BYTE ASCII SBYTE UNDEFINED
    case 3: case 8: return 2;                  // SHORT SSHORT
    case 4: case 9: case 11: return 4;         // LONG SLONG FLOAT
    case 5: case 10: case 12: return 8;        // RATIONAL SRATIONAL DOUBLE
    default: return 0;
    }
}

// where an entry's value is (inline in the entry when it fits in 4 bytes)
qint64 valueAt(const Tiff &t, const Entry &e)
{
    const qint64 size = qint64(typeSize(e.type)) * e.count;
    if (size <= 4)
        return e.at + 8;
    return t.u32(e.at + 8);
}

QString asciiValue(const Tiff &t, const Entry &e)
{
    if (e.type != 2)
        return QString();
    const qint64 at = valueAt(t, e);
    if (!t.has(at, e.count))
        return QString();
    QByteArray s = t.data.mid(at, e.count);
    const int nul = s.indexOf('\0');
    if (nul >= 0)
        s.truncate(nul);
    return QString::fromLatin1(s).trimmed();
}

bool rationalValue(const Tiff &t, const Entry &e, double *num, double *den)
{
    if ((e.type != 5 && e.type != 10) || e.count < 1)
        return false;
    const qint64 at = valueAt(t, e);
    if (!t.has(at, 8))
        return false;
    if (e.type == 5) {
        *num = t.u32(at);
        *den = t.u32(at + 4);
    } else {
        *num = qint32(t.u32(at));
        *den = qint32(t.u32(at + 4));
    }
    return *den != 0;
}

long intValue(const Tiff &t, const Entry &e)
{
    const qint64 at = valueAt(t, e);
    if (e.type == 3 && t.has(at, 2))
        return t.u16(at);
    if (e.type == 4 && t.has(at, 4))
        return long(t.u32(at));
    return -1;
}

QList<Entry> entries(const Tiff &t, qint64 ifd, qint64 *nextLinkAt = nullptr)
{
    QList<Entry> list;
    if (!t.has(ifd, 2))
        return list;
    const int n = t.u16(ifd);
    if (n > 1000 || !t.has(ifd + 2, qint64(n) * 12 + 4))
        return list;
    for (int i = 0; i < n; ++i) {
        const qint64 at = ifd + 2 + qint64(i) * 12;
        list.append({t.u16(at), t.u16(at + 2), t.u32(at + 4), at});
    }
    if (nextLinkAt)
        *nextLinkAt = ifd + 2 + qint64(n) * 12;
    return list;
}

QDateTime exifDate(const QString &s)
{
    // "2024:06:01 14:03:22"
    QDateTime d = QDateTime::fromString(s.left(19), QStringLiteral("yyyy:MM:dd HH:mm:ss"));
    return d.isValid() ? d : QDateTime();
}

QString number(double v)
{
    return QString::number(v, 'f', v == qRound(v) ? 0 : 1);
}

} // namespace

QString ExifInfo::camera() const
{
    // "Canon Canon EOS 80D" reads better as "Canon EOS 80D"
    if (model.isEmpty())
        return make;
    if (make.isEmpty() || model.startsWith(make.section(QLatin1Char(' '), 0, 0), Qt::CaseInsensitive))
        return model;
    return make + QLatin1Char(' ') + model;
}

ExifInfo Exif::read(const QString &path)
{
    ExifInfo info;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return info;
    uchar soi[2];
    if (f.read(reinterpret_cast<char *>(soi), 2) != 2 || soi[0] != 0xFF || soi[1] != 0xD8)
        return info;

    // walk the JPEG's markers to its APP1 "Exif" segment (it comes before
    // the picture data: stop at the start of scan)
    QByteArray segment;
    for (int guard = 0; guard < 64; ++guard) {
        uchar hdr[4];
        if (f.read(reinterpret_cast<char *>(hdr), 4) != 4 || hdr[0] != 0xFF)
            return info;
        const int marker = hdr[1];
        const int len = (hdr[2] << 8) | hdr[3];
        if (marker == 0xDA || marker == 0xD9 || len < 2)
            return info;
        if (marker == 0xE1) {
            const QByteArray body = f.read(len - 2);
            if (body.size() == len - 2 && body.startsWith(QByteArray("Exif\0\0", 6))) {
                segment = QByteArray(reinterpret_cast<const char *>(hdr), 4) + body;
                break;
            }
            continue;
        }
        if (!f.seek(f.pos() + len - 2))
            return info;
    }
    if (segment.isEmpty())
        return info;

    const int tiffStart = 4 + 6; // marker+length, "Exif\0\0"
    Tiff t;
    t.data = segment.mid(tiffStart);
    if (t.data.size() < 8)
        return info;
    if (t.data.startsWith("II"))
        t.little = true;
    else if (t.data.startsWith("MM"))
        t.little = false;
    else
        return info;
    if (t.u16(2) != 42)
        return info;

    qint64 ifd1Link = -1;
    const qint64 ifd0 = t.u32(4);
    qint64 exifIfd = -1;
    QString dateTime;
    for (const Entry &e : entries(t, ifd0, &ifd1Link)) {
        switch (e.tag) {
        case 0x010F: info.make = asciiValue(t, e); break;
        case 0x0110: info.model = asciiValue(t, e); break;
        case 0x0132: dateTime = asciiValue(t, e); break;
        case 0x8769: exifIfd = intValue(t, e); break;
        case 0x0112: {
            const long o = intValue(t, e);
            if (o >= 1 && o <= 8)
                info.orientation = int(o);
            // the saved picture is upright already
            if (e.type == 3)
                t.put16(valueAt(t, e), 1);
            break;
        }
        }
    }
    for (const Entry &e : entries(t, exifIfd)) {
        double num = 0, den = 1;
        switch (e.tag) {
        case 0x9003: info.taken = exifDate(asciiValue(t, e)); break;
        case 0x829A:
            if (rationalValue(t, e, &num, &den) && num > 0) {
                const double s = num / den;
                info.exposure = s >= 1 ? number(s) + QStringLiteral(" s")
                                       : QStringLiteral("1/%1 s").arg(qRound(1.0 / s));
            }
            break;
        case 0x829D:
            if (rationalValue(t, e, &num, &den))
                info.aperture = QStringLiteral("f/") + number(num / den);
            break;
        case 0x8827: {
            const long iso = intValue(t, e);
            if (iso > 0)
                info.iso = QString::number(iso);
            break;
        }
        case 0x920A:
            if (rationalValue(t, e, &num, &den))
                info.focalLength = number(num / den) + QStringLiteral(" mm");
            break;
        }
    }
    if (!info.taken.isValid() && !dateTime.isEmpty())
        info.taken = exifDate(dateTime);

    // the embedded thumbnail (IFD1) shows the picture as it was: unlink it
    if (ifd1Link >= 0 && t.has(ifd1Link, 4))
        t.put32(ifd1Link, 0);
    info.segment = segment.left(tiffStart) + t.data;
    info.valid = true;
    return info;
}

QByteArray Exif::insertSegment(const QByteArray &jpeg, const QByteArray &segment)
{
    if (segment.isEmpty() || jpeg.size() < 4 || uchar(jpeg[0]) != 0xFF || uchar(jpeg[1]) != 0xD8)
        return jpeg;
    int at = 2;
    // after the JFIF header (APP0), which has to come first
    if (uchar(jpeg[2]) == 0xFF && uchar(jpeg[3]) == 0xE0 && jpeg.size() >= 6) {
        const int len = (uchar(jpeg[4]) << 8) | uchar(jpeg[5]);
        at = 4 + len;
        if (at > jpeg.size())
            return jpeg;
    }
    QByteArray out = jpeg;
    out.insert(at, segment);
    return out;
}
