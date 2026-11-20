#include "archiveengine.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <archive.h>
#include <archive_entry.h>

#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr int BlockSize = 64 * 1024;

// ----------------------------------------------------------------- formats

struct Suffix { const char *suffix; const char *format; };
// longest first: ".tar.gz" before ".gz"
const Suffix suffixes[] = {
    {".tar.gz", "tar.gz"}, {".tgz", "tar.gz"}, {".taz", "tar.gz"},
    {".tar.bz2", "tar.bz2"}, {".tbz2", "tar.bz2"}, {".tbz", "tar.bz2"}, {".tb2", "tar.bz2"},
    {".tar.xz", "tar.xz"}, {".txz", "tar.xz"},
    {".tar.zst", "tar.zst"}, {".tzst", "tar.zst"}, {".tar.zstd", "tar.zst"},
    {".tar.lzma", "tar.lzma"}, {".tlz", "tar.lzma"},
    {".tar", "tar"},
    {".zip", "zip"}, {".jar", "zip"}, {".zipx", "zip"},
    {".7z", "7z"},
    {".rar", "rar"},
    {".iso", "iso"},
    {".cab", "cab"},
    {".cpio", "cpio"},
    {".deb", "ar"}, {".ar", "ar"},
    {".lzh", "lha"}, {".lha", "lha"},
    {".xar", "xar"},
    {".gz", "gz"}, {".bz2", "bz2"}, {".xz", "xz"}, {".zst", "zst"}, {".zstd", "zst"}, {".lzma", "lzma"},
};

QString matchSuffix(const QString &fileName, QString *format)
{
    const QString lower = fileName.toLower();
    for (const Suffix &s : suffixes) {
        const QLatin1String suffix(s.suffix);
        if (lower.endsWith(suffix) && lower.size() > suffix.size()) {
            if (format)
                *format = QLatin1String(s.format);
            return fileName.left(fileName.size() - suffix.size());
        }
    }
    return QString();
}

QString formatName(const QString &format)
{
    static const QHash<QString, QString> names{
        {QStringLiteral("zip"), QStringLiteral("ZIP archive")},
        {QStringLiteral("7z"), QStringLiteral("7-Zip archive")},
        {QStringLiteral("rar"), QStringLiteral("RAR archive")},
        {QStringLiteral("tar"), QStringLiteral("Tar archive")},
        {QStringLiteral("tar.gz"), QStringLiteral("Tar archive (gzip)")},
        {QStringLiteral("tar.bz2"), QStringLiteral("Tar archive (bzip2)")},
        {QStringLiteral("tar.xz"), QStringLiteral("Tar archive (xz)")},
        {QStringLiteral("tar.zst"), QStringLiteral("Tar archive (Zstandard)")},
        {QStringLiteral("tar.lzma"), QStringLiteral("Tar archive (lzma)")},
        {QStringLiteral("gz"), QStringLiteral("Gzip-compressed file")},
        {QStringLiteral("bz2"), QStringLiteral("Bzip2-compressed file")},
        {QStringLiteral("xz"), QStringLiteral("XZ-compressed file")},
        {QStringLiteral("zst"), QStringLiteral("Zstandard-compressed file")},
        {QStringLiteral("lzma"), QStringLiteral("LZMA-compressed file")},
        {QStringLiteral("iso"), QStringLiteral("CD/DVD image (ISO 9660)")},
        {QStringLiteral("cab"), QStringLiteral("Cabinet file")},
        {QStringLiteral("cpio"), QStringLiteral("cpio archive")},
        {QStringLiteral("ar"), QStringLiteral("ar archive")},
        {QStringLiteral("lha"), QStringLiteral("LHA archive")},
        {QStringLiteral("xar"), QStringLiteral("XAR archive")},
    };
    return names.value(format, QStringLiteral("Archive"));
}

QString filterSuffix(int code)
{
    switch (code) {
    case ARCHIVE_FILTER_GZIP: return QStringLiteral("gz");
    case ARCHIVE_FILTER_BZIP2: return QStringLiteral("bz2");
    case ARCHIVE_FILTER_XZ: return QStringLiteral("xz");
    case ARCHIVE_FILTER_ZSTD: return QStringLiteral("zst");
    case ARCHIVE_FILTER_LZMA: return QStringLiteral("lzma");
    default: return QString();
    }
}

// what libarchive found: tar + gzip is "tar.gz", raw + xz is "xz"
QString detectedFormat(struct archive *a, const QString &byName)
{
    const int base = archive_format(a) & ARCHIVE_FORMAT_BASE_MASK;
    const QString filter = archive_filter_count(a) > 1 ? filterSuffix(archive_filter_code(a, 0)) : QString();
    switch (base) {
    case ARCHIVE_FORMAT_TAR:
        return filter.isEmpty() ? QStringLiteral("tar") : QStringLiteral("tar.") + filter;
    case ARCHIVE_FORMAT_RAW:
        return filter.isEmpty() ? byName : filter;
    case ARCHIVE_FORMAT_ZIP: return QStringLiteral("zip");
    case ARCHIVE_FORMAT_7ZIP: return QStringLiteral("7z");
    case ARCHIVE_FORMAT_RAR:
    case ARCHIVE_FORMAT_RAR_V5: return QStringLiteral("rar");
    case ARCHIVE_FORMAT_ISO9660: return QStringLiteral("iso");
    case ARCHIVE_FORMAT_CAB: return QStringLiteral("cab");
    case ARCHIVE_FORMAT_CPIO: return QStringLiteral("cpio");
    case ARCHIVE_FORMAT_AR: return QStringLiteral("ar");
    case ARCHIVE_FORMAT_LHA: return QStringLiteral("lha");
    case ARCHIVE_FORMAT_XAR: return QStringLiteral("xar");
    default: return byName.isEmpty() ? QStringLiteral("other") : byName;
    }
}

QString errorOf(struct archive *a)
{
    const char *e = archive_error_string(a);
    return e ? QString::fromUtf8(e) : QStringLiteral("Unknown error.");
}

bool isPasswordError(const QString &e)
{
    return e.contains(QLatin1String("assphrase"), Qt::CaseInsensitive)
        || e.contains(QLatin1String("password"), Qt::CaseInsensitive)
        || e.contains(QLatin1String("decrypt"), Qt::CaseInsensitive);
}

bool isEncryptionError(const QString &e)
{
    return isPasswordError(e) || e.contains(QLatin1String("ncrypt"), Qt::CaseInsensitive);
}

QString entryPath(struct archive_entry *e)
{
    const char *p = archive_entry_pathname_utf8(e);
    QString s = p ? QString::fromUtf8(p) : QString::fromLocal8Bit(archive_entry_pathname(e));
    while (s.startsWith(QLatin1String("./")))
        s.remove(0, 2);
    while (s.startsWith(QLatin1Char('/')))
        s.remove(0, 1);
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    if (s == QLatin1String("."))
        return QString(); // the top folder itself (ISO images have one)
    return s;
}

