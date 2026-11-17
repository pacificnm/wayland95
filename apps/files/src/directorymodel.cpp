#include "directorymodel.h"

#include <QClipboard>
#include <QCollator>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMimeData>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>

#include "drives.h"
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace {

fs::path toFsPath(const QString &p)
{
    return fs::path(QFile::encodeName(p).toStdString());
}

QString iconKindForMime(const QMimeType &mime, const QFileInfo &info)
{
    const QString name = mime.name();
    if (name.startsWith(QLatin1String("image/")))
        return QStringLiteral("image");
    if (name.startsWith(QLatin1String("audio/")))
        return QStringLiteral("audio");
    if (name.startsWith(QLatin1String("video/")))
        return QStringLiteral("video");
    if (name == QLatin1String("application/pdf"))
        return QStringLiteral("pdf");
    static const QStringList archives = {
        QStringLiteral("application/zip"), QStringLiteral("application/x-tar"),
        QStringLiteral("application/gzip"), QStringLiteral("application/x-7z-compressed"),
        QStringLiteral("application/vnd.rar"), QStringLiteral("application/x-xz"),
        QStringLiteral("application/x-bzip2"), QStringLiteral("application/zstd"),
        QStringLiteral("application/x-compressed-tar"), QStringLiteral("application/x-xz-compressed-tar"),
        QStringLiteral("application/x-zstd-compressed-tar"), QStringLiteral("application/vnd.debian.binary-package"),
    };
    for (const QString &a : archives)
        if (mime.inherits(a))
            return QStringLiteral("archive");
    if (name == QLatin1String("application/x-executable") || name == QLatin1String("application/x-pie-executable")
        || name == QLatin1String("application/x-sharedlib") || name == QLatin1String("application/vnd.appimage"))
        return info.isExecutable() ? QStringLiteral("exec") : QStringLiteral("file");
    if (mime.inherits(QStringLiteral("application/x-shellscript")) || name == QLatin1String("application/x-desktop"))
        return QStringLiteral("exec");
    if (mime.inherits(QStringLiteral("text/plain")))
        return QStringLiteral("text");
    return QStringLiteral("file");
}

bool isElfExecutable(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.read(4) == QByteArray("\x7f" "ELF", 4) && QFileInfo(path).isExecutable();
}

// "Copy of x", "Copy (2) of x", ... - the Win95 naming for pasting a file
// next to an existing one with the same name
QString uniqueName(const QDir &dir, const QString &name)
{
    if (!dir.exists(name))
        return name;
    for (int n = 1;; ++n) {
        const QString candidate = n == 1 ? QStringLiteral("Copy of %1").arg(name)
                                         : QStringLiteral("Copy (%1) of %2").arg(n).arg(name);
        if (!dir.exists(candidate))
            return candidate;
    }
}

// Runs on a worker thread. Returns human-readable error lines.
QStringList transferFiles(const QStringList &sources, const QString &destDir, bool move)
{
    QStringList errors;
    const QDir dest(destDir);
    const QString destCanonical = QFileInfo(destDir).canonicalFilePath();

    for (const QString &src : sources) {
        const QFileInfo info(src);
        const QString name = info.fileName();
        if (!info.exists() && !info.isSymLink()) {
            errors << QStringLiteral("Cannot find '%1'.").arg(name);
            continue;
        }
        if (info.isDir() && !info.isSymLink()) {
            const QString srcCanonical = info.canonicalFilePath();
            if (destCanonical == srcCanonical || destCanonical.startsWith(srcCanonical + QLatin1Char('/'))) {
                errors << QStringLiteral("Cannot %1 '%2': the destination folder is inside the source folder.")
                              .arg(move ? QStringLiteral("move") : QStringLiteral("copy"), name);
                continue;
            }
        }
        // moving something into the folder it's already in is a no-op
        if (move && QFileInfo(info.absolutePath()).canonicalFilePath() == destCanonical)
            continue;

        const QString target = dest.filePath(uniqueName(dest, name));
        std::error_code ec;
        if (move) {
            fs::rename(toFsPath(src), toFsPath(target), ec);
            if (!ec)
                continue;
            if (ec != std::errc::cross_device_link) {
                errors << QStringLiteral("Cannot move '%1': %2").arg(name, QString::fromLocal8Bit(ec.message()));
                continue;
            }
            ec.clear(); // different filesystem: copy, then delete the original
        }
        fs::copy(toFsPath(src), toFsPath(target),
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) {
            errors << QStringLiteral("Cannot copy '%1': %2").arg(name, QString::fromLocal8Bit(ec.message()));
            continue;
        }
        if (move) {
            fs::remove_all(toFsPath(src), ec);
            if (ec)
                errors << QStringLiteral("Copied '%1' but could not remove the original: %2")
                              .arg(name, QString::fromLocal8Bit(ec.message()));
        }
    }
    return errors;
}

bool removePath(const QString &path, QString *errorOut)
{
    std::error_code ec;
    fs::remove_all(toFsPath(path), ec); // does not follow symlinks
    if (ec && errorOut)
        *errorOut = QString::fromLocal8Bit(ec.message());
    return !ec;
}

} // namespace

