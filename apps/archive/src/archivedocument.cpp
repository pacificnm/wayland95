#include "archivedocument.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace {

QString sizeText(qint64 bytes);

// our own programs: next to this one (a build tree) or on the PATH
QString ours(const QString &program)
{
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(program);
    return QFileInfo(beside).isExecutable() ? beside : QStandardPaths::findExecutable(program);
}

QString kindOf(const QString &name, bool isDir)
{
    if (isDir)
        return QStringLiteral("folder");
    if (!ArchiveEngine::formatForName(name).isEmpty())
        return QStringLiteral("archive");
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension);
    const QString m = mime.name();
    if (m.startsWith(QLatin1String("image/")))
        return QStringLiteral("image");
    if (m.startsWith(QLatin1String("audio/")))
        return QStringLiteral("audio");
    if (m.startsWith(QLatin1String("video/")))
        return QStringLiteral("video");
    if (m == QLatin1String("application/pdf"))
        return QStringLiteral("pdf");
    if (mime.inherits(QStringLiteral("application/x-shellscript")) || m == QLatin1String("application/x-executable"))
        return QStringLiteral("exec");
    if (mime.inherits(QStringLiteral("text/plain")))
        return QStringLiteral("text");
    return QStringLiteral("file");
}

int ratio(const ArchiveItem &i)
{
    if (i.packed < 0 || i.size <= 0)
        return -1;
    return qBound(0, int(100 - (i.packed * 100 + i.size / 2) / i.size), 100);
}

} // namespace

ArchiveDocument::ArchiveDocument(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, [this]() {
        m_busy = false;
        m_ctx.reset();
        auto done = std::move(m_done);
        m_done = nullptr;
        emit busyChanged();
        if (done)
            done();
    });
}

ArchiveDocument::~ArchiveDocument()
{
    cancel();
    m_watcher.waitForFinished();
}

// ------------------------------------------------------------------ model

int ArchiveDocument::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ArchiveDocument::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case NameRole: return r.name;
    case FolderRole: return r.folder;
    case ArchivePathRole: return r.item.path;
    case IsDirRole: return r.item.isDir;
    case ModifiedRole:
        return r.item.modified.isValid() ? QLocale().toString(r.item.modified, QStringLiteral("M/d/yy h:mm AP")) : QString();
    case SizeRole: return r.item.isDir || r.item.size < 0 ? QString() : QLocale().toString(r.item.size);
    case PackedRole: return r.item.isDir || r.item.packed < 0 ? QString() : QLocale().toString(r.item.packed);
    case RatioRole: {
        const int pc = ratio(r.item);
        return r.item.isDir || pc < 0 ? QString() : QStringLiteral("%1%").arg(pc);
    }
    case EncryptedRole: return r.item.encrypted;
    case SelectedRole: return r.selected;
    case KindRole: return kindOf(r.name, r.item.isDir);
    }
    return {};
}

QHash<int, QByteArray> ArchiveDocument::roleNames() const
{
    return {
        {NameRole, "name"}, {FolderRole, "folder"}, {ArchivePathRole, "archivePath"}, {IsDirRole, "isDir"},
        {ModifiedRole, "modified"}, {SizeRole, "sizeText"}, {PackedRole, "packedText"}, {RatioRole, "ratioText"},
        {EncryptedRole, "encrypted"}, {SelectedRole, "selected"}, {KindRole, "kind"},
    };
}

QString ArchiveDocument::fileName() const
{
    return QFileInfo(m_path).fileName();
}

bool ArchiveDocument::writable() const
{
    return readOnlyReason().isEmpty();
}

QString ArchiveDocument::readOnlyReason() const
{
    if (m_path.isEmpty())
        return QStringLiteral("No archive is open.");
    // a new .gz (and the like) takes its one file
    if (ArchiveEngine::isSingleFile(m_format) && m_isNew && m_rows.isEmpty())
        return QString();
    if (ArchiveEngine::isSingleFile(m_format))
        return QStringLiteral("A %1 holds just one file; nothing can be added to it or deleted from it.").arg(m_formatName);
    if (!ArchiveEngine::canWrite(m_format))
        return QStringLiteral("Archive Manager can read %1s but not change them. Extract the files and make a "
                              ".zip or .7z of them instead.").arg(m_formatName);
    if (!m_isNew && !QFileInfo(m_path).isWritable())
        return QStringLiteral("%1 is read-only.").arg(fileName());
    return QString();
}

