#include "photofolder.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QtConcurrent/QtConcurrentRun>

#include "exif.h"
#include "photofiles.h"

namespace {

QList<PhotoFolder::Entry> readFolder(const QString &folder)
{
    QList<PhotoFolder::Entry> list;
    const QDir dir(folder);
    const QFileInfoList infos = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);
    for (const QFileInfo &info : infos) {
        PhotoFolder::Entry e;
        e.path = info.absoluteFilePath();
        e.name = info.fileName();
        e.isDir = info.isDir();
        if (!e.isDir && !PhotoFiles::isPhoto(e.path))
            continue;
        e.size = e.isDir ? 0 : info.size();
        e.modified = info.lastModified();
        e.date = e.modified;
        if (!e.isDir) {
            // when it was taken, if the camera said
            const ExifInfo exif = Exif::read(e.path);
            if (exif.taken.isValid())
                e.date = exif.taken;
        }
        list.append(e);
    }
    return list;
}

} // namespace

PhotoFolder::PhotoFolder(QObject *parent)
    : QAbstractListModel(parent)
{
    // a burst of changes (a copy of many photos) reads the folder once
    m_rescan.setSingleShot(true);
    m_rescan.setInterval(400);
    connect(&m_rescan, &QTimer::timeout, this, &PhotoFolder::scan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_rescan.start(); });
}

PhotoFolder::~PhotoFolder()
{
    if (m_scan)
        m_scan->waitForFinished();
}

int PhotoFolder::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant PhotoFolder::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return QVariant();
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case PathRole: return e.path;
    case NameRole:
    case Qt::DisplayRole: return e.name;
    case IsDirRole: return e.isDir;
    case SizeRole: return e.size;
    case DateRole: return e.date;
    case DateTextRole: return QLocale().toString(e.date, QLocale::ShortFormat);
    case StampRole: return e.modified.toMSecsSinceEpoch();
    }
    return QVariant();
}

QHash<int, QByteArray> PhotoFolder::roleNames() const
{
    return {
        {PathRole, "path"},
        {NameRole, "name"},
        {IsDirRole, "isDir"},
        {SizeRole, "size"},
        {DateRole, "date"},
        {DateTextRole, "dateText"},
        {StampRole, "stamp"},
    };
}

void PhotoFolder::setFolder(const QString &folder)
{
    const QString path = folder.isEmpty() ? QString() : QDir::cleanPath(QDir(folder).absolutePath());
    if (path == m_folder)
        return;
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_folder = path;
    emit folderChanged();
    beginResetModel();
    m_entries.clear();
    m_dirCount = 0;
    endResetModel();
    emit countChanged();
    if (!m_folder.isEmpty())
        m_watcher.addPath(m_folder);
    scan();
}

void PhotoFolder::setSortBy(const QString &by)
{
    if (by == m_sortBy || (by != QLatin1String("date") && by != QLatin1String("name")))
        return;
    m_sortBy = by;
    emit sortChanged();
    resort();
}

void PhotoFolder::setDescending(bool on)
{
    if (on == m_descending)
        return;
    m_descending = on;
    emit sortChanged();
    resort();
}

void PhotoFolder::refresh()
{
    scan();
}

void PhotoFolder::scan()
{
    const int generation = ++m_generation;
    if (m_folder.isEmpty())
        return;
    if (!QFileInfo(m_folder).isDir()) {
        m_error = QStringLiteral("'%1' isn't there.").arg(m_folder);
        m_loading = false;
        emit loadingChanged();
        return;
    }
    m_error.clear();
    m_loading = true;
    emit loadingChanged();

    auto *watcher = new QFutureWatcher<QList<Entry>>(this);
    connect(watcher, &QFutureWatcher<QList<Entry>>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (m_scan == watcher)
            m_scan = nullptr;
        if (generation != m_generation)
            return; // another folder (or another read) since
        QList<Entry> list = watcher->result();
        sortEntries(list);
        beginResetModel();
        m_entries = list;
        m_dirCount = int(std::count_if(m_entries.cbegin(), m_entries.cend(), [](const Entry &e) { return e.isDir; }));
        endResetModel();
        m_loading = false;
        emit loadingChanged();
        emit countChanged();
        emit reloaded();
    });
    m_scan = watcher;
    watcher->setFuture(QtConcurrent::run(readFolder, m_folder));
}

void PhotoFolder::sortEntries(QList<Entry> &list) const
{
    QCollator collator;
    collator.setNumericMode(true); // IMG_9 before IMG_10
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const bool byDate = m_sortBy == QLatin1String("date");
    const bool desc = m_descending;
    std::stable_sort(list.begin(), list.end(), [&](const Entry &a, const Entry &b) {
        // folders first, always by name
        if (a.isDir != b.isDir)
            return a.isDir;
        if (a.isDir)
            return collator.compare(a.name, b.name) < 0;
        int c = 0;
        if (byDate)
            c = a.date < b.date ? -1 : (b.date < a.date ? 1 : 0);
        if (c == 0)
            c = collator.compare(a.name, b.name);
        return desc ? c > 0 : c < 0;
    });
}

void PhotoFolder::resort()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    sortEntries(m_entries);
    endResetModel();
    emit reloaded();
}

QString PhotoFolder::pathAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).path : QString();
}

bool PhotoFolder::isDirAt(int row) const
{
    return row >= 0 && row < m_entries.size() && m_entries.at(row).isDir;
}

int PhotoFolder::rowOf(const QString &path) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).path == path)
            return i;
    return -1;
}

QString PhotoFolder::photoAt(int i) const
{
    return pathAt(m_dirCount + i);
}

int PhotoFolder::photoIndexOf(const QString &path) const
{
    const int row = rowOf(path);
    return row < m_dirCount ? -1 : row - m_dirCount;
}

QString PhotoFolder::stepFrom(const QString &path, int step) const
{
    const int n = photoCount();
    if (n == 0)
        return QString();
    const int i = photoIndexOf(path);
    if (i < 0)
        return photoAt(step >= 0 ? 0 : n - 1);
    return photoAt(((i + step) % n + n) % n);
}

void PhotoFolder::forget(const QString &path)
{
    const int row = rowOf(path);
    if (row < 0)
        return;
    beginRemoveRows(QModelIndex(), row, row);
    if (m_entries.at(row).isDir)
        --m_dirCount;
    m_entries.removeAt(row);
    endRemoveRows();
    emit countChanged();
}