DirectoryModel::DirectoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(150);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this] {
        if (m_autoRefresh)
            reload(true);
        else
            m_refreshPending = true;
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_reloadTimer.start(); });

    connect(&m_transfer, &QFutureWatcher<QStringList>::finished, this, [this] {
        m_busy = false;
        emit busyChanged();
        const QStringList errors = m_transfer.result();
        reload(true);
        if (!errors.isEmpty())
            emit error(QStringLiteral("Error Copying File or Folder"), errors.join(QLatin1Char('\n')));
    });

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &DirectoryModel::updateCanPaste);
    updateCanPaste();

    // a drive plugged in, pulled out, opened or ejected
    connect(Drives::instance(), &Drives::drivesChanged, this, [this] {
        if (isComputer()) {
            reload(true);
            return;
        }
        // the folder shown was on a drive that's gone (pulled out, ejected)
        if (!realDir().isEmpty() && !QFileInfo::exists(realDir()))
            navigate(QStringLiteral("computer:"), false);
    });
}

// ---------------------------------------------------------------------------
// QAbstractListModel

int DirectoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case NameRole:         return e.name;
    case PathRole:         return e.path;
    case IsDirRole:        return e.isDir;
    case SizeTextRole:     return !e.sizeText.isEmpty() ? e.sizeText : (e.isDir ? QString() : formatSize(e.size));
    case TypeTextRole:     return e.typeText;
    case ModifiedTextRole: return e.modified.isValid() ? QLocale::system().toString(e.modified, QLocale::ShortFormat) : QString();
    case IconKindRole:     return e.iconKind;
    case HiddenRole:       return e.hidden;
    case SymlinkRole:      return e.symlink;
    case SelectedRole:     return e.selected;
    default:               return {};
    }
}

QHash<int, QByteArray> DirectoryModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "filePath"},
        {IsDirRole, "isDir"},
        {SizeTextRole, "sizeText"},
        {TypeTextRole, "typeText"},
        {ModifiedTextRole, "modifiedText"},
        {IconKindRole, "iconKind"},
        {HiddenRole, "hidden"},
        {SymlinkRole, "symlink"},
        {SelectedRole, "selected"},
    };
}

// ---------------------------------------------------------------------------
// location

QString DirectoryModel::trashFilesDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files");
}

QString DirectoryModel::trashInfoDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/info");
}

QString DirectoryModel::realDir() const
{
    if (isComputer())
        return {};
    if (isTrash())
        return trashFilesDir();
    return m_path;
}

QString DirectoryModel::displayPath() const
{
    if (isComputer())
        return QStringLiteral("My Computer");
    if (isTrash())
        return QStringLiteral("Recycle Bin");
    return m_path;
}

QString DirectoryModel::title() const
{
    if (isComputer() || isTrash())
        return displayPath();
    if (m_path == QLatin1String("/"))
        return QStringLiteral("File System");
    return QFileInfo(m_path).fileName();
}

bool DirectoryModel::canGoUp() const
{
    return !isComputer() && !m_path.isEmpty();
}

bool DirectoryModel::writable() const
{
    if (isComputer() || isTrash() || m_path.isEmpty())
        return false;
    return QFileInfo(m_path).isWritable();
}

void DirectoryModel::setPath(const QString &path)
{
    navigate(path, true);
}

void DirectoryModel::navigate(const QString &requested, bool recordHistory)
{
    QString target = requested.trimmed();
    if (target.compare(QLatin1String("My Computer"), Qt::CaseInsensitive) == 0)
        target = QStringLiteral("computer:");
    else if (target.compare(QLatin1String("Recycle Bin"), Qt::CaseInsensitive) == 0)
        target = QStringLiteral("trash:");

    if (target != QLatin1String("computer:") && target != QLatin1String("trash:")) {
        if (target.startsWith(QLatin1String("file://")))
            target = QUrl(target).toLocalFile();
        if (target == QLatin1String("~") || target.startsWith(QLatin1String("~/")))
            target = QDir::homePath() + target.mid(1);
        if (QDir::isRelativePath(target) && !m_path.isEmpty() && !isComputer() && !isTrash())
            target = QDir(m_path).filePath(target);
        target = QDir::cleanPath(QDir(target).absolutePath());

        const QFileInfo info(target);
        if (!info.exists()) {
            emit error(QStringLiteral("Folder Not Found"),
                       QStringLiteral("Cannot find '%1'. Make sure the path is correct.").arg(requested));
            return;
        }
        if (!info.isDir()) {
            // typed a file name: open it instead
            QDesktopServices::openUrl(QUrl::fromLocalFile(target));
            return;
        }
        if (!info.isReadable() || !info.isExecutable()) {
            emit error(QStringLiteral("Access Denied"),
                       QStringLiteral("'%1' is not accessible.\n\nAccess is denied.").arg(target));
            return;
        }
    } else if (target == QLatin1String("trash:")) {
        QDir().mkpath(trashFilesDir());
        QDir().mkpath(trashInfoDir());
    }

    if (target == m_path) {
        refresh();
        return;
    }

    if (recordHistory && !m_path.isEmpty()) {
        m_back.append(m_path);
        m_forward.clear();
        emit historyChanged();
    }

    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_path = target;
    if (const QString dir = realDir(); !dir.isEmpty())
        m_watcher.addPath(dir);

    m_anchorRow = -1;
    emit pathChanged();
    reload(false);
}