// an archive path made safe to put under a folder: no "..", no leading /
// ("" if it can't be)
QString safeRelative(const QString &path)
{
    QStringList parts;
    for (const QString &p : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (p == QLatin1String("."))
            continue;
        if (p == QLatin1String(".."))
            return QString();
        parts << p;
    }
    return parts.join(QLatin1Char('/'));
}

bool selected(const QString &path, const QStringList &only)
{
    if (only.isEmpty())
        return true;
    for (const QString &o : only)
        if (path == o || path.startsWith(o + QLatin1Char('/')))
            return true;
    return false;
}

// the reader: everything libarchive knows; raw for a lone compressed file
struct archive *openReader(const QString &path, const QString &password, bool raw, QString *error)
{
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    if (raw)
        archive_read_support_format_raw(a);
    else
        archive_read_support_format_all(a);
    if (!password.isEmpty())
        archive_read_add_passphrase(a, password.toUtf8().constData());
    if (archive_read_open_filename(a, QFile::encodeName(path).constData(), BlockSize) != ARCHIVE_OK) {
        *error = errorOf(a);
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

bool isRawFormat(const QString &format)
{
    return ArchiveEngine::isSingleFile(format) || format == QLatin1String("lzma");
}

// ------------------------------------------------------- zip packed sizes

quint16 le16(const uchar *p) { return quint16(p[0] | (p[1] << 8)); }
quint32 le32(const uchar *p) { return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24); }
quint64 le64(const uchar *p) { return quint64(le32(p)) | (quint64(le32(p + 4)) << 32); }

// zip's central directory: each file's compressed size (libarchive doesn't say)
QHash<QString, qint64> zipPackedSizes(const QString &path)
{
    QHash<QString, qint64> sizes;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() < 22)
        return sizes;
    const qint64 tailSize = qMin<qint64>(f.size(), 65536 + 22);
    f.seek(f.size() - tailSize);
    const QByteArray tail = f.read(tailSize);
    const uchar *t = reinterpret_cast<const uchar *>(tail.constData());
    qint64 eocd = -1;
    for (qint64 i = tail.size() - 22; i >= 0; --i)
        if (le32(t + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0)
        return sizes;
    quint64 count = le16(t + eocd + 10);
    quint64 cdSize = le32(t + eocd + 12);
    quint64 cdOffset = le32(t + eocd + 16);
    // zip64: the locator just before, pointing at the zip64 end record
    if (eocd >= 20 && le32(t + eocd - 20) == 0x07064b50) {
        f.seek(qint64(le64(t + eocd - 20 + 8)));
        const QByteArray z = f.read(56);
        const uchar *zp = reinterpret_cast<const uchar *>(z.constData());
        if (z.size() == 56 && le32(zp) == 0x06064b50) {
            count = le64(zp + 32);
            cdSize = le64(zp + 40);
            cdOffset = le64(zp + 48);
        }
    }
    if (cdSize > 256u * 1024 * 1024 || !f.seek(qint64(cdOffset)))
        return sizes;
    const QByteArray cd = f.read(qint64(cdSize));
    const uchar *c = reinterpret_cast<const uchar *>(cd.constData());
    qint64 pos = 0;
    for (quint64 n = 0; n < count && pos + 46 <= cd.size(); ++n) {
        if (le32(c + pos) != 0x02014b50)
            break;
        const quint16 flags = le16(c + pos + 8);
        quint64 csize = le32(c + pos + 20);
        const quint32 usize = le32(c + pos + 24);
        const int nameLen = le16(c + pos + 28), extraLen = le16(c + pos + 30), commentLen = le16(c + pos + 32);
        if (pos + 46 + nameLen + extraLen > cd.size())
            break;
        const QByteArray rawName = cd.mid(pos + 46, nameLen);
        if (csize == 0xffffffffu) {
            // zip64 extra field: original size first (if it overflowed), then compressed
            const uchar *x = c + pos + 46 + nameLen;
            for (int i = 0; i + 4 <= extraLen;) {
                const int id = le16(x + i), len = le16(x + i + 2);
                if (id == 1) {
                    int off = i + 4;
                    if (usize == 0xffffffffu)
                        off += 8;
                    if (off + 8 <= i + 4 + len)
                        csize = le64(x + off);
                    break;
                }
                i += 4 + len;
            }
        }
        QString name = (flags & 0x800) ? QString::fromUtf8(rawName) : QString::fromLocal8Bit(rawName);
        while (name.endsWith(QLatin1Char('/')))
            name.chop(1);
        sizes.insert(name, qint64(csize));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return sizes;
}

// ---------------------------------------------------------------- programs

struct ToolRun
{
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
    bool cancelled = false;
    bool started = false;
};

// runs a program to the end (or until cancelled), feeding its output to
// `onOutput` as it comes
ToolRun runTool(const QString &program, const QStringList &args, JobContext *ctx,
                const std::function<void(const QByteArray &)> &onOutput = {},
                const QString &workDir = QString())
{
    ToolRun r;
    QProcess p;
    if (!workDir.isEmpty())
        p.setWorkingDirectory(workDir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    p.setProcessEnvironment(env);
    p.start(program, args);
    if (!p.waitForStarted(5000))
        return r;
    r.started = true;
    p.closeWriteChannel(); // a password prompt gets nothing, rather than hanging
    while (p.state() != QProcess::NotRunning) {
        p.waitForReadyRead(200);
        const QByteArray chunk = p.readAllStandardOutput();
        r.out += chunk;
        r.err += p.readAllStandardError();
        if (onOutput && !chunk.isEmpty())
            onOutput(chunk);
        if (ctx && ctx->cancel) {
            p.kill();
            p.waitForFinished(2000);
            r.cancelled = true;
            return r;
        }
    }
    r.out += p.readAllStandardOutput();
    r.err += p.readAllStandardError();
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return r;
}

QString passwordArg(const QString &password)
{
    // always given: without one, 7z would sit waiting for one to be typed
    return QStringLiteral("-p") + password;
}

QString toolError(const ToolRun &r, const QString &program)
{
    if (!r.started)
        return QStringLiteral("Cannot run %1.").arg(program);
    QString text = QString::fromUtf8(r.err).trimmed();
    if (text.isEmpty())
        text = QString::fromUtf8(r.out).trimmed().split(QLatin1Char('\n')).mid(-4).join(QLatin1Char('\n'));
    return text.isEmpty() ? QStringLiteral("%1 failed (exit code %2).").arg(program).arg(r.exitCode) : text;
}

bool toolPasswordProblem(const ToolRun &r)
{
    const QString all = QString::fromUtf8(r.err + r.out);
    return all.contains(QLatin1String("Wrong password"), Qt::CaseInsensitive)
        || all.contains(QLatin1String("password"), Qt::CaseInsensitive);
}

ListResult listWith7z(const QString &archive, const QString &password)
{
    ListResult res;
    res.tool = QStringLiteral("7z");
    const QString prog = ArchiveEngine::sevenZip();
    const ToolRun r = runTool(prog, {QStringLiteral("l"), QStringLiteral("-slt"), QStringLiteral("-ba"), QStringLiteral("-sccUTF-8"),
                                     passwordArg(password), QStringLiteral("--"), archive}, nullptr);
    if (r.exitCode != 0) {
        res.needPassword = toolPasswordProblem(r);
        res.error = toolError(r, QStringLiteral("7-Zip"));
        return res;
    }
    ArchiveItem item;
    bool have = false;
    auto flush = [&]() {
        if (have && !item.path.isEmpty())
            res.items << item;
        item = ArchiveItem();
        have = false;
    };
    for (const QString &line : QString::fromUtf8(r.out).split(QLatin1Char('\n'))) {
        const int eq = line.indexOf(QLatin1String(" = "));
        if (eq < 0)
            continue;
        const QString key = line.left(eq), value = line.mid(eq + 3).trimmed();
        if (key == QLatin1String("Path")) {
            flush();
            item.path = value;
            have = true;
        } else if (key == QLatin1String("Size")) {
            item.size = value.toLongLong();
        } else if (key == QLatin1String("Packed Size")) {
            item.packed = value.isEmpty() ? -1 : value.toLongLong();
        } else if (key == QLatin1String("Modified")) {
            item.modified = QDateTime::fromString(value.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        } else if (key == QLatin1String("Folder")) {
            item.isDir = value == QLatin1String("+");
        } else if (key == QLatin1String("Attributes")) {
            if (value.startsWith(QLatin1Char('D')))
                item.isDir = true;
        } else if (key == QLatin1String("Encrypted")) {
            item.encrypted = value == QLatin1String("+");
            if (item.encrypted)
                res.encrypted = true;
        }
    }
    flush();
    // a solid archive gives the block's packed size with its first file only
    bool solid = false;
    for (const ArchiveItem &i : res.items)
        solid = solid || (!i.isDir && i.packed < 0);
    if (solid)
        for (ArchiveItem &i : res.items)
            i.packed = -1;
    res.ok = true;
    return res;
}

ListResult listWithUnar(const QString &archive, const QString &password)
{
    ListResult res;
    res.tool = QStringLiteral("unar");
    QStringList args{QStringLiteral("-j")};
    if (!password.isEmpty())
        args << QStringLiteral("-p") << password;
    args << QStringLiteral("--") << archive;
    const ToolRun r = runTool(ArchiveEngine::lsar(), args, nullptr);
    const QJsonObject root = QJsonDocument::fromJson(r.out).object();
    if (r.exitCode != 0 && root.isEmpty()) {
        res.needPassword = toolPasswordProblem(r);
        res.error = toolError(r, QStringLiteral("lsar"));
        return res;
    }
    for (const QJsonValue &v : root.value(QLatin1String("lsarContents")).toArray()) {
        const QJsonObject o = v.toObject();
        ArchiveItem item;
        item.path = o.value(QLatin1String("XADFileName")).toString();
        while (item.path.endsWith(QLatin1Char('/')))
            item.path.chop(1);
        item.isDir = o.value(QLatin1String("XADIsDirectory")).toBool();
        item.size = qint64(o.value(QLatin1String("XADFileSize")).toDouble());
        item.packed = o.contains(QLatin1String("XADCompressedSize")) ? qint64(o.value(QLatin1String("XADCompressedSize")).toDouble()) : -1;
        item.modified = QDateTime::fromString(o.value(QLatin1String("XADLastModificationDate")).toString().left(19),
                                              QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        item.encrypted = o.value(QLatin1String("XADIsEncrypted")).toBool();
        if (item.encrypted)
            res.encrypted = true;
        if (!item.path.isEmpty())
            res.items << item;
    }
    res.ok = true;
    return res;
}

// -------------------------------------------------------- writing to disk

// makes dest/rel's folders, refusing to go through a symbolic link (an
// archive's link pointing out of dest, then a file "through" it)
bool makeParents(const QString &dest, const QString &rel, QString *error)
{
    const QStringList parts = rel.split(QLatin1Char('/'));
    QString at = dest;
    for (int i = 0; i + 1 < parts.size(); ++i) {
        at += QLatin1Char('/') + parts.at(i);
        const QFileInfo fi(at);
        if (fi.isSymLink()) {
            *error = QStringLiteral("%1: goes through a link (skipped)").arg(rel);
            return false;
        }
        if (fi.exists() && !fi.isDir()) {
            *error = QStringLiteral("%1: %2 is a file, not a folder").arg(rel, parts.mid(0, i + 1).join(QLatin1Char('/')));
            return false;
        }
        if (!fi.exists() && !QDir().mkdir(at)) {
            *error = QStringLiteral("Cannot create the folder %1").arg(at);
            return false;
        }
    }
    return true;
}

int fileMode(struct archive_entry *e, bool dir)
{
    int perm = archive_entry_perm(e) & 0777;
    if (perm == 0)
        perm = dir ? 0755 : 0644;
    // never setuid/world-writable from an archive; always ours to read and write
    return (perm & 0755) | (dir ? 0700 : 0600);
}

void addWritten(JobResult &res, const QString &dest, const QString &rel)
{
    const QString top = dest + QLatin1Char('/') + rel.section(QLatin1Char('/'), 0, 0);
    if (!res.written.contains(top))
        res.written << top;
}

// the Overwrite question, with Yes to All / No to All remembered
struct OverwritePolicy
{
    int all = -1; // OverwriteYesAll / OverwriteNoAll once answered so
    // true: write over it; false: skip it (cancel set if Cancel)
    bool decide(const QString &path, JobContext &ctx)
    {
        if (all == OverwriteYesAll)
            return true;
        if (all == OverwriteNoAll)
            return false;
        const int a = ctx.askOverwrite ? ctx.askOverwrite(path) : OverwriteYes;
        if (a == OverwriteCancel) {
            ctx.cancel = true;
            return false;
        }
        if (a == OverwriteYesAll || a == OverwriteNoAll)
            all = a;
        return a == OverwriteYes || a == OverwriteYesAll;
    }
};

// moves what a program extracted (into staging) to dest, asking before
// writing over anything
void moveExtracted(const QString &staging, const QString &dest, const QStringList &only, bool useFolders,
                   JobContext &ctx, JobResult &res)
{
    OverwritePolicy policy;
    QDirIterator it(staging, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    QStringList files;
    while (it.hasNext()) {
        it.next();
        if (!it.fileInfo().isDir() || it.fileInfo().isSymLink())
            files << QDir(staging).relativeFilePath(it.filePath());
        else if (useFolders && QDir(it.filePath()).isEmpty())
            files << QDir(staging).relativeFilePath(it.filePath()) + QLatin1Char('/');
    }
    files.sort();
    for (const QString &f : files) {
        if (ctx.cancel)
            return;
        const bool emptyDir = f.endsWith(QLatin1Char('/'));
        const QString inArchive = emptyDir ? f.chopped(1) : f;
        if (!selected(inArchive, only))
            continue;
        const QString rel = useFolders ? inArchive : inArchive.section(QLatin1Char('/'), -1);
        const QString target = dest + QLatin1Char('/') + rel;
        QString err;
        if (!makeParents(dest, rel, &err)) {
            res.problems << err;
            continue;
        }
        if (emptyDir) {
            QDir().mkpath(target);
            addWritten(res, dest, rel);
            continue;
        }
        const QFileInfo existing(target);
        if (existing.exists() || existing.isSymLink()) {
            if (existing.isDir() && !existing.isSymLink()) {
                res.problems << QStringLiteral("%1: a folder of that name is in the way").arg(rel);
                continue;
            }
            if (!policy.decide(target, ctx)) {
                if (ctx.cancel)
                    return;
                res.problems << QStringLiteral("%1: not replaced").arg(rel);
                continue;
            }
            QFile::remove(target);
        }
        if (!QFile::rename(staging + QLatin1Char('/') + f, target)) {
            res.problems << QStringLiteral("%1: cannot write it").arg(rel);
            continue;
        }
        ++res.files;
        addWritten(res, dest, rel);
    }
}

// 7z / unar extract everything into a staging folder in dest, then it's moved
JobResult extractWithTool(const QString &archive, const QString &tool, const QStringList &only,
                          const QString &dest, bool useFolders, JobContext &ctx)
{
    JobResult res;
    QTemporaryDir staging(dest + QStringLiteral("/.archive-manager-XXXXXX"));
    if (!staging.isValid()) {
        res.error = QStringLiteral("Cannot write in %1.").arg(dest);
        return res;
    }
    static const QRegularExpression percent(QStringLiteral("(\\d+)%"));
    auto onOutput = [&](const QByteArray &chunk) {
        QRegularExpressionMatchIterator m = percent.globalMatch(QString::fromUtf8(chunk));
        int last = -1;
        while (m.hasNext())
            last = m.next().captured(1).toInt();
        if (last >= 0 && ctx.progress)
            ctx.progress(last, 100, QFileInfo(archive).fileName());
    };
    ToolRun r;
    QString name;
    if (tool == QLatin1String("7z")) {
        name = QStringLiteral("7-Zip");
        QStringList args{QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-bsp1"), QStringLiteral("-bb0"),
                         QStringLiteral("-sccUTF-8"), passwordArg(ctx.password), QStringLiteral("-o") + staging.path(),
                         QStringLiteral("--"), archive};
        args << only;
        r = runTool(ArchiveEngine::sevenZip(), args, &ctx, onOutput);
    } else {
        name = QStringLiteral("unar");
        QStringList args{QStringLiteral("-q"), QStringLiteral("-D"), QStringLiteral("-f"), QStringLiteral("-o"), staging.path()};
        if (!ctx.password.isEmpty())
            args << QStringLiteral("-p") << ctx.password;
        args << QStringLiteral("--") << archive;
        r = runTool(ArchiveEngine::unar(), args, &ctx);
    }
    if (r.cancelled || ctx.cancel) {
        res.cancelled = true;
        return res;
    }
    if (r.exitCode != 0) {
        res.needPassword = toolPasswordProblem(r);
        res.error = toolError(r, name);
        return res;
    }
    moveExtracted(staging.path(), dest, only, useFolders, ctx, res);
    res.cancelled = ctx.cancel;
    res.ok = !res.cancelled;
    return res;
}

// --------------------------------------------------------------- writing

struct NewFile
{
    QString source;
    QString arcPath;
    bool isDir = false;
    bool isLink = false;
};

QVector<NewFile> gather(const QStringList &files, const QString &inside, qint64 *bytes)
{
    QVector<NewFile> out;
    QString prefix = safeRelative(inside);
    if (!prefix.isEmpty())
        prefix += QLatin1Char('/');
    for (const QString &f : files) {
        const QFileInfo fi(f);
        if (!fi.exists() && !fi.isSymLink())
            continue;
        const QString base = fi.absolutePath();
        out << NewFile{fi.absoluteFilePath(), prefix + fi.fileName(), fi.isDir() && !fi.isSymLink(), fi.isSymLink()};
        if (fi.isFile() && !fi.isSymLink())
            *bytes += fi.size();
        if (fi.isDir() && !fi.isSymLink()) {
            QDirIterator it(fi.absoluteFilePath(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo c = it.fileInfo();
                out << NewFile{c.absoluteFilePath(), prefix + QDir(base).relativeFilePath(c.absoluteFilePath()),
                               c.isDir() && !c.isSymLink(), c.isSymLink()};
                if (c.isFile() && !c.isSymLink())
                    *bytes += c.size();
            }
        }
    }
    return out;
}

bool setupWriter(struct archive *w, const WriteOptions &o, QString *error)
{
    const QString &f = o.format;
    const int level = qBound(0, o.level, 9);
    QStringList opts;
    int r = ARCHIVE_OK;
    if (f == QLatin1String("zip")) {
        r = archive_write_set_format_zip(w);
        if (level == 0)
            opts << QStringLiteral("zip:compression=store");
        else
            opts << QStringLiteral("zip:compression=deflate") << QStringLiteral("zip:compression-level=%1").arg(level);
        if (!o.password.isEmpty()) {
            archive_write_set_passphrase(w, o.password.toUtf8().constData());
            opts << QStringLiteral("zip:encryption=aes256");
        }
    } else if (f == QLatin1String("7z")) {
        r = archive_write_set_format_7zip(w);
        opts << (level == 0 ? QStringLiteral("7zip:compression=store") : QStringLiteral("7zip:compression=lzma2"));
        if (level > 0)
            opts << QStringLiteral("7zip:compression-level=%1").arg(level);
    } else if (f.startsWith(QLatin1String("tar")) || ArchiveEngine::isSingleFile(f)) {
        const bool single = ArchiveEngine::isSingleFile(f);
        r = single ? archive_write_set_format_raw(w) : archive_write_set_format_pax_restricted(w);
        const QString filter = single ? f : f.mid(4);
        int lv = level;
        if (filter == QLatin1String("gz")) {
            archive_write_add_filter_gzip(w);
        } else if (filter == QLatin1String("bz2")) {
            archive_write_add_filter_bzip2(w);
            lv = qMax(1, lv);
        } else if (filter == QLatin1String("xz")) {
            archive_write_add_filter_xz(w);
        } else if (filter == QLatin1String("zst")) {
            archive_write_add_filter_zstd(w);
            lv = lv >= 9 ? 19 : qMax(1, lv);
        } else if (!filter.isEmpty()) {
            *error = QStringLiteral("Archive Manager can't write %1 files.").arg(f);
            return false;
        }
        if (!filter.isEmpty())
            opts << QStringLiteral("compression-level=%1").arg(lv);
    } else {
        *error = QStringLiteral("Archive Manager can't write %1. Make a .zip, .7z or .tar.gz instead.").arg(formatName(f));
        return false;
    }
    if (r != ARCHIVE_OK) {
        *error = errorOf(w);
        return false;
    }
    for (const QString &opt : opts)
        if (archive_write_set_options(w, opt.toUtf8().constData()) < ARCHIVE_WARN) {
            *error = errorOf(w);
            return false;
        }
    return true;
}

bool copyData(struct archive *in, struct archive *out, JobContext &ctx, qint64 &done, qint64 total,
              const QString &name, QString *error)
{
    const void *buf;
    size_t size;
    la_int64_t offset;
    for (;;) {
        if (ctx.cancel)
            return false;
        const int r = archive_read_data_block(in, &buf, &size, &offset);
        if (r == ARCHIVE_EOF)
            return true;
        if (r < ARCHIVE_WARN) {
            *error = errorOf(in);
            return false;
        }
        if (size > 0 && archive_write_data(out, buf, size) < 0) {
            *error = errorOf(out);
            return false;
        }
        done += qint64(size);
        if (ctx.progress)
            ctx.progress(done, total, name);
    }
}

// the rewrite behind add and remove: the old archive's entries (less those
// `drop` matches) then `files`, into a new copy that replaces it at the end
JobResult rewrite(const QString &archive, const QVector<NewFile> &files, qint64 newBytes,
                  const QStringList &drop, const WriteOptions &options, JobContext &ctx)
{
    JobResult res;
    const bool exists = QFileInfo::exists(archive);
    if (ArchiveEngine::isSingleFile(options.format)) {
        int regular = 0;
        for (const NewFile &f : files)
            regular += (!f.isDir && !f.isLink) ? 1 : 0;
        if (exists || regular != 1 || files.size() != 1) {
            res.error = QStringLiteral("A %1 holds just one file. To put several files or a folder together, "
                                       "make a .tar.%2 or a .zip instead.").arg(formatName(options.format), options.format);
            return res;
        }
    }

    QSaveFile out(archive);
    if (!out.open(QIODevice::WriteOnly)) {
        res.error = QStringLiteral("Cannot write %1: %2").arg(archive, out.errorString());
        return res;
    }
    struct archive *w = archive_write_new();
    QString error;
    if (!setupWriter(w, options, &error) || archive_write_open_fd(w, out.handle()) != ARCHIVE_OK) {
        if (error.isEmpty())
            error = errorOf(w);
        archive_write_free(w);
        res.error = error;
        return res;
    }

    QSet<QString> replaced;
    for (const NewFile &f : files)
        replaced.insert(f.arcPath);
    qint64 total = ctx.progress ? newBytes : 0;
    qint64 done = 0;
    bool failed = false;

    // what's there already
    if (exists) {
        struct archive *in = openReader(archive, ctx.password, false, &error);
        if (!in) {
            failed = true;
        } else {
            total += QFileInfo(archive).size(); // roughly: uncompressed is more
            struct archive_entry *e;
            for (;;) {
                if (ctx.cancel)
                    break;
                const int r = archive_read_next_header(in, &e);
                if (r == ARCHIVE_EOF)
                    break;
                if (r < ARCHIVE_WARN) {
                    error = errorOf(in);
                    failed = true;
                    break;
                }
                const QString path = entryPath(e);
                if (replaced.contains(path) || (!drop.isEmpty() && selected(path, drop))) {
                    archive_read_data_skip(in);
                    continue;
                }
                if (archive_write_header(w, e) < ARCHIVE_WARN) {
                    error = QStringLiteral("%1: %2").arg(path, errorOf(w));
                    failed = true;
                    break;
                }
                if (!copyData(in, w, ctx, done, total, path, &error)) {
                    if (!ctx.cancel) {
                        res.needPassword = isPasswordError(error);
                        failed = true;
                    }
                    break;
                }
                archive_write_finish_entry(w);
                ++res.files;
            }
            archive_read_free(in);
        }
    }

    // the new ones
    QByteArray buf(BlockSize, Qt::Uninitialized);
    for (const NewFile &f : files) {
        if (failed || ctx.cancel)
            break;
        struct stat st;
        if (::lstat(QFile::encodeName(f.source).constData(), &st) != 0) {
            res.problems << QStringLiteral("%1: cannot read it").arg(f.source);
            continue;
        }
        QFile src(f.source);
        if (!f.isDir && !f.isLink && !src.open(QIODevice::ReadOnly)) {
            res.problems << QStringLiteral("%1: %2").arg(f.source, src.errorString());
            continue;
        }
        struct archive_entry *e = archive_entry_new();
        archive_entry_set_pathname_utf8(e, f.arcPath.toUtf8().constData());
        archive_entry_copy_stat(e, &st);
        archive_entry_set_uid(e, 0);
        archive_entry_set_gid(e, 0);
        archive_entry_set_uname(e, nullptr);
        archive_entry_set_gname(e, nullptr);
        if (f.isLink) {
            archive_entry_set_size(e, 0);
            const ssize_t n = ::readlink(QFile::encodeName(f.source).constData(), buf.data(), size_t(buf.size() - 1));
            const QByteArray target = n >= 0 ? QByteArray(buf.constData(), int(n)) : QByteArray();
            archive_entry_set_symlink(e, target.constData());
        } else if (f.isDir) {
            archive_entry_set_size(e, 0);
        } else if (!S_ISREG(st.st_mode)) {
            archive_entry_free(e);
            continue; // devices, sockets, fifos
        }
        if (archive_write_header(w, e) < ARCHIVE_WARN) {
            error = QStringLiteral("%1: %2").arg(f.arcPath, errorOf(w));
            archive_entry_free(e);
            failed = true;
            break;
        }
        archive_entry_free(e);
        if (!f.isDir && !f.isLink) {
            for (;;) {
                if (ctx.cancel)
                    break;
                const qint64 n = src.read(buf.data(), buf.size());
                if (n < 0) {
                    error = QStringLiteral("%1: %2").arg(f.source, src.errorString());
                    failed = true;
                    break;
                }
                if (n == 0)
                    break;
                if (archive_write_data(w, buf.constData(), size_t(n)) < 0) {
                    error = errorOf(w);
                    failed = true;
                    break;
                }
                done += n;
                if (ctx.progress)
                    ctx.progress(done, total, f.arcPath);
            }
            ++res.files;
            res.bytes += st.st_size;
        }
        archive_write_finish_entry(w);
    }

    if (!failed && !ctx.cancel && archive_write_close(w) != ARCHIVE_OK) {
        error = errorOf(w);
        failed = true;
    }
    archive_write_free(w);
    if (ctx.cancel) {
        out.cancelWriting();
        res.cancelled = true;
        return res;
    }
    if (failed) {
        out.cancelWriting();
        res.error = error;
        return res;
    }
    if (!out.commit()) {
        res.error = QStringLiteral("Cannot write %1: %2").arg(archive, out.errorString());
        return res;
    }
    res.ok = true;
    return res;
}

} // namespace

// =================================================================== public

QString ArchiveEngine::formatForName(const QString &fileName)
{
    QString format;
    matchSuffix(fileName, &format);
    return format;
}

QString ArchiveEngine::formatLabel(const QString &format)
{
    return formatName(format);
}

QString ArchiveEngine::baseName(const QString &fileName)
{
    const QString base = matchSuffix(fileName, nullptr);
    return base.isEmpty() ? QFileInfo(fileName).completeBaseName() : base;
}

bool ArchiveEngine::canWrite(const QString &format)
{
    static const QStringList writable{
        QStringLiteral("zip"), QStringLiteral("7z"), QStringLiteral("tar"), QStringLiteral("tar.gz"),
        QStringLiteral("tar.bz2"), QStringLiteral("tar.xz"), QStringLiteral("tar.zst"),
    };
    return writable.contains(format);
}

bool ArchiveEngine::isSingleFile(const QString &format)
{
    return format == QLatin1String("gz") || format == QLatin1String("bz2") || format == QLatin1String("xz")
        || format == QLatin1String("zst");
}

QString ArchiveEngine::toolNeeded(const QString &what)
{
    if (what == QLatin1String("7z"))
        return sevenZip().isEmpty() ? QStringLiteral("7-Zip (install it with: sudo apt install 7zip)") : QString();
    if (what == QLatin1String("unar"))
        return unar().isEmpty() || lsar().isEmpty()
            ? QStringLiteral("unar (install it with: sudo apt install unar)") : QString();
    return QString();
}

QString ArchiveEngine::sevenZip()
{
    for (const char *name : {"7z", "7zz", "7za"}) {
        const QString p = QStandardPaths::findExecutable(QLatin1String(name));
        if (!p.isEmpty())
            return p;
    }
    return QString();
}

QString ArchiveEngine::unar() { return QStandardPaths::findExecutable(QStringLiteral("unar")); }
QString ArchiveEngine::lsar() { return QStandardPaths::findExecutable(QStringLiteral("lsar")); }

ListResult ArchiveEngine::list(const QString &archive, const QString &password)
{
    ListResult res;
    const QFileInfo info(archive);
    const QString byName = formatForName(info.fileName());
    if (!info.isFile()) {
        res.error = QStringLiteral("Cannot find %1.").arg(archive);
        return res;
    }

    bool raw = isRawFormat(byName);
    QString error;
    for (int attempt = 0; attempt < 2; ++attempt) {
        struct archive *a = openReader(archive, password, raw, &error);
        if (!a)
            break;
        res.items.clear();
        bool failed = false;
        struct archive_entry *e;
        for (;;) {
            const int r = archive_read_next_header(a, &e);
            if (r == ARCHIVE_EOF)
                break;
            if (r < ARCHIVE_WARN) {
                error = errorOf(a);
                failed = true;
                break;
            }
            ArchiveItem item;
            item.path = raw ? baseName(info.fileName()) : entryPath(e);
            item.isDir = archive_entry_filetype(e) == AE_IFDIR;
            item.size = archive_entry_size_is_set(e) ? archive_entry_size(e) : -1;
            if (archive_entry_mtime_is_set(e))
                item.modified = QDateTime::fromSecsSinceEpoch(archive_entry_mtime(e));
            item.encrypted = archive_entry_is_encrypted(e);
            if (archive_entry_filetype(e) == AE_IFLNK && archive_entry_symlink_utf8(e))
                item.linkTarget = QString::fromUtf8(archive_entry_symlink_utf8(e));
            if (item.encrypted)
                res.encrypted = true;
            if (raw) {
                // a lone compressed file says nothing of its size: count it
                // (up to a point - a big one would take a while)
                item.packed = info.size();
                item.modified = info.lastModified();
                if (item.size < 0 && info.size() < 64 * 1024 * 1024) {
                    qint64 n = 0;
                    const void *buf;
                    size_t size;
                    la_int64_t offset;
                    int rr;
                    while ((rr = archive_read_data_block(a, &buf, &size, &offset)) == ARCHIVE_OK)
                        n = offset + qint64(size);
                    if (rr == ARCHIVE_EOF)
                        item.size = n;
                }
            }
            if (!item.path.isEmpty())
                res.items << item;
            if (!raw)
                archive_read_data_skip(a);
        }
        res.format = detectedFormat(a, byName);
        archive_read_free(a);
        if (!failed) {
            res.ok = true;
            break;
        }
        // a lone .gz (not a tar inside): read it as one file
        if (!raw && res.items.isEmpty() && error.contains(QLatin1String("Unrecognized archive format"))) {
            raw = true;
            continue;
        }
        break;
    }

    if (res.format.isEmpty() || res.format == QLatin1String("other"))
        res.format = byName.isEmpty() ? QStringLiteral("other") : byName;

    // what libarchive can't do: 7z with a password, some RAR archives
    const bool sevenZ = res.format == QLatin1String("7z") || byName == QLatin1String("7z");
    const bool rar = res.format == QLatin1String("rar") || byName == QLatin1String("rar");
    if (sevenZ && (res.encrypted || (!res.ok && isEncryptionError(error)))) {
        if (!sevenZip().isEmpty()) {
            ListResult t = listWith7z(archive, password);
            t.format = QStringLiteral("7z");
            t.formatName = formatName(t.format);
            if (!t.ok && !t.needPassword && res.ok) {
                res.tool = QStringLiteral("7z"); // list from libarchive; extract with 7z
                return res;
            }
            return t;
        }
        if (res.ok) {
            res.tool = QStringLiteral("7z"); // listed, but extracting needs 7-Zip
        } else {
            res.error = QStringLiteral("This 7-Zip archive is protected with a password, which needs the 7-Zip program: %1.")
                            .arg(toolNeeded(QStringLiteral("7z")));
        }
    } else if (rar && (res.encrypted || !res.ok)) {
        if (toolNeeded(QStringLiteral("unar")).isEmpty()) {
            ListResult t = listWithUnar(archive, password);
            t.format = QStringLiteral("rar");
            t.formatName = formatName(t.format);
            if (t.ok || t.needPassword)
                return t;
        }
        if (res.ok) {
            res.tool = QStringLiteral("unar");
        } else {
            res.error = QStringLiteral("Archive Manager can't read this RAR archive by itself (%1). It needs the unar program: %2.")
                            .arg(error, toolNeeded(QStringLiteral("unar")));
        }
    } else if (!res.ok) {
        res.needPassword = isPasswordError(error);
        res.error = error;
    }

    res.formatName = formatName(res.format);
    if (res.ok && res.format == QLatin1String("zip")) {
        const QHash<QString, qint64> packed = zipPackedSizes(archive);
        for (ArchiveItem &item : res.items)
            item.packed = packed.value(item.path, -1);
    }
    return res;
}

JobResult ArchiveEngine::extract(const QString &archive, const QString &format, const QString &tool,
                                 const QStringList &only, const QString &dest, bool useFolders, JobContext &ctx)
{
    JobResult res;
    if (!QDir().mkpath(dest)) {
        res.error = QStringLiteral("Cannot create the folder %1.").arg(dest);
        return res;
    }
    const QString destDir = QDir(dest).absolutePath();
    if (!tool.isEmpty()) {
        const QString missing = toolNeeded(tool);
        if (!missing.isEmpty()) {
            res.error = QStringLiteral("Extracting this archive needs %1.").arg(missing);
            return res;
        }
        return extractWithTool(archive, tool, only, destDir, useFolders, ctx);
    }

    QString error;
    const bool raw = isRawFormat(format);
    struct archive *a = openReader(archive, ctx.password, raw, &error);
    if (!a) {
        res.error = error;
        return res;
    }
    OverwritePolicy policy;
    qint64 done = 0;
    QByteArray rawName = baseName(QFileInfo(archive).fileName()).toUtf8();
    struct archive_entry *e;
    bool failed = false;
    while (!ctx.cancel) {
        const int r = archive_read_next_header(a, &e);
        if (r == ARCHIVE_EOF)
            break;
        if (r < ARCHIVE_WARN) {
            error = errorOf(a);
            failed = true;
            break;
        }
        const QString path = raw ? QString::fromUtf8(rawName) : entryPath(e);
        const unsigned type = archive_entry_filetype(e);
        if (path.isEmpty() || !selected(path, only)) {
            archive_read_data_skip(a);
            continue;
        }
        const QString safe = safeRelative(path);
        if (safe.isEmpty()) {
            res.problems << QStringLiteral("%1: unsafe name (skipped)").arg(path);
            archive_read_data_skip(a);
            continue;
        }
        if (type == AE_IFDIR) {
            if (useFolders) {
                QString err;
                if (makeParents(destDir, safe + QStringLiteral("/x"), &err))
                    addWritten(res, destDir, safe);
                else
                    res.problems << err;
            }
            archive_read_data_skip(a);
            continue;
        }
        const QString rel = useFolders ? safe : safe.section(QLatin1Char('/'), -1);
        const QString target = destDir + QLatin1Char('/') + rel;
        QString err;
        if (!makeParents(destDir, rel, &err)) {
            res.problems << err;
            archive_read_data_skip(a);
            continue;
        }
        const QFileInfo existing(target);
        if (existing.exists() || existing.isSymLink()) {
            if (existing.isDir() && !existing.isSymLink()) {
                res.problems << QStringLiteral("%1: a folder of that name is in the way").arg(rel);
                archive_read_data_skip(a);
                continue;
            }
            if (!policy.decide(target, ctx)) {
                if (!ctx.cancel)
                    res.problems << QStringLiteral("%1: not replaced").arg(rel);
                archive_read_data_skip(a);
                continue;
            }
            QFile::remove(target);
        }
        if (ctx.progress)
            ctx.progress(done, 0, rel);

        const char *hardlink = archive_entry_hardlink(e);
        if (hardlink) {
            // a second name for a file extracted before: a copy of it
            const QString from = destDir + QLatin1Char('/')
                + (useFolders ? safeRelative(QString::fromUtf8(hardlink)) : QString::fromUtf8(hardlink).section(QLatin1Char('/'), -1));
            if (!QFile::copy(from, target))
                res.problems << QStringLiteral("%1: cannot copy it from %2").arg(rel, QString::fromUtf8(hardlink));
            else
                ++res.files;
            archive_read_data_skip(a);
            addWritten(res, destDir, rel);
            continue;
        }
        if (type == AE_IFLNK) {
            // only links that stay inside the folder extracted into
            const QString linkTo = QString::fromUtf8(archive_entry_symlink_utf8(e) ? archive_entry_symlink_utf8(e) : "");
            const QString resolved = QDir::cleanPath(QFileInfo(target).absolutePath() + QLatin1Char('/') + linkTo);
            if (linkTo.isEmpty() || linkTo.startsWith(QLatin1Char('/'))
                || !(resolved == destDir || resolved.startsWith(destDir + QLatin1Char('/')))) {
                res.problems << QStringLiteral("%1: a link out of the folder (skipped)").arg(rel);
            } else if (::symlink(QFile::encodeName(linkTo).constData(), QFile::encodeName(target).constData()) != 0) {
                res.problems << QStringLiteral("%1: cannot make the link").arg(rel);
            } else {
                ++res.files;
                addWritten(res, destDir, rel);
            }
            archive_read_data_skip(a);
            continue;
        }
        if (type != AE_IFREG && !raw) {
            archive_read_data_skip(a); // devices, fifos: not from an archive
            continue;
        }

        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            res.problems << QStringLiteral("%1: %2").arg(rel, out.errorString());
            archive_read_data_skip(a);
            continue;
        }
        const void *buf;
        size_t size;
        la_int64_t offset;
        bool ok = true;
        int status = ARCHIVE_OK;
        for (;;) {
            if (ctx.cancel) {
                ok = false;
                break;
            }
            const int rr = archive_read_data_block(a, &buf, &size, &offset);
            if (rr == ARCHIVE_EOF)
                break;
            if (rr < ARCHIVE_WARN) {
                error = errorOf(a);
                status = rr;
                ok = false;
                break;
            }
            if (out.pos() != offset && !out.seek(offset)) { // sparse files
                error = out.errorString();
                ok = false;
                break;
            }
            if (out.write(static_cast<const char *>(buf), qint64(size)) != qint64(size)) {
                error = QStringLiteral("%1: %2").arg(rel, out.errorString());
                ok = false;
                break;
            }
            done += qint64(size);
            if (ctx.progress)
                ctx.progress(done, 0, rel);
        }
        if (ok && out.size() < archive_entry_size(e) && archive_entry_size_is_set(e))
            out.resize(archive_entry_size(e)); // a sparse file's hole at the end
        out.close();
        if (!ok) {
            out.remove();
            if (ctx.cancel)
                break;
            if (isPasswordError(error)) {
                res.needPassword = true;
                failed = true;
                break;
            }
            res.problems << QStringLiteral("%1: %2").arg(rel, error);
            if (status == ARCHIVE_FATAL) {
                failed = true;
                break;
            }
            continue;
        }
        ::chmod(QFile::encodeName(target).constData(), mode_t(fileMode(e, false)));
        if (archive_entry_mtime_is_set(e) && !raw)
            out.setFileTime(QDateTime::fromSecsSinceEpoch(archive_entry_mtime(e)), QFileDevice::FileModificationTime);
        ++res.files;
        res.bytes += out.size();
        addWritten(res, destDir, rel);
    }
    archive_read_free(a);
    if (ctx.cancel) {
        res.cancelled = true;
        return res;
    }
    if (failed) {
        res.error = res.needPassword ? QStringLiteral("The password is wrong.") : error;
        return res;
    }
    res.ok = true;
    return res;
}

JobResult ArchiveEngine::test(const QString &archive, const QString &tool, JobContext &ctx)
{
    JobResult res;
    if (!tool.isEmpty()) {
        const QString missing = toolNeeded(tool);
        if (!missing.isEmpty()) {
            res.error = QStringLiteral("Testing this archive needs %1.").arg(missing);
            return res;
        }
        ToolRun r;
        if (tool == QLatin1String("7z")) {
            r = runTool(sevenZip(), {QStringLiteral("t"), QStringLiteral("-sccUTF-8"), passwordArg(ctx.password),
                                     QStringLiteral("--"), archive}, &ctx);
        } else {
            QStringList args{QStringLiteral("-t")};
            if (!ctx.password.isEmpty())
                args << QStringLiteral("-p") << ctx.password;
            r = runTool(lsar(), args << QStringLiteral("--") << archive, &ctx);
        }
        if (r.cancelled) {
            res.cancelled = true;
            return res;
        }
        res.ok = r.exitCode == 0;
        if (!res.ok) {
            res.needPassword = toolPasswordProblem(r);
            res.problems << toolError(r, tool == QLatin1String("7z") ? QStringLiteral("7-Zip") : QStringLiteral("lsar"));
        }
        return res;
    }

    QString error;
    const bool raw = isRawFormat(formatForName(QFileInfo(archive).fileName()));
    struct archive *a = openReader(archive, ctx.password, raw, &error);
    if (!a) {
        res.error = error;
        return res;
    }
    struct archive_entry *e;
    qint64 done = 0;
    while (!ctx.cancel) {
        const int r = archive_read_next_header(a, &e);
        if (r == ARCHIVE_EOF)
            break;
        if (r < ARCHIVE_WARN) {
            res.problems << errorOf(a);
            break;
        }
        const QString path = raw ? baseName(QFileInfo(archive).fileName()) : entryPath(e);
        if (archive_entry_filetype(e) != AE_IFREG && !raw)
            continue;
        const void *buf;
        size_t size;
        la_int64_t offset;
        int rr;
        while (!ctx.cancel && (rr = archive_read_data_block(a, &buf, &size, &offset)) == ARCHIVE_OK) {
            done += qint64(size);
            if (ctx.progress)
                ctx.progress(done, 0, path);
        }
        if (ctx.cancel)
            break;
        if (rr < ARCHIVE_WARN) {
            const QString err = errorOf(a);
            if (isPasswordError(err)) {
                res.needPassword = true;
                res.problems << QStringLiteral("%1: the password is wrong").arg(path);
                break;
            }
            res.problems << QStringLiteral("%1: %2").arg(path, err);
            if (rr == ARCHIVE_FATAL)
                break;
        } else {
            ++res.files;
        }
    }
    archive_read_free(a);
    res.cancelled = ctx.cancel;
    res.ok = !res.cancelled && res.problems.isEmpty();
    return res;
}

JobResult ArchiveEngine::add(const QString &archive, const QStringList &files, const QString &inside,
                             const WriteOptions &options, JobContext &ctx)
{
    // 7z with a password: libarchive can't write that, 7-Zip can
    if (options.format == QLatin1String("7z")
        && (!options.password.isEmpty() || (!ctx.password.isEmpty() && QFileInfo::exists(archive)))) {
        JobResult res;
        const QString missing = toolNeeded(QStringLiteral("7z"));
        if (!missing.isEmpty()) {
            res.error = QStringLiteral("A 7-Zip archive with a password needs %1. (A .zip can have a password without it.)").arg(missing);
            return res;
        }
        if (!inside.isEmpty()) {
            res.error = QStringLiteral("Files can only go in the top folder of a 7-Zip archive with a password.");
            return res;
        }
        const QString pw = options.password.isEmpty() ? ctx.password : options.password;
        QStringList args{QStringLiteral("a"), QStringLiteral("-t7z"), QStringLiteral("-mx=%1").arg(qBound(0, options.level, 9)),
                         QStringLiteral("-bsp1"), QStringLiteral("-sccUTF-8"), QStringLiteral("-mhe=on"), passwordArg(pw),
                         QStringLiteral("--"), archive};
        for (const QString &f : files)
            args << QFileInfo(f).absoluteFilePath();
        static const QRegularExpression percent(QStringLiteral("(\\d+)%"));
        const ToolRun r = runTool(sevenZip(), args, &ctx, [&](const QByteArray &chunk) {
            QRegularExpressionMatchIterator m = percent.globalMatch(QString::fromUtf8(chunk));
            int last = -1;
            while (m.hasNext())
                last = m.next().captured(1).toInt();
            if (last >= 0 && ctx.progress)
                ctx.progress(last, 100, QFileInfo(archive).fileName());
        });
        if (r.cancelled) {
            res.cancelled = true;
            return res;
        }
        res.ok = r.exitCode == 0;
        if (!res.ok) {
            res.needPassword = toolPasswordProblem(r);
            res.error = toolError(r, QStringLiteral("7-Zip"));
        }
        res.files = files.size();
        return res;
    }
    qint64 bytes = 0;
    const QVector<NewFile> gathered = gather(files, inside, &bytes);
    if (gathered.isEmpty()) {
        JobResult res;
        res.error = QStringLiteral("There's nothing to add.");
        return res;
    }
    return rewrite(archive, gathered, bytes, QStringList(), options, ctx);
}

JobResult ArchiveEngine::remove(const QString &archive, const QStringList &paths, const WriteOptions &options,
                                JobContext &ctx)
{
    if (options.format == QLatin1String("7z") && !ctx.password.isEmpty()) {
        JobResult res;
        const QString missing = toolNeeded(QStringLiteral("7z"));
        if (!missing.isEmpty()) {
            res.error = QStringLiteral("Changing a 7-Zip archive with a password needs %1.").arg(missing);
            return res;
        }
        QStringList args{QStringLiteral("d"), QStringLiteral("-sccUTF-8"), passwordArg(ctx.password),
                         QStringLiteral("--"), archive};
        const ToolRun r = runTool(sevenZip(), args << paths, &ctx);
        res.cancelled = r.cancelled;
        res.ok = r.exitCode == 0;
        if (!res.ok && !res.cancelled) {
            res.needPassword = toolPasswordProblem(r);
            res.error = toolError(r, QStringLiteral("7-Zip"));
        }
        return res;
    }
    return rewrite(archive, {}, 0, paths, options, ctx);
}