int ArchiveDocument::fileCount() const
{
    return int(std::count_if(m_all.cbegin(), m_all.cend(), [](const ArchiveItem &i) { return !i.isDir; }));
}

QString ArchiveDocument::totalText() const
{
    if (m_path.isEmpty())
        return QString();
    qint64 total = 0;
    for (const ArchiveItem &i : m_all)
        total += qMax<qint64>(0, i.size);
    const int files = fileCount();
    QString text = QStringLiteral("Total %1 file%2, %3").arg(files).arg(files == 1 ? "" : "s").arg(sizeText(total));
    const qint64 onDisk = QFileInfo(m_path).size();
    if (total > 0 && onDisk > 0 && QFileInfo::exists(m_path))
        text += QStringLiteral(" (%1 packed, %2%)").arg(sizeText(onDisk)).arg(qMax<qint64>(0, 100 - onDisk * 100 / total));
    return text;
}

int ArchiveDocument::selectionCount() const
{
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(), [](const Row &r) { return r.selected; }));
}

QString ArchiveDocument::selectionText() const
{
    int n = 0;
    qint64 bytes = 0;
    for (const Row &r : m_rows)
        if (r.selected) {
            ++n;
            bytes += qMax<qint64>(0, r.item.size);
        }
    return QStringLiteral("Selected %1 file%2, %3").arg(n).arg(n == 1 ? "" : "s").arg(sizeText(bytes));
}

QString Archives::formatSize(qint64 bytes) const
{
    return sizeText(bytes);
}

namespace {
QString sizeText(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 bytes").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1KB").arg(QString::number(bytes / 1024.0, 'f', bytes < 10 * 1024 ? 1 : 0));
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1MB").arg(QString::number(bytes / (1024.0 * 1024), 'f', 1));
    return QStringLiteral("%1GB").arg(QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2));
}
} // namespace

bool Archives::exists(const QString &path) const
{
    return QFileInfo::exists(path);
}

QStringList Archives::urlsToPaths(const QVariant &urls) const
{
    QStringList out;
    for (const QVariant &v : urls.toList()) {
        const QUrl u = v.toUrl();
        if (u.isLocalFile())
            out << u.toLocalFile();
    }
    return out;
}

void Archives::showFolder(const QString &path) const
{
    const QFileInfo fi(path);
    const QString files = ours(QStringLiteral("win95files"));
    if (fi.isDir() && !files.isEmpty()) {
        QProcess::startDetached(files, {fi.absoluteFilePath()});
    } else if (fi.exists() && !files.isEmpty()) {
        QProcess::startDetached(files, {fi.absolutePath(), QStringLiteral("--select"), fi.fileName()});
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath()));
    }
}

QString Archives::formatsText() const
{
    auto status = [](const QString &tool) {
        const QString missing = ArchiveEngine::toolNeeded(tool);
        return missing.isEmpty() ? QStringLiteral("installed") : QStringLiteral("not installed - ") + missing;
    };
    return QStringLiteral(
               "Read and write: ZIP (.zip, with or without a password), 7-Zip (.7z), Tar (.tar, .tar.gz, "
               ".tgz, .tar.bz2, .tar.xz, .tar.zst)\n"
               "Make from one file: .gz, .bz2, .xz, .zst\n"
               "Read only: RAR (.rar), CD/DVD images (.iso), Cabinet (.cab), .deb, cpio, LHA, XAR\n\n"
               "7-Zip archives with a password: 7-Zip program, %1\n"
               "RAR archives with a password or newer compression: unar program, %2")
        .arg(status(QStringLiteral("7z")), status(QStringLiteral("unar")));
}

QString Archives::setting(const QString &key, const QString &fallback) const
{
    return QSettings().value(key, fallback).toString();
}

void Archives::setSetting(const QString &key, const QString &value)
{
    QSettings().setValue(key, value);
}

// ---------------------------------------------------------------- sorting

void ArchiveDocument::setSortColumn(int c)
{
    if (c == m_sortColumn)
        return;
    m_sortColumn = c;
    emit sortChanged();
    beginResetModel();
    sortRows();
    endResetModel();
}

void ArchiveDocument::setSortAscending(bool a)
{
    if (a == m_sortAscending)
        return;
    m_sortAscending = a;
    emit sortChanged();
    beginResetModel();
    sortRows();
    endResetModel();
}