void DirectoryModel::goUp()
{
    if (!canGoUp())
        return;
    if (isTrash() || m_path == QLatin1String("/")) {
        navigate(QStringLiteral("computer:"), true);
        return;
    }
    m_pendingFocusName = QFileInfo(m_path).fileName();
    navigate(QFileInfo(m_path).absolutePath(), true);
}

void DirectoryModel::goBack()
{
    if (m_back.isEmpty())
        return;
    m_forward.prepend(m_path);
    const QString target = m_back.takeLast();
    emit historyChanged();
    navigate(target, false);
}

void DirectoryModel::goForward()
{
    if (m_forward.isEmpty())
        return;
    m_back.append(m_path);
    const QString target = m_forward.takeFirst();
    emit historyChanged();
    navigate(target, false);
}

void DirectoryModel::goHome()
{
    navigate(QDir::homePath(), true);
}

void DirectoryModel::refresh()
{
    reload(true);
}

// ---------------------------------------------------------------------------
// loading

void DirectoryModel::reload(bool keepSelection)
{
    QSet<QString> selectedNames;
    if (keepSelection)
        for (const Entry &e : std::as_const(m_entries))
            if (e.selected)
                selectedNames.insert(e.name);

    beginResetModel();
    m_entries.clear();
    if (isComputer())
        loadComputer();
    else if (!m_path.isEmpty())
        loadDirectory(realDir());
    sortEntries();

    int focusRow = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        Entry &e = m_entries[i];
        e.selected = selectedNames.contains(e.name);
        if (!m_pendingFocusName.isEmpty() && e.name == m_pendingFocusName) {
            e.selected = true;
            focusRow = i;
        }
    }
    m_pendingFocusName.clear();
    if (m_anchorRow >= m_entries.size())
        m_anchorRow = -1;
    endResetModel();

    emit countChanged();
    emit selectionChanged();
    if (focusRow >= 0) {
        m_anchorRow = focusRow;
        emit rowFocusRequested(focusRow);
    }
}

void DirectoryModel::loadComputer()
{
    Entry home;
    home.name = QStringLiteral("Home");
    home.path = QDir::homePath();
    home.isDir = true;
    home.typeText = QStringLiteral("Home Folder");
    home.iconKind = QStringLiteral("home");
    m_entries.append(home);

    // plug-in drives come from Drives (mounted or not), the rest from the mounts
    const QVariantList drives = Drives::instance()->drives();
    QSet<QString> driveDevices;
    for (const QVariant &v : drives)
        driveDevices << v.toMap().value(QStringLiteral("device")).toString();

    const auto volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &vol : volumes) {
        if (!vol.isValid() || !vol.isReady())
            continue;
        if (driveDevices.contains(QString::fromLocal8Bit(vol.device())))
            continue;
        const QString root = vol.rootPath();
        if (!vol.device().startsWith("/dev/") || vol.device().startsWith("/dev/loop"))
            continue; // tmpfs, proc, snaps and other pseudo filesystems
        if (root.startsWith(QLatin1String("/boot")))
            continue;

        Entry e;
        e.path = root;
        e.isDir = true;
        e.iconKind = QStringLiteral("drive");
        if (root == QLatin1String("/"))
            e.name = QStringLiteral("File System");
        else if (!vol.name().isEmpty())
            e.name = vol.name();
        else
            e.name = QFileInfo(root).fileName();
        const bool removable = root.startsWith(QLatin1String("/media/")) || root.startsWith(QLatin1String("/run/media/"));
        e.typeText = removable ? QStringLiteral("Removable Disk") : QStringLiteral("Local Disk");
        e.size = vol.bytesTotal();
        e.sizeText = QStringLiteral("%1 free of %2").arg(formatSize(vol.bytesAvailable()), formatSize(vol.bytesTotal()));
        m_entries.append(e);
    }

    for (const QVariant &v : drives) {
        const QVariantMap d = v.toMap();
        Entry e;
        e.name = d.value(QStringLiteral("name")).toString();
        e.driveId = d.value(QStringLiteral("id")).toString();
        e.isDir = true;
        e.iconKind = d.value(QStringLiteral("removable")).toBool() ? QStringLiteral("removable") : QStringLiteral("drive");
        e.typeText = d.value(QStringLiteral("removable")).toBool() ? QStringLiteral("Removable Disk") : QStringLiteral("Local Disk");
        e.size = d.value(QStringLiteral("size")).toLongLong();
        if (d.value(QStringLiteral("mounted")).toBool()) {
            e.path = d.value(QStringLiteral("mountPath")).toString();
            const QStorageInfo vol(e.path);
            e.sizeText = QStringLiteral("%1 free of %2").arg(formatSize(vol.bytesAvailable()), formatSize(vol.bytesTotal()));
        } else {
            // opening it mounts it (openRow); until then it has no folder
            e.path = QStringLiteral("drive:") + e.driveId;
            e.sizeText = d.value(QStringLiteral("sizeText")).toString();
            if (d.value(QStringLiteral("encrypted")).toBool())
                e.typeText = QStringLiteral("Encrypted Disk");
        }
        m_entries.append(e);
    }
}

