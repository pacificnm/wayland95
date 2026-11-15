#include "trashwatcher.h"

#include <QDir>
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