void ArchiveDocument::sortRows()
{
    const int col = m_sortColumn;
    auto key = [col](const Row &a, const Row &b) -> int {
        auto cmp = [](auto x, auto y) { return x < y ? -1 : (y < x ? 1 : 0); };
        switch (col) {
        case SortName: return QString::localeAwareCompare(a.name.toLower(), b.name.toLower());
        case SortModified: return cmp(a.item.modified, b.item.modified);
        case SortSize: return cmp(a.item.size, b.item.size);
        case SortRatio: return cmp(ratio(a.item), ratio(b.item));
        case SortPacked: return cmp(a.item.packed, b.item.packed);
        default: {
            const int f = QString::localeAwareCompare(a.folder.toLower(), b.folder.toLower());
            return f ? f : QString::localeAwareCompare(a.name.toLower(), b.name.toLower());
        }
        }
    };
    const bool asc = m_sortAscending;
    std::stable_sort(m_rows.begin(), m_rows.end(), [&](const Row &a, const Row &b) {
        int k = key(a, b);
        if (k == 0 && col != SortName && col != SortPath)
            k = QString::compare(a.item.path, b.item.path, Qt::CaseInsensitive);
        return asc ? k < 0 : k > 0;
    });
}

void ArchiveDocument::setItems(const QVector<ArchiveItem> &items)
{
    beginResetModel();
    m_all = items;
    m_rows.clear();
    // the files, and the folders with nothing in them (the others show as
    // the Path of what's in them)
    QSet<QString> parents;
    for (const ArchiveItem &i : items) {
        QString p = i.path;
        while (p.contains(QLatin1Char('/'))) {
            p = p.section(QLatin1Char('/'), 0, -2);
            parents.insert(p);
        }
    }
    for (const ArchiveItem &i : items) {
        if (i.isDir && parents.contains(i.path))
            continue;
        Row r;
        r.item = i;
        r.name = i.path.section(QLatin1Char('/'), -1);
        r.folder = i.path.contains(QLatin1Char('/')) ? i.path.section(QLatin1Char('/'), 0, -2) + QLatin1Char('/') : QString();
        if (i.isDir) {
            r.folder = i.path + QLatin1Char('/');
            r.name = QStringLiteral("(folder)");
        }
        m_rows << r;
    }
    sortRows();
    m_anchor = -1;
    endResetModel();
    emit changed();
    emit selectionChanged();
}

// -------------------------------------------------------------- selection

void ArchiveDocument::select(int row, int mode)
{
    if (row < 0 || row >= m_rows.size())
        return;
    if (mode == Toggle) {
        m_rows[row].selected = !m_rows[row].selected;
        m_anchor = row;
    } else if (mode == Extend && m_anchor >= 0) {
        for (int i = 0; i < m_rows.size(); ++i)
            m_rows[i].selected = i >= qMin(row, m_anchor) && i <= qMax(row, m_anchor);
    } else {
        for (int i = 0; i < m_rows.size(); ++i)
            m_rows[i].selected = i == row;
        m_anchor = row;
    }
    emit dataChanged(index(0), index(int(m_rows.size()) - 1), {SelectedRole});
    emit selectionChanged();
}

void ArchiveDocument::selectAll()
{
    for (Row &r : m_rows)
        r.selected = true;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1), {SelectedRole});
    emit selectionChanged();
}

void ArchiveDocument::clearSelection()
{
    for (Row &r : m_rows)
        r.selected = false;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1), {SelectedRole});
    emit selectionChanged();
}

int ArchiveDocument::firstSelectedRow() const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).selected)
            return i;
    return -1;
}

QStringList ArchiveDocument::selectedPaths() const
{
    QStringList out;
    for (const Row &r : m_rows)
        if (r.selected)
            out << r.item.path;
    return out;
}

QString ArchiveDocument::pathAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).item.path : QString();
}

// ------------------------------------------------------------------- jobs

