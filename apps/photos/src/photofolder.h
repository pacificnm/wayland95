#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

// The gallery: one folder's subfolders (first) and photos, as a list model.
// The folder is read in the background (the photos' EXIF dates too, for
// sorting by when they were taken), and read again when it changes.
//
//   PhotoFolder { folder: "/home/me/Pictures"; sortBy: "date"; descending: true }
class PhotoFolder : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY sortChanged) // "date" | "name"
    Q_PROPERTY(bool descending READ descending WRITE setDescending NOTIFY sortChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int photoCount READ photoCount NOTIFY countChanged)
    Q_PROPERTY(QString error READ error NOTIFY loadingChanged)

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        NameRole,
        IsDirRole,
        SizeRole,
        DateRole,      // taken (EXIF), else modified
        DateTextRole,
        StampRole,     // the file's modification time, ms (thumbnails' freshness)
    };

    struct Entry
    {
        QString path;
        QString name;
        bool isDir = false;
        qint64 size = 0;
        QDateTime modified;
        QDateTime date;
    };

    explicit PhotoFolder(QObject *parent = nullptr);
    ~PhotoFolder() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    QString sortBy() const { return m_sortBy; }
    void setSortBy(const QString &by);
    bool descending() const { return m_descending; }
    void setDescending(bool on);
    bool loading() const { return m_loading; }
    int count() const { return int(m_entries.size()); }
    int photoCount() const { return int(m_entries.size()) - m_dirCount; }
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE bool isDirAt(int row) const;
    Q_INVOKABLE int rowOf(const QString &path) const;
    // photos only, in the gallery's order: number `i`, which one `path` is
    Q_INVOKABLE QString photoAt(int i) const;
    Q_INVOKABLE int photoIndexOf(const QString &path) const;
    // the photo `step` away from `path` (wrapping round), "" if none
    Q_INVOKABLE QString stepFrom(const QString &path, int step) const;
    // takes a photo out of the list straight away (it was deleted)
    Q_INVOKABLE void forget(const QString &path);

signals:
    void folderChanged();
    void sortChanged();
    void loadingChanged();
    void countChanged();
    // the list was read again (rows may have moved)
    void reloaded();

private:
    void scan();
    void sortEntries(QList<Entry> &list) const;
    void resort();

    QString m_folder;
    QString m_sortBy = QStringLiteral("date");
    bool m_descending = true;
    bool m_loading = false;
    QString m_error;
    QList<Entry> m_entries;
    int m_dirCount = 0;
    int m_generation = 0;
    QFutureWatcher<QList<Entry>> *m_scan = nullptr;
    QFileSystemWatcher m_watcher;
    QTimer m_rescan;
};
