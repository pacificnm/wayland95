#include "fontcatalog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <fontconfig/fontconfig.h>

namespace {

QString kindFor(const QString &format, const QString &file)
{
    const QString suffix = QFileInfo(file).suffix().toLower();
    if (format == QLatin1String("CFF") || suffix == QLatin1String("otf"))
        return QStringLiteral("opentype");
    if (format == QLatin1String("TrueType"))
        return QStringLiteral("truetype");
    return QStringLiteral("other");
}

QString formatName(const QString &kind, const QString &format)
{
    if (kind == QLatin1String("opentype")) return QStringLiteral("OpenType");
    if (kind == QLatin1String("truetype")) return QStringLiteral("TrueType");
    if (format == QLatin1String("Type 1")) return QStringLiteral("PostScript Type 1");
    if (format == QLatin1String("PCF") || format == QLatin1String("BDF")) return QStringLiteral("Bitmap");
    return format.isEmpty() ? QStringLiteral("Font") : format;
}

QString string(FcPattern *p, const char *object, int n = 0)
{
    FcChar8 *s = nullptr;
    return FcPatternGetString(p, object, n, &s) == FcResultMatch ? QString::fromUtf8(reinterpret_cast<const char *>(s)) : QString();
}

// the English name when the font has names in several languages
QString english(FcPattern *p, const char *object, const char *langObject)
{
    FcChar8 *lang = nullptr;
    for (int i = 0; FcPatternGetString(p, langObject, i, &lang) == FcResultMatch; ++i)
        if (qstrcmp(reinterpret_cast<const char *>(lang), "en") == 0)
            return string(p, object, i);
    return string(p, object);
}

const QStringList FontSuffixes{ QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("ttc"), QStringLiteral("otc"),
                                QStringLiteral("pfb"), QStringLiteral("pfa"), QStringLiteral("pcf"), QStringLiteral("woff"),
                                QStringLiteral("woff2") };

} // namespace

FontCatalog::FontCatalog(QObject *parent)
    : QObject(parent)
{
    FcInit();
    reload();
}

QString FontCatalog::userFontDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/fonts");
}

void FontCatalog::reload()
{
    // re-read fontconfig: fonts added or removed since we started (its own
    // up-to-date check only looks every 30 seconds)
    FcInitReinitialize();
    FcPattern *pattern = FcPatternCreate();
    FcObjectSet *objects = FcObjectSetBuild(FC_FAMILY, FC_FAMILYLANG, FC_STYLE, FC_STYLELANG, FC_FULLNAME, FC_FULLNAMELANG,
                                            FC_FILE, FC_INDEX, FC_FONTFORMAT, FC_FONTVERSION, nullptr);
    FcFontSet *set = FcFontList(nullptr, pattern, objects);
    const QString home = QDir::homePath();
    const QString mine1 = userFontDir() + QLatin1Char('/'), mine2 = home + QStringLiteral("/.fonts/");

    QVariantList list;
    for (int i = 0; set && i < set->nfont; ++i) {
        FcPattern *p = set->fonts[i];
        const QString file = string(p, FC_FILE);
        if (file.isEmpty())
            continue;
        int index = 0;
        FcPatternGetInteger(p, FC_INDEX, 0, &index);
        int version = 0;
        FcPatternGetInteger(p, FC_FONTVERSION, 0, &version);
        const QString family = english(p, FC_FAMILY, FC_FAMILYLANG);
        const QString style = english(p, FC_STYLE, FC_STYLELANG);
        QString name = english(p, FC_FULLNAME, FC_FULLNAMELANG);
        if (name.isEmpty())
            name = style.isEmpty() || style == QLatin1String("Regular") ? family : family + QLatin1Char(' ') + style;
        const QString format = string(p, FC_FONTFORMAT);
        const QString kind = kindFor(format, file);
        const QFileInfo info(file);
        list << QVariantMap{
            { QStringLiteral("key"), file + QLatin1Char('#') + QString::number(index) + QLatin1Char('#') + style },
            { QStringLiteral("family"), family },
            { QStringLiteral("style"), style },
            { QStringLiteral("name"), name },
            { QStringLiteral("file"), file },
            { QStringLiteral("index"), index },
            { QStringLiteral("kind"), kind },
            { QStringLiteral("format"), formatName(kind, format) },
            { QStringLiteral("size"), info.size() },
            { QStringLiteral("modified"), info.lastModified() },
            { QStringLiteral("version"), version > 0 ? QString::number(version / 65536.0, 'f', 3) : QString() },
            { QStringLiteral("mine"), file.startsWith(mine1) || file.startsWith(mine2) },
        };
    }
    if (set)
        FcFontSetDestroy(set);
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);

    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    m_fonts = list;
    emit fontsChanged();
}