void ArchiveDocument::run(const QString &operation, const Job &job, const std::function<void()> &done)
{
    if (m_busy)
        return;
    auto ctx = std::make_shared<JobContext>();
    ctx->password = m_password;
    // progress, at most every 50ms (and each new file)
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    auto lastFile = std::make_shared<QString>();
    ctx->progress = [this, timer, lastFile](qint64 doneBytes, qint64 total, const QString &file) {
        if (file == *lastFile && timer->elapsed() < 50)
            return;
        timer->restart();
        *lastFile = file;
        const int pc = total > 0 ? int(qBound<qint64>(qint64(0), doneBytes * 100 / total, qint64(100))) : -1;
        QMetaObject::invokeMethod(this, [this, pc, file]() { setProgress(pc, file); }, Qt::QueuedConnection);
    };
    // (a plain pointer: the context holds this function, and lives as long as the job)
    JobContext *raw = ctx.get();
    ctx->askOverwrite = [this, raw](const QString &path) {
        QMutexLocker lock(&m_askMutex);
        m_answer = -1;
        QMetaObject::invokeMethod(this, [this, path]() { emit overwriteAsked(path); }, Qt::QueuedConnection);
        while (m_answer < 0 && !raw->cancel)
            m_askCond.wait(&m_askMutex, 200);
        return raw->cancel ? int(OverwriteCancel) : m_answer;
    };
    m_ctx = ctx;
    m_done = done;
    m_busy = true;
    m_operation = operation;
    m_percent = -1;
    m_currentFile.clear();
    emit busyChanged();
    emit progressChanged();
    m_watcher.setFuture(QtConcurrent::run([job, ctx]() { job(*ctx); }));
}

void ArchiveDocument::setProgress(int percent, const QString &file)
{
    if (!m_busy)
        return;
    m_percent = percent;
    m_currentFile = file;
    emit progressChanged();
}

void ArchiveDocument::cancel()
{
    if (m_ctx)
        m_ctx->cancel = true;
    QMutexLocker lock(&m_askMutex);
    m_askCond.wakeAll();
}

void ArchiveDocument::answerOverwrite(int answer)
{
    QMutexLocker lock(&m_askMutex);
    m_answer = qBound(0, answer, int(OverwriteCancel));
    m_askCond.wakeAll();
}

void ArchiveDocument::retry(const QString &password)
{
    m_password = password;
    auto again = std::move(m_retry);
    m_retry = nullptr;
    if (again)
        again();
}

void ArchiveDocument::open(const QString &path)
{
    if (m_busy)
        return;
    const QString abs = QFileInfo(path).absoluteFilePath();
    // another archive: not the last one's password (but this one's, asked
    // for while opening it)
    if (abs != m_path && abs != m_passwordFor)
        m_password.clear();
    m_passwordFor = abs;
    auto result = std::make_shared<ListResult>();
    const QString password = m_password;
    run(QStringLiteral("Opening"), [abs, password, result](JobContext &) { *result = ArchiveEngine::list(abs, password); },
        [this, abs, result]() {
            if (!result->ok) {
                if (result->needPassword) {
                    m_retry = [this, abs]() { open(abs); };
                    emit passwordNeeded(QStringLiteral("open"), !m_password.isEmpty());
                    return;
                }
                emit failed(QStringLiteral("Archive Manager"),
                            QStringLiteral("Cannot open %1.\n\n%2").arg(QFileInfo(abs).fileName(), result->error));
                emit opened();
                return;
            }
            m_path = abs;
            m_isNew = false;
            m_format = result->format;
            m_formatName = result->formatName;
            m_tool = result->tool;
            m_encrypted = result->encrypted;
            setItems(result->items);
            emit opened();
        });
}

void ArchiveDocument::create(const QString &path, const QString &format)
{
    if (m_busy)
        return;
    m_path = QFileInfo(path).absoluteFilePath();
    m_isNew = true;
    m_format = format.isEmpty() ? ArchiveEngine::formatForName(path) : format;
    m_formatName = ArchiveEngine::formatLabel(m_format);
    m_tool.clear();
    m_encrypted = false;
    m_password.clear();
    m_passwordFor.clear();
    setItems({});
}

void ArchiveDocument::close()
{
    if (m_busy)
        return;
    m_path.clear();
    m_format.clear();
    m_formatName.clear();
    m_tool.clear();
    m_encrypted = false;
    m_isNew = false;
    m_password.clear();
    m_passwordFor.clear();
    setItems({});
}

void ArchiveDocument::reload()
{
    if (!m_path.isEmpty() && !m_isNew)
        open(m_path);
}