void DirectoryModel::openDrive(const QString &id)
{
    const QVariantMap d = Drives::instance()->drive(id);
    if (d.value(QStringLiteral("mounted")).toBool()) {
        navigate(d.value(QStringLiteral("mountPath")).toString(), true);
        return;
    }
    m_busy = true;
    emit busyChanged();
    const QString name = d.value(QStringLiteral("name"), QStringLiteral("The drive")).toString();
    Drives::instance()->mount(id, [this, name](bool ok, const QString &result) {
        m_busy = false;
        emit busyChanged();
        if (!ok) {
            emit error(name, result);
            return;
        }
        navigate(result, true);
    });
}

bool DirectoryModel::selectedIsDrive() const
{
    const int row = firstSelectedRow();
    if (!isComputer() || row < 0 || selectionCount() != 1)
        return false;
    const QString id = m_entries.at(row).driveId;
    return !id.isEmpty() && Drives::instance()->drive(id).value(QStringLiteral("canEject")).toBool();
}

void DirectoryModel::ejectSelected()
{
    if (selectedIsDrive())
        ejectDrive(m_entries.at(firstSelectedRow()).driveId);
}

void DirectoryModel::ejectDrive(const QString &id)
{
    const QVariantMap d = Drives::instance()->drive(id);
    const QString name = d.value(QStringLiteral("name"), QStringLiteral("The drive")).toString();
    // not looking into it while it's unmounted
    if (!realDir().isEmpty() && !Drives::instance()->driveAt(realDir()).isEmpty()
        && Drives::instance()->driveAt(realDir()).value(QStringLiteral("id")) == id)
        navigate(QStringLiteral("computer:"), true);
    m_busy = true;
    emit busyChanged();
    Drives::instance()->eject(id, [this, name](bool ok, const QString &err) {
        m_busy = false;
        emit busyChanged();
        if (!ok)
            emit error(QStringLiteral("Eject"), err);
        else
            emit notice(QStringLiteral("Eject"), QStringLiteral("%1 can now be safely removed.").arg(name));
    });
}

void DirectoryModel::loadDirectory(const QString &dir)
{
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
    if (m_showHidden)
        filters |= QDir::Hidden;
    const QFileInfoList infos = QDir(dir).entryInfoList(filters, QDir::NoSort);
    m_entries.reserve(infos.size());
    for (const QFileInfo &info : infos) {
        Entry e;
        e.name = info.fileName();
        e.path = info.absoluteFilePath();
        e.isDir = info.isDir();
        e.hidden = e.name.startsWith(QLatin1Char('.'));
        e.symlink = info.isSymLink();
        e.size = e.isDir ? -1 : info.size();
        e.modified = info.lastModified();
        describe(e);
        m_entries.append(e);
    }
}

void DirectoryModel::describe(Entry &e) const
{
    describeFile(e.path, e.isDir, &e.iconKind, &e.typeText);
}

void DirectoryModel::describeFile(const QString &path, bool isDir, QString *iconKind, QString *typeText)
{
    if (isDir) {
        *typeText = QStringLiteral("File Folder");
        *iconKind = QStringLiteral("folder");
        return;
    }
    static const QMimeDatabase db;
    const QFileInfo info(path);
    // extension-only matching: sniffing file contents would make big
    // folders slow to open
    const QMimeType mime = db.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
    *iconKind = iconKindForMime(mime, info);
    if (mime.isDefault() && info.isExecutable()) {
        *iconKind = QStringLiteral("exec");
        *typeText = QStringLiteral("Application");
    } else if (mime.isDefault()) {
        const QString suffix = info.suffix().toUpper();
        *typeText = suffix.isEmpty() ? QStringLiteral("File") : QStringLiteral("%1 File").arg(suffix);
    } else {
        QString comment = mime.comment();
        if (!comment.isEmpty())
            comment[0] = comment[0].toUpper();
        *typeText = comment;
    }
}

