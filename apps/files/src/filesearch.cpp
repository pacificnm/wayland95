#include "filesearch.h"

#include "directorymodel.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cstdio>

namespace {

struct Criteria {
    QList<QRegularExpression> names; // none: any name
    QString lookIn;
    bool subfolders = true;
    int days = 0;
    QByteArray containing; // empty: anything
    bool caseSensitive = false;
    bool hidden = false;
};

// "*.txt; report" -> a pattern each; one without wildcards matches anywhere
QList<QRegularExpression> namePatterns(const QString &named, bool caseSensitive)
{
    QList<QRegularExpression> out;
    for (QString p : named.split(QRegularExpression(QStringLiteral("[;,]")), Qt::SkipEmptyParts)) {
        p = p.trimmed();
        if (p.isEmpty())
            continue;
        if (!p.contains(QLatin1Char('*')) && !p.contains(QLatin1Char('?')))
            p = QLatin1Char('*') + p + QLatin1Char('*');
        QRegularExpression re(QRegularExpression::wildcardToRegularExpression(p, QRegularExpression::NonPathWildcardConversion),
                              caseSensitive ? QRegularExpression::NoPatternOption
                                            : QRegularExpression::CaseInsensitiveOption);
        out << re;
    }
    return out;
}

bool nameMatches(const Criteria &c, const QString &name)
{
    if (c.names.isEmpty())
        return true;
    for (const QRegularExpression &re : c.names)
        if (re.match(name).hasMatch())
            return true;
    return false;
}

// the text inside a plain file: read in pieces (overlapping by the text's
// length so a match across two pieces is found); binary files (a NUL in
// the first 8 KB) and huge ones are passed over
bool containsText(const QString &path, const QByteArray &needle, bool caseSensitive,
                  const std::atomic<bool> &cancel)
{
    QFile f(path);
    if (f.size() > 64LL * 1024 * 1024 || !f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray want = caseSensitive ? needle : needle.toLower();
    QByteArray carry;
    bool first = true;
    while (!f.atEnd()) {
        if (cancel.load())
            return false;
        QByteArray chunk = f.read(256 * 1024);
        if (first && chunk.left(8192).contains('\0'))
            return false;
        first = false;
        QByteArray hay = carry + chunk;
        if (!caseSensitive)
            hay = hay.toLower();
        if (hay.contains(want))
            return true;
        carry = chunk.right(want.size() - 1);
    }
    return false;
}

const QStringList skippedRoots = {
    QStringLiteral("/proc"), QStringLiteral("/sys"), QStringLiteral("/dev"), QStringLiteral("/run"),
};

} // namespace

FileSearch::FileSearch(QObject *parent)
    : QAbstractListModel(parent)
{
}

FileSearch::~FileSearch()
{
    if (m_cancel)
        m_cancel->store(true);
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

int FileSearch::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_results.size());
}

QVariant FileSearch::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_results.size())
        return QVariant();
    const Result &r = m_results.at(index.row());
    switch (role) {
    case NameRole:         return r.name;
    case FolderRole:       return r.folder;
    case PathRole:         return r.path;
    case IconKindRole:     return r.iconKind;
    case TypeTextRole:     return r.typeText;
    case SizeTextRole:     return r.isDir ? QString() : DirectoryModel::formatSize(r.size);
    case ModifiedTextRole: return QLocale().toString(r.modified, QLocale::ShortFormat);
    case IsDirRole:        return r.isDir;
    case SelectedRole:     return r.selected;
    }
    return QVariant();
}

QHash<int, QByteArray> FileSearch::roleNames() const
{
    return {
        {NameRole, "name"},
        {FolderRole, "folder"},
        {PathRole, "path"},
        {IconKindRole, "iconKind"},
        {TypeTextRole, "typeText"},
        {SizeTextRole, "sizeText"},
        {ModifiedTextRole, "modifiedText"},
        {IsDirRole, "isDir"},
        {SelectedRole, "selected"},
    };
}

QString FileSearch::expandPath(const QString &path) const
{
    QString p = path.trimmed();
    if (p == QLatin1String("~"))
        return QDir::homePath();
    if (p.startsWith(QLatin1String("~/")))
        return QDir::homePath() + p.mid(1);
    return p;
}