QStringList ArchiveDocument::pathsForExtract(bool selectedOnly, qint64 *bytes) const
{
    QStringList paths;
    *bytes = 0;
    for (const Row &r : m_rows) {
        if (selectedOnly && !r.selected)
            continue;
        if (selectedOnly)
            paths << r.item.path;
    }
    for (const ArchiveItem &i : m_all) {
        if (i.isDir)
            continue;
        if (!selectedOnly) {
            *bytes += qMax<qint64>(0, i.size);
            continue;
        }
        for (const QString &p : paths)
            if (i.path == p || i.path.startsWith(p + QLatin1Char('/'))) {
                *bytes += qMax<qint64>(0, i.size);
                break;
            }
    }
    return paths;
}

void ArchiveDocument::extract(const QString &dest, bool selectedOnly, bool useFolders)
{
    if (m_busy || m_path.isEmpty())
        return;
    qint64 total = 0;
    const QStringList only = pathsForExtract(selectedOnly && selectionCount() > 0, &total);
    const QString archive = m_path, format = m_format, tool = m_tool;
    auto result = std::make_shared<JobResult>();
    run(QStringLiteral("Extracting"),
        [=](JobContext &ctx) {
            // byte progress against what's being extracted
            auto inner = ctx.progress;
            if (tool.isEmpty() && total > 0)
                ctx.progress = [inner, total](qint64 d, qint64, const QString &f) { inner(d, total, f); };
            *result = ArchiveEngine::extract(archive, format, tool, only, dest, useFolders, ctx);
        },
        [=]() {
            if (result->needPassword) {
                m_retry = [=]() { extract(dest, selectedOnly, useFolders); };
                emit passwordNeeded(QStringLiteral("extract"), !m_password.isEmpty());
                return;
            }
            const QString summary = result->cancelled ? QStringLiteral("Extracting was cancelled.")
                : result->ok ? QStringLiteral("Extracted %1 file%2 to %3.").arg(result->files).arg(result->files == 1 ? "" : "s").arg(dest)
                             : result->error;
            emit finished(QStringLiteral("extract"), result->ok, summary, result->problems, result->written);
        });
}

void ArchiveDocument::view(int row)
{
    if (m_busy || row < 0 || row >= m_rows.size() || m_rows.at(row).item.isDir)
        return;
    if (!m_viewDir)
        m_viewDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/archive-manager-view-XXXXXX"));
    // each view in its own folder: two files of one name from different folders
    const QString dest = m_viewDir->path() + QStringLiteral("/%1").arg(QDateTime::currentMSecsSinceEpoch());
    const QString path = m_rows.at(row).item.path;
    const QString archive = m_path, format = m_format, tool = m_tool;
    auto result = std::make_shared<JobResult>();
    run(QStringLiteral("Extracting"),
        [=](JobContext &ctx) { *result = ArchiveEngine::extract(archive, format, tool, {path}, dest, false, ctx); },
        [=]() {
            if (result->needPassword) {
                m_retry = [=]() { view(row); };
                emit passwordNeeded(QStringLiteral("view"), !m_password.isEmpty());
                return;
            }
            if (!result->ok || result->written.isEmpty()) {
                if (!result->cancelled)
                    emit failed(QStringLiteral("View"), result->error.isEmpty()
                                    ? QStringLiteral("Cannot extract %1.\n\n%2").arg(path, result->problems.join(QLatin1Char('\n')))
                                    : result->error);
                return;
            }
            openViewed(result->written.first());
            emit finished(QStringLiteral("view"), true, QStringLiteral("Opened %1.").arg(QFileInfo(path).fileName()), {}, {});
        });
}

void ArchiveDocument::openViewed(const QString &file)
{
    const QFileInfo info(file);
    const QString suffix = info.suffix().toLower();
    const QString kind = kindOf(info.fileName(), false);
    QString program;
    if (kind == QLatin1String("archive"))
        program = QCoreApplication::applicationFilePath();
    else if (QStringList{"md", "markdown", "mdown", "mkd"}.contains(suffix))
        program = ours(QStringLiteral("win95markdown"));
    else if (QStringList{"jpg", "jpeg", "png", "gif", "bmp", "webp", "tif", "tiff"}.contains(suffix))
        program = ours(QStringLiteral("win95photos"));
    else if (kind == QLatin1String("text"))
        program = ours(QStringLiteral("win95notepad"));
    if (!program.isEmpty() && QProcess::startDetached(program, {file}, info.absolutePath()))
        return;
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(file)))
        emit failed(QStringLiteral("View"), QStringLiteral("There is no program associated with '%1'.").arg(info.fileName()));
}