void DirectoryModel::sortEntries()
{
    if (isComputer())
        return; // keep Home first, then drives in mount order

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    const int column = m_sortColumn;
    const bool asc = m_sortAscending;
    std::stable_sort(m_entries.begin(), m_entries.end(), [&](const Entry &a, const Entry &b) {
        if (a.isDir != b.isDir)
            return a.isDir; // folders always first
        int c = 0;
        switch (column) {
        case SortSize:
            c = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            break;
        case SortType:
            c = collator.compare(a.typeText, b.typeText);
            break;
        case SortModified:
            c = a.modified < b.modified ? -1 : (a.modified > b.modified ? 1 : 0);
            break;
        default:
            break;
        }
        if (c == 0)
            c = collator.compare(a.name, b.name);
        return asc ? c < 0 : c > 0;
    });
}

void DirectoryModel::setShowHidden(bool show)
{
    if (show == m_showHidden)
        return;
    m_showHidden = show;
    emit showHiddenChanged();
    reload(true);
}

void DirectoryModel::setSortColumn(int column)
{
    if (column == m_sortColumn)
        return;
    m_sortColumn = column;
    emit sortChanged();
    reload(true);
}

void DirectoryModel::setSortAscending(bool ascending)
{
    if (ascending == m_sortAscending)
        return;
    m_sortAscending = ascending;
    emit sortChanged();
    reload(true);
}

// ---------------------------------------------------------------------------
// opening

void DirectoryModel::openRow(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const Entry e = m_entries.at(row);
    if (!e.driveId.isEmpty() && e.path.startsWith(QLatin1String("drive:"))) {
        openDrive(e.driveId);
        return;
    }
    if (e.isDir) {
        if (isTrash()) {
            emit error(QStringLiteral("Recycle Bin"),
                       QStringLiteral("Restore '%1' to open it.").arg(e.name));
            return;
        }
        navigate(e.path, true);
        return;
    }
    if (isTrash()) {
        emit error(QStringLiteral("Recycle Bin"),
                   QStringLiteral("Files in the Recycle Bin can't be opened. Restore '%1' first.").arg(e.name));
        return;
    }
    const QString problem = openFile(e.path);
    if (!problem.isEmpty())
        emit error(e.name, problem);
}

QString DirectoryModel::openFile(const QString &path)
{
    const QFileInfo info(path);
    const QString name = info.fileName();
    if (isElfExecutable(path)) {
        if (!QProcess::startDetached(path, {}, info.absolutePath()))
            return QStringLiteral("Cannot run '%1'.").arg(name);
        return QString();
    }
    QString kind, type;
    describeFile(path, false, &kind, &type);
    // our own programs: next to this one (a build tree) or on the PATH
    auto ours = [](const QString &program) {
        const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(program);
        return QFileInfo(beside).isExecutable() ? beside : QStandardPaths::findExecutable(program);
    };
    static const QStringList markdown{QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mdown"), QStringLiteral("mkd")};
    if (markdown.contains(info.suffix().toLower())) {
        // Markdown opens formatted, in the Markdown Editor
        const QString editor = ours(QStringLiteral("win95markdown"));
        if (!editor.isEmpty() && QProcess::startDetached(editor, {path}, info.absolutePath()))
            return QString();
    }
    if (kind == QLatin1String("text")) {
        // plain text opens in our Notepad, as in Win95
        const QString notepad = ours(QStringLiteral("win95notepad"));
        if (!notepad.isEmpty() && QProcess::startDetached(notepad, {path}, info.absolutePath()))
            return QString();
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        return QStringLiteral("There is no program associated with '%1'.").arg(name);
    return QString();
}

void DirectoryModel::openSelected()
{
    // a single folder navigates; otherwise open each selected file
    const int first = firstSelectedRow();
    if (first < 0)
        return;
    if (selectionCount() == 1) {
        openRow(first);
        return;
    }
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).selected && !m_entries.at(i).isDir)
            openRow(i);
}

void DirectoryModel::openTerminalHere()
{
    QString dir = realDir();
    if (dir.isEmpty() || isTrash())
        dir = QDir::homePath();
    QString program = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("win95terminal"));
    if (!QFileInfo(program).isExecutable())
        program = QStandardPaths::findExecutable(QStringLiteral("win95terminal"));
    if (program.isEmpty() || !QProcess::startDetached(program, {QStringLiteral("--workdir"), dir}, dir))
        emit error(QStringLiteral("Open Terminal"), QStringLiteral("Cannot find the terminal program (win95terminal)."));
}