void FileSearch::start(const QVariantMap &criteria)
{
    stop();
    clear();

    auto c = std::make_shared<Criteria>();
    c->caseSensitive = criteria.value(QStringLiteral("caseSensitive")).toBool();
    c->names = namePatterns(criteria.value(QStringLiteral("named")).toString(), c->caseSensitive);
    c->lookIn = QDir::cleanPath(expandPath(criteria.value(QStringLiteral("lookIn")).toString()));
    if (c->lookIn.isEmpty() || c->lookIn == QLatin1String("."))
        c->lookIn = QDir::homePath();
    c->subfolders = criteria.value(QStringLiteral("subfolders"), true).toBool();
    c->days = criteria.value(QStringLiteral("days")).toInt();
    c->containing = criteria.value(QStringLiteral("containing")).toString().toUtf8();
    c->hidden = criteria.value(QStringLiteral("hidden")).toBool();

    const quint64 generation = ++m_generation;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_cancel = cancel;
    m_truncated = false;
    m_running = true;
    emit runningChanged();

    const QDateTime since = c->days > 0 ? QDateTime::currentDateTime().addDays(-c->days) : QDateTime();
    m_thread = QThread::create([this, c, cancel, generation, since] {
        QList<Result> batch;
        QElapsedTimer sent;
        sent.start();
        int found = 0;
        bool truncated = false;
        auto flush = [&] {
            if (batch.isEmpty())
                return;
            QMetaObject::invokeMethod(this, [this, generation, batch] { addBatch(generation, batch); }, Qt::QueuedConnection);
            batch.clear();
            sent.restart();
        };

        QStringList todo{c->lookIn};
        while (!todo.isEmpty() && !cancel->load()) {
            const QString folder = todo.takeLast();
            if (sent.elapsed() > 150) {
                QMetaObject::invokeMethod(this, [this, generation, folder] { noteFolder(generation, folder); }, Qt::QueuedConnection);
                flush();
            }
            QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
            if (c->hidden)
                filters |= QDir::Hidden;
            const QFileInfoList entries = QDir(folder).entryInfoList(filters, QDir::Name | QDir::DirsFirst);
            QStringList subfolders;
            for (const QFileInfo &info : entries) {
                if (cancel->load())
                    break;
                const QString name = info.fileName();
                const bool isDir = info.isDir();
                if (isDir && c->subfolders && !info.isSymLink() && !skippedRoots.contains(info.absoluteFilePath()))
                    subfolders.prepend(info.absoluteFilePath()); // (taken from the end: in name order)
                if (!nameMatches(*c, name))
                    continue;
                if (since.isValid() && info.lastModified() < since)
                    continue;
                if (!c->containing.isEmpty()
                        && (isDir || !info.isFile() || !containsText(info.absoluteFilePath(), c->containing, c->caseSensitive, *cancel)))
                    continue;
                Result r;
                r.name = name;
                r.path = info.absoluteFilePath();
                r.folder = info.absolutePath();
                r.isDir = isDir;
                r.size = isDir ? -1 : info.size();
                r.modified = info.lastModified();
                DirectoryModel::describeFile(r.path, isDir, &r.iconKind, &r.typeText);
                batch << r;
                if (++found >= MaxResults) {
                    truncated = true;
                    cancel->store(true);
                    break;
                }
                if (batch.size() >= 200 || sent.elapsed() > 150)
                    flush();
            }
            todo << subfolders;
        }
        flush();
        QMetaObject::invokeMethod(this, [this, generation, truncated] { done(generation, truncated); }, Qt::QueuedConnection);
    });
    m_thread->start();
}

void FileSearch::stop()
{
    if (m_cancel)
        m_cancel->store(true);
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    // (the worker's last messages are for an old search now: dropped)
    ++m_generation;
    if (m_running) {
        m_running = false;
        m_searchingIn.clear();
        emit searchingInChanged();
        emit runningChanged();
        emit finished(count());
    }
}

void FileSearch::clear()
{
    beginResetModel();
    m_results.clear();
    m_anchor = -1;
    endResetModel();
    emit countChanged();
}

void FileSearch::addBatch(quint64 generation, const QList<Result> &batch)
{
    if (generation != m_generation)
        return;
    if (m_sortColumn >= 0) {
        beginResetModel();
        m_results << batch;
        sortResults();
        endResetModel();
    } else {
        beginInsertRows(QModelIndex(), int(m_results.size()), int(m_results.size() + batch.size() - 1));
        m_results << batch;
        endInsertRows();
    }
    emit countChanged();
}

void FileSearch::noteFolder(quint64 generation, const QString &folder)
{
    if (generation != m_generation || folder == m_searchingIn)
        return;
    m_searchingIn = folder;
    emit searchingInChanged();
}

void FileSearch::done(quint64 generation, bool truncated)
{
    if (generation != m_generation)
        return;
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    m_truncated = truncated;
    m_running = false;
    m_searchingIn.clear();
    fprintf(stderr, "Find: %d found%s\n", count(), truncated ? " (stopped at the limit)" : "");
    if (qEnvironmentVariableIsSet("W95_FIND_DEBUG"))
        for (const Result &r : std::as_const(m_results))
            fprintf(stderr, "Find: found %s\n", qPrintable(r.path));
    emit searchingInChanged();
    emit runningChanged();
    emit finished(count());
}

