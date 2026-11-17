#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

// Tells the desktop whether the Recycle Bin (the freedesktop.org trash,
// ~/.local/share/Trash/files) has anything in it, so its icon can switch
// between empty and full. Exposed to QML as the singleton `TrashWatcher`.
class TrashWatcher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool full READ full NOTIFY fullChanged)

public:
    explicit TrashWatcher(QObject *parent = nullptr);

    bool full() const { return m_full; }
    // Empty Recycle Bin: everything in it is deleted for good (off the
    // desktop's thread: there can be a lot)
    Q_INVOKABLE void empty();

signals:
    void fullChanged();

private:
    void check();

    QString m_dir;
    QFileSystemWatcher m_watcher;
    bool m_full = false;
};