// ---------------------------------------------------------------------------
// selection

int DirectoryModel::selectionCount() const
{
    return int(std::count_if(m_entries.cbegin(), m_entries.cend(), [](const Entry &e) { return e.selected; }));
}

bool DirectoryModel::isSelected(int row) const
{
    return row >= 0 && row < m_entries.size() && m_entries.at(row).selected;
}

int DirectoryModel::firstSelectedRow() const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).selected)
            return i;
    return -1;
}

void DirectoryModel::setAutoRefresh(bool on)
{
    if (on == m_autoRefresh)
        return;
    m_autoRefresh = on;
    emit autoRefreshChanged();
    if (on && m_refreshPending) {
        m_refreshPending = false;
        reload(true);
    }
}

int DirectoryModel::rowOfName(const QString &name) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).name == name)
            return i;
    return -1;
}

QString DirectoryModel::nameAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).name : QString();
}

QStringList DirectoryModel::selectedNames() const
{
    QStringList names;
    for (const Entry &e : m_entries)
        if (e.selected)
            names << e.name;
    return names;
}

QStringList DirectoryModel::selectedPaths() const
{
    QStringList paths;
    for (const Entry &e : m_entries)
        if (e.selected)
            paths << e.path;
    return paths;
}

int DirectoryModel::findNextByPrefix(const QString &prefix, int fromRow) const
{
    if (prefix.isEmpty() || m_entries.isEmpty())
        return -1;
    const int n = int(m_entries.size());
    for (int k = 1; k <= n; ++k) {
        const int i = (fromRow + k) % n;
        if (m_entries.at(i).name.startsWith(prefix, Qt::CaseInsensitive))
            return i;
    }
    return -1;
}

void DirectoryModel::emitAllRowsChanged(int role)
{
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(int(m_entries.size()) - 1), {role});
    emit selectionChanged();
}

void DirectoryModel::select(int row, int mode)
{
    const bool valid = row >= 0 && row < m_entries.size();
    switch (mode) {
    case Toggle:
        if (!valid)
            return;
        m_entries[row].selected = !m_entries[row].selected;
        m_anchorRow = row;
        emit dataChanged(index(row), index(row), {SelectedRole});
        emit selectionChanged();
        return;
    case Extend: {
        if (!valid)
            return;
        const int anchor = m_anchorRow >= 0 ? m_anchorRow : row;
        const int lo = qMin(anchor, row), hi = qMax(anchor, row);
        for (int i = 0; i < m_entries.size(); ++i)
            m_entries[i].selected = i >= lo && i <= hi;
        m_anchorRow = anchor;
        break;
    }
    default:
        for (Entry &e : m_entries)
            e.selected = false;
        if (valid)
            m_entries[row].selected = true;
        m_anchorRow = valid ? row : -1;
        break;
    }
    emitAllRowsChanged(SelectedRole);
}

void DirectoryModel::selectAll()
{
    for (Entry &e : m_entries)
        e.selected = true;
    emitAllRowsChanged(SelectedRole);
}

void DirectoryModel::clearSelection()
{
    select(-1, Replace);
}

// ---------------------------------------------------------------------------
// status bar

QString DirectoryModel::formatSize(qint64 bytes)
{
    if (bytes < 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 bytes").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1KB").arg((bytes + 1023) / 1024);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    return QStringLiteral("%1GB").arg(QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2));
}

QString DirectoryModel::statusText() const
{
    const int sel = selectionCount();
    if (m_busy)
        return QStringLiteral("Copying...");
    if (sel > 0)
        return QStringLiteral("%1 object(s) selected").arg(sel);
    return QStringLiteral("%1 object(s)").arg(m_entries.size());
}

QString DirectoryModel::sizeStatusText() const
{
    if (isComputer())
        return {};
    const bool anySelected = selectionCount() > 0;
    qint64 total = 0;
    for (const Entry &e : m_entries)
        if ((!anySelected || e.selected) && !e.isDir)
            total += e.size;
    QString text = formatSize(total);
    const QStorageInfo storage(realDir());
    if (storage.isValid())
        text += QStringLiteral(" (Disk free space: %1)").arg(formatSize(storage.bytesAvailable()));
    return text;
}

