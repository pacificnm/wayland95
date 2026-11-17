#include "trashwatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThreadPool>
#include <QStandardPaths>

TrashWatcher::TrashWatcher(QObject *parent)
    : QObject(parent)
    , m_dir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files"))
{
    // make sure there's something to watch even before the first delete
    QDir().mkpath(m_dir);
    m_watcher.addPath(m_dir);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &TrashWatcher::check);
    check();
}

void TrashWatcher::check()
{
    const bool full = !QDir(m_dir).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    if (full != m_full) {
        m_full = full;
        emit fullChanged();
    }
}

void TrashWatcher::empty()
{
    // the files, and the notes of where each came from (Trash/info)
    const QString files = m_dir;
    const QString info = QFileInfo(m_dir).absolutePath() + QStringLiteral("/info");
    QThreadPool::globalInstance()->start([files, info] {
        for (const QString &dir : { files, info }) {
            const QFileInfoList all = QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
            for (const QFileInfo &entry : all) {
                // (a link is removed itself, never what it points to)
                if (entry.isDir() && !entry.isSymLink())
                    QDir(entry.absoluteFilePath()).removeRecursively();
                else
                    QFile::remove(entry.absoluteFilePath());
            }
        }
    });
}
