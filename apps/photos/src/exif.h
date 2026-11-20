#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

// The few EXIF fields Photo Gallery shows (and sorts by), read straight
// from a JPEG's APP1 segment: no library needed for this much. Anything it
// doesn't understand is simply left empty.
struct ExifInfo
{
    bool valid = false;
    QDateTime taken;      // DateTimeOriginal, else DateTime
    QString make;
    QString model;
    QString exposure;     // "1/250 s"
    QString aperture;     // "f/2.8"
    QString iso;          // "200"
    QString focalLength;  // "35 mm"
    int orientation = 1;  // 1..8, as the tag says

    // the whole APP1 segment (marker and length included), with its
    // orientation set to 1 (normal) and the embedded thumbnail unlinked:
    // what to put back into a JPEG saved from the upright, edited picture
    QByteArray segment;

    QString camera() const;
};

namespace Exif {
// reads the file's EXIF (JPEG only; anything else: invalid)
ExifInfo read(const QString &path);
// `jpeg` with `segment` put back in after its JFIF header
QByteArray insertSegment(const QByteArray &jpeg, const QByteArray &segment);
}