QVariantList FontCatalog::fontsInFolder(const QString &dir) const
{
    QSet<QString> installed;
    for (const QVariant &v : m_fonts)
        installed.insert(v.toMap().value(QStringLiteral("name")).toString());

    QVariantList list;
    const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &f : files) {
        if (!FontSuffixes.contains(f.suffix().toLower()))
            continue;
        const QByteArray path = QFile::encodeName(f.absoluteFilePath());
        int count = 0;
        FcPattern *first = FcFreeTypeQuery(reinterpret_cast<const FcChar8 *>(path.constData()), 0, nullptr, &count);
        if (!first)
            continue;
        // one entry per file; a collection says how many faces it holds
        const QString family = english(first, FC_FAMILY, FC_FAMILYLANG);
        const QString style = english(first, FC_STYLE, FC_STYLELANG);
        QString name = english(first, FC_FULLNAME, FC_FULLNAMELANG);
        if (name.isEmpty())
            name = family + (style.isEmpty() || style == QLatin1String("Regular") ? QString() : QLatin1Char(' ') + style);
        const QString kind = kindFor(string(first, FC_FONTFORMAT), f.absoluteFilePath());
        FcPatternDestroy(first);
        list << QVariantMap{
            { QStringLiteral("name"), count > 1 ? QStringLiteral("%1 (+%2 more)").arg(name).arg(count - 1) : name },
            { QStringLiteral("family"), family },
            { QStringLiteral("style"), style },
            { QStringLiteral("file"), f.absoluteFilePath() },
            { QStringLiteral("kind"), kind },
            { QStringLiteral("installed"), installed.contains(name) },
        };
    }
    return list;
}

QString FontCatalog::install(const QStringList &files)
{
    if (m_busy)
        return QStringLiteral("Fonts are being installed. Please wait.");
    const QString dir = userFontDir();
    if (!QDir().mkpath(dir))
        return QStringLiteral("Can't create the folder %1.").arg(dir);
    int copied = 0;
    QStringList failed;
    for (const QString &file : files) {
        const QFileInfo info(file);
        QString target = dir + QLatin1Char('/') + info.fileName();
        if (QFileInfo(target).canonicalFilePath() == info.canonicalFilePath())
            continue; // already there
        if (QFileInfo::exists(target)) {
            // same name, different font: keep both
            for (int n = 2; QFileInfo::exists(target); ++n)
                target = QStringLiteral("%1/%2 (%3).%4").arg(dir, info.completeBaseName()).arg(n).arg(info.suffix());
        }
        if (QFile::copy(file, target))
            ++copied;
        else
            failed << info.fileName();
    }
    if (!failed.isEmpty())
        return QStringLiteral("These couldn't be copied: %1.").arg(failed.join(QStringLiteral(", ")));
    refreshCache(copied);
    return QString();
}

QString FontCatalog::remove(const QStringList &files)
{
    QStringList refused;
    for (const QString &file : files) {
        bool mine = false;
        for (const QVariant &v : std::as_const(m_fonts))
            if (v.toMap().value(QStringLiteral("file")).toString() == file)
                mine = v.toMap().value(QStringLiteral("mine")).toBool();
        if (!mine || !QFile::moveToTrash(file))
            refused << QFileInfo(file).fileName();
    }
    refreshCache(0);
    if (!refused.isEmpty())
        return QStringLiteral("These couldn't be deleted: %1.").arg(refused.join(QStringLiteral(", ")));
    return QString();
}

void FontCatalog::refreshCache(int installed)
{
    // fontconfig's cache, so every program sees the change
    m_busy = true;
    emit busyChanged();
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p, installed] {
        p->deleteLater();
        m_busy = false;
        emit busyChanged();
        reload();
        emit installFinished(installed);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, installed](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        m_busy = false;
        emit busyChanged();
        reload();
        emit installFinished(installed);
    });
    p->start(QStringLiteral("fc-cache"), { QStringLiteral("-f"), userFontDir() });
}

QString FontCatalog::previewFamily(const QString &file)
{
    if (auto it = m_previewFamilies.constFind(file); it != m_previewFamilies.cend())
        return *it;
    const int id = QFontDatabase::addApplicationFont(file);
    const QString family = id < 0 ? QString() : QFontDatabase::applicationFontFamilies(id).value(0);
    m_previewFamilies.insert(file, family);
    return family;
}