QString DirectoryModel::propertiesText() const
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        if (isComputer())
            return QStringLiteral("My Computer");
        const QFileInfo info(realDir());
        return QStringLiteral("%1\n\nLocation: %2\nContains: %3 object(s)")
            .arg(title(), info.absoluteFilePath()).arg(m_entries.size());
    }
    if (paths.size() > 1) {
        qint64 total = 0;
        int files = 0, folders = 0;
        for (const Entry &e : m_entries) {
            if (!e.selected)
                continue;
            if (e.isDir)
                ++folders;
            else {
                ++files;
                total += e.size;
            }
        }
        return QStringLiteral("%1 Files, %2 Folders\n\nSize of files: %3").arg(files).arg(folders).arg(formatSize(total));
    }
    const Entry &e = m_entries.at(firstSelectedRow());
    if (!e.driveId.isEmpty()) {
        const QVariantMap d = Drives::instance()->drive(e.driveId);
        QString text = QStringLiteral("%1\n\nType: %2\nDevice: %3\n").arg(e.name, e.typeText, d.value(QStringLiteral("device")).toString());
        if (!d.value(QStringLiteral("driveName")).toString().isEmpty())
            text += QStringLiteral("Drive: %1\n").arg(d.value(QStringLiteral("driveName")).toString());
        if (!d.value(QStringLiteral("fsType")).toString().isEmpty())
            text += QStringLiteral("File system: %1\n").arg(d.value(QStringLiteral("fsType")).toString());
        text += d.value(QStringLiteral("mounted")).toBool()
            ? QStringLiteral("Space: %1\nOpened at: %2").arg(e.sizeText, e.path)
            : QStringLiteral("Size: %1\nNot opened yet: double-click it to open it.").arg(e.sizeText);
        return text;
    }
    const QFileInfo info(e.path);
    QString text = QStringLiteral("%1\n\nType: %2\nLocation: %3\n").arg(e.name, e.typeText, info.absolutePath());
    if (e.symlink)
        text += QStringLiteral("Link to: %1\n").arg(info.symLinkTarget());
    if (!e.isDir)
        text += QStringLiteral("Size: %1 (%2 bytes)\n").arg(formatSize(e.size), QLocale::system().toString(e.size));
    else if (!e.sizeText.isEmpty())
        text += QStringLiteral("Space: %1\n").arg(e.sizeText);
    if (e.modified.isValid())
        text += QStringLiteral("Modified: %1\n").arg(QLocale::system().toString(e.modified, QLocale::LongFormat));
    text += QStringLiteral("Owner: %1\nAttributes: %2%3%4")
                .arg(info.owner(),
                     info.isReadable() ? QStringLiteral("Read") : QString(),
                     info.isWritable() ? QStringLiteral(", Write") : QStringLiteral(", Read-only"),
                     e.hidden ? QStringLiteral(", Hidden") : QString());
    return text;
}

// ---------------------------------------------------------------------------
// clipboard & transfers

void DirectoryModel::setClipboard(bool cut)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty() || isComputer())
        return;
    QList<QUrl> urls;
    QByteArray gnome = cut ? "cut" : "copy";
    for (const QString &p : paths) {
        const QUrl url = QUrl::fromLocalFile(p);
        urls << url;
        gnome += '\n' + url.toEncoded();
    }
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    // what GNOME/KDE file managers look at to tell cut from copy
    mime->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
    mime->setData(QStringLiteral("application/x-kde-cutselection"), cut ? "1" : "0");
    QGuiApplication::clipboard()->setMimeData(mime);
}

void DirectoryModel::copySelected()
{
    setClipboard(false);
}

void DirectoryModel::cutSelected()
{
    setClipboard(true);
}

void DirectoryModel::updateCanPaste()
{
    bool can = false;
    if (const QMimeData *mime = QGuiApplication::clipboard()->mimeData()) {
        const QList<QUrl> urls = mime->urls();
        can = std::any_of(urls.cbegin(), urls.cend(), [](const QUrl &u) { return u.isLocalFile(); });
    }
    if (can != m_canPaste) {
        m_canPaste = can;
        emit canPasteChanged();
    }
}

void DirectoryModel::paste()
{
    if (!writable() || m_busy)
        return;
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return;
    QStringList sources;
    for (const QUrl &u : mime->urls())
        if (u.isLocalFile())
            sources << u.toLocalFile();
    if (sources.isEmpty())
        return;
    const bool cut = mime->data(QStringLiteral("application/x-kde-cutselection")) == "1"
        || mime->data(QStringLiteral("x-special/gnome-copied-files")).startsWith("cut");
    startTransfer(sources, m_path, cut);
    if (cut)
        QGuiApplication::clipboard()->clear(); // a cut can only be pasted once
}

void DirectoryModel::startTransfer(const QStringList &sources, const QString &destDir, bool move)
{
    m_busy = true;
    emit busyChanged();
    emit selectionChanged(); // status text
    m_transfer.setFuture(QtConcurrent::run(transferFiles, sources, destDir, move));
}

// ---------------------------------------------------------------------------
// delete / rename / new folder / trash