bool FileSearch::lessThan(const Result &a, const Result &b) const
{
    static const QCollator collator = [] {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    int cmp = 0;
    switch (m_sortColumn) {
    case SortFolder:   cmp = collator.compare(a.folder, b.folder); break;
    case SortSize:     cmp = a.size < b.size ? -1 : a.size > b.size ? 1 : 0; break;
    case SortType:     cmp = collator.compare(a.typeText, b.typeText); break;
    case SortModified: cmp = a.modified < b.modified ? -1 : a.modified > b.modified ? 1 : 0; break;
    default:           break;
    }
    if (cmp == 0)
        cmp = collator.compare(a.name, b.name);
    if (cmp == 0)
        cmp = collator.compare(a.folder, b.folder);
    return m_sortAscending ? cmp < 0 : cmp > 0;
}

void FileSearch::sortResults()
{
    std::stable_sort(m_results.begin(), m_results.end(), [this](const Result &a, const Result &b) { return lessThan(a, b); });
}

void FileSearch::sortBy(int column)
{
    if (column == m_sortColumn)
        m_sortAscending = !m_sortAscending;
    else {
        m_sortColumn = column;
        m_sortAscending = true;
    }
    beginResetModel();
    sortResults();
    m_anchor = -1;
    endResetModel();
    emit sortChanged();
}

QString FileSearch::pathAt(int row) const
{
    return row >= 0 && row < m_results.size() ? m_results.at(row).path : QString();
}

bool FileSearch::isDirAt(int row) const
{
    return row >= 0 && row < m_results.size() && m_results.at(row).isDir;
}

void FileSearch::select(int row, int mode)
{
    if (row < 0 || row >= m_results.size())
        return;
    if (mode == 1) {
        m_results[row].selected = !m_results[row].selected;
        m_anchor = row;
    } else {
        const int from = (mode == 2 && m_anchor >= 0) ? std::min(m_anchor, row) : row;
        const int to = (mode == 2 && m_anchor >= 0) ? std::max(m_anchor, row) : row;
        for (int i = 0; i < m_results.size(); ++i)
            m_results[i].selected = i >= from && i <= to;
        if (mode != 2)
            m_anchor = row;
    }
    emit dataChanged(index(0), index(int(m_results.size()) - 1), {SelectedRole});
}

void FileSearch::selectAll()
{
    for (Result &r : m_results)
        r.selected = true;
    if (!m_results.isEmpty())
        emit dataChanged(index(0), index(int(m_results.size()) - 1), {SelectedRole});
}

QStringList FileSearch::selectedPaths() const
{
    QStringList out;
    for (const Result &r : m_results)
        if (r.selected)
            out << r.path;
    return out;
}

static bool startFiles(const QStringList &args)
{
    // our own file manager, next to this one (or on the PATH)
    QString files = QCoreApplication::applicationFilePath();
    return QProcess::startDetached(files, args);
}

QString FileSearch::open(int row)
{
    if (row < 0 || row >= m_results.size())
        return QString();
    const Result &r = m_results.at(row);
    if (!QFileInfo::exists(r.path))
        return QStringLiteral("'%1' isn't there any more.").arg(r.name);
    if (r.isDir)
        return startFiles({r.path}) ? QString() : QStringLiteral("Cannot open '%1'.").arg(r.name);
    return DirectoryModel::openFile(r.path);
}

QString FileSearch::openContainingFolder(int row)
{
    if (row < 0 || row >= m_results.size())
        return QString();
    const Result &r = m_results.at(row);
    if (!QFileInfo::exists(r.folder))
        return QStringLiteral("'%1' isn't there any more.").arg(r.folder);
    return startFiles({QStringLiteral("--select"), r.name, r.folder}) ? QString()
                                                                       : QStringLiteral("Cannot open '%1'.").arg(r.folder);
}

QString FileSearch::properties(int row) const
{
    if (row < 0 || row >= m_results.size())
        return QString();
    const Result &r = m_results.at(row);
    QString text = QStringLiteral("%1\n\nType: %2\nLocation: %3\n").arg(r.name, r.typeText, r.folder);
    if (!r.isDir)
        text += QStringLiteral("Size: %1 (%2 bytes)\n").arg(DirectoryModel::formatSize(r.size), QLocale().toString(r.size));
    text += QStringLiteral("Modified: %1").arg(QLocale().toString(r.modified, QLocale::LongFormat));
    return text;
}