void ArchiveDocument::test()
{
    if (m_busy || m_path.isEmpty() || m_isNew)
        return;
    const QString archive = m_path, tool = m_tool;
    qint64 total = 0;
    for (const ArchiveItem &i : m_all)
        total += qMax<qint64>(0, i.size);
    auto result = std::make_shared<JobResult>();
    run(QStringLiteral("Testing"),
        [=](JobContext &ctx) {
            auto inner = ctx.progress;
            if (tool.isEmpty() && total > 0)
                ctx.progress = [inner, total](qint64 d, qint64, const QString &f) { inner(d, total, f); };
            *result = ArchiveEngine::test(archive, tool, ctx);
        },
        [=]() {
            if (result->needPassword && !result->cancelled) {
                m_retry = [=]() { test(); };
                emit passwordNeeded(QStringLiteral("test"), !m_password.isEmpty());
                return;
            }
            QString summary;
            if (result->cancelled)
                summary = QStringLiteral("Testing was cancelled.");
            else if (!result->error.isEmpty())
                summary = result->error;
            else if (result->ok)
                summary = QStringLiteral("No errors detected in compressed data of %1.").arg(QFileInfo(archive).fileName());
            else
                summary = QStringLiteral("Errors were found in %1.").arg(QFileInfo(archive).fileName());
            emit finished(QStringLiteral("test"), result->ok, summary, result->problems, {});
        });
}

void ArchiveDocument::add(const QStringList &files, const QString &format, int level, const QString &password)
{
    if (m_busy || m_path.isEmpty() || files.isEmpty())
        return;
    const QString archive = m_path;
    for (const QString &f : files)
        if (QFileInfo(f).absoluteFilePath() == archive) {
            emit failed(QStringLiteral("Add"), QStringLiteral("An archive can't be added to itself."));
            return;
        }
    WriteOptions options;
    options.format = m_isNew && !format.isEmpty() ? format : m_format;
    options.level = level;
    // an archive with a password keeps it (its files are read with it)
    options.password = !password.isEmpty() ? password : (m_encrypted ? m_password : QString());
    if (m_isNew) {
        m_format = options.format;
        m_formatName = ArchiveEngine::formatLabel(m_format);
    }
    const bool wasNew = m_isNew;
    auto result = std::make_shared<JobResult>();
    run(QStringLiteral("Adding"),
        [=](JobContext &ctx) { *result = ArchiveEngine::add(archive, files, QString(), options, ctx); },
        [=]() {
            if (result->needPassword && !result->cancelled) {
                m_retry = [=]() { add(files, format, level, password); };
                emit passwordNeeded(QStringLiteral("add"), !m_password.isEmpty());
                return;
            }
            const QString summary = result->cancelled ? QStringLiteral("Adding was cancelled; the archive wasn't changed.")
                : result->ok ? QStringLiteral("Added %1 file%2 to %3.").arg(result->files).arg(result->files == 1 ? "" : "s").arg(QFileInfo(archive).fileName())
                             : result->error;
            if (result->ok) {
                if (!password.isEmpty())
                    m_password = password;
                m_isNew = false;
            } else {
                m_isNew = wasNew;
            }
            emit finished(QStringLiteral("add"), result->ok, summary, result->problems, {archive});
            if (QFileInfo::exists(archive))
                reload();
        });
}

void ArchiveDocument::removeSelected()
{
    if (m_busy || m_path.isEmpty() || selectionCount() == 0)
        return;
    const QStringList paths = selectedPaths();
    const QString archive = m_path;
    WriteOptions options;
    options.format = m_format;
    options.level = 6;
    options.password = m_encrypted ? m_password : QString();
    auto result = std::make_shared<JobResult>();
    run(QStringLiteral("Deleting"),
        [=](JobContext &ctx) { *result = ArchiveEngine::remove(archive, paths, options, ctx); },
        [=]() {
            if (result->needPassword && !result->cancelled) {
                m_retry = [=]() { removeSelected(); };
                emit passwordNeeded(QStringLiteral("delete"), !m_password.isEmpty());
                return;
            }
            const QString summary = result->cancelled ? QStringLiteral("Deleting was cancelled; the archive wasn't changed.")
                : result->ok ? QStringLiteral("Deleted %1 item%2 from %3.").arg(paths.size()).arg(paths.size() == 1 ? "" : "s").arg(QFileInfo(archive).fileName())
                             : result->error;
            emit finished(QStringLiteral("delete"), result->ok, summary, result->problems, {});
            reload();
        });
}