void DirectoryModel::trashSelected()
{
    if (isTrash()) {
        deleteSelectedPermanently();
        return;
    }
    QStringList errors;
    for (const QString &p : selectedPaths())
        if (!QFile::moveToTrash(p))
            errors << QStringLiteral("Cannot move '%1' to the Recycle Bin.").arg(QFileInfo(p).fileName());
    reload(false);
    if (!errors.isEmpty())
        emit error(QStringLiteral("Error Deleting File or Folder"), errors.join(QLatin1Char('\n')));
}

void DirectoryModel::deleteSelectedPermanently()
{
    QStringList errors;
    for (const QString &p : selectedPaths()) {
        QString why;
        if (!removePath(p, &why)) {
            errors << QStringLiteral("Cannot delete '%1': %2").arg(QFileInfo(p).fileName(), why);
            continue;
        }
        if (isTrash())
            QFile::remove(trashInfoDir() + QLatin1Char('/') + QFileInfo(p).fileName() + QStringLiteral(".trashinfo"));
    }
    reload(false);
    if (!errors.isEmpty())
        emit error(QStringLiteral("Error Deleting File or Folder"), errors.join(QLatin1Char('\n')));
}

void DirectoryModel::restoreSelected()
{
    if (!isTrash())
        return;
    QStringList errors;
    for (const QString &p : selectedPaths()) {
        const QString name = QFileInfo(p).fileName();
        const QString infoPath = trashInfoDir() + QLatin1Char('/') + name + QStringLiteral(".trashinfo");
        QFile infoFile(infoPath);
        QString original;
        if (infoFile.open(QIODevice::ReadOnly)) {
            for (const QByteArray &line : infoFile.readAll().split('\n'))
                if (line.startsWith("Path="))
                    original = QUrl::fromPercentEncoding(line.mid(5));
            infoFile.close();
        }
        if (original.isEmpty()) {
            errors << QStringLiteral("Cannot restore '%1': its original location is unknown.").arg(name);
            continue;
        }
        if (QDir::isRelativePath(original)) // relative to the trash's top dir
            original = QDir::homePath() + QLatin1Char('/') + original;
        if (QFileInfo::exists(original)) {
            errors << QStringLiteral("Cannot restore '%1': '%2' already exists.").arg(name, original);
            continue;
        }
        QDir().mkpath(QFileInfo(original).absolutePath());
        std::error_code ec;
        fs::rename(toFsPath(p), toFsPath(original), ec);
        if (ec) {
            errors << QStringLiteral("Cannot restore '%1': %2").arg(name, QString::fromLocal8Bit(ec.message()));
            continue;
        }
        QFile::remove(infoPath);
    }
    reload(false);
    if (!errors.isEmpty())
        emit error(QStringLiteral("Recycle Bin"), errors.join(QLatin1Char('\n')));
}

void DirectoryModel::emptyTrash()
{
    for (const QString &dir : {trashFilesDir(), trashInfoDir()}) {
        const QFileInfoList all = QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo &info : all)
            removePath(info.absoluteFilePath(), nullptr);
    }
    if (isTrash())
        reload(false);
}

bool DirectoryModel::renameRow(int row, const QString &requestedName)
{
    if (row < 0 || row >= m_entries.size() || !writable())
        return false;
    const QString newName = requestedName.trimmed();
    const Entry e = m_entries.at(row);
    if (newName == e.name)
        return true;
    if (newName.isEmpty() || newName == QLatin1String(".") || newName == QLatin1String("..")
        || newName.contains(QLatin1Char('/'))) {
        emit error(QStringLiteral("Rename"), QStringLiteral("A filename cannot be empty or contain the character /."));
        return false;
    }
    QDir dir(m_path);
    if (dir.exists(newName)) {
        emit error(QStringLiteral("Error Renaming File or Folder"),
                   QStringLiteral("Cannot rename %1: A file with the name you specified already exists. "
                                  "Specify a different filename.").arg(e.name));
        return false;
    }
    if (!dir.rename(e.name, newName)) {
        emit error(QStringLiteral("Error Renaming File or Folder"),
                   QStringLiteral("Cannot rename %1: Access is denied.").arg(e.name));
        return false;
    }
    m_pendingFocusName = newName;
    reload(false);
    return true;
}

int DirectoryModel::newFolder()
{
    if (!writable())
        return -1;
    QDir dir(m_path);
    QString name = QStringLiteral("New Folder");
    for (int n = 2; dir.exists(name); ++n)
        name = QStringLiteral("New Folder (%1)").arg(n);
    if (!dir.mkdir(name)) {
        emit error(QStringLiteral("Error Creating Folder"), QStringLiteral("Unable to create the folder '%1'.").arg(name));
        return -1;
    }
    m_pendingFocusName = name;
    reload(false);
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).name == name)
            return i;
    return -1;
}
