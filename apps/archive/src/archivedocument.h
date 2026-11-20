#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QMutex>
#include <QTemporaryDir>
#include <QWaitCondition>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>

#include "archiveengine.h"

// The archive Archive Manager has open, as the list model its window
// shows (one row per file, with folders that hold nothing), plus every
// job done to it: open, extract, view, test, add and delete. Jobs run on a
// worker thread (ArchiveEngine); while one runs `busy` is set and progress
// comes through `percent` and `currentFile`.
//
// A job that finds a file already there asks through overwriteAsked() and
// waits for answerOverwrite(); one that needs a password stops with
// passwordNeeded(), and retry(password) runs it again with one.
class ArchiveDocument : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString format READ format NOTIFY changed)
    Q_PROPERTY(QString formatName READ formatName NOTIFY changed)
    Q_PROPERTY(QString tool READ tool NOTIFY changed)
    Q_PROPERTY(bool writable READ writable NOTIFY changed)
    Q_PROPERTY(QString readOnlyReason READ readOnlyReason NOTIFY changed)
    Q_PROPERTY(bool encrypted READ encrypted NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY changed)
    Q_PROPERTY(QString totalText READ totalText NOTIFY changed)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionText READ selectionText NOTIFY selectionChanged)
    Q_PROPERTY(int sortColumn READ sortColumn WRITE setSortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString operation READ operation NOTIFY busyChanged)
    Q_PROPERTY(int percent READ percent NOTIFY progressChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY progressChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        FolderRole,
        ArchivePathRole,
        IsDirRole,
        ModifiedRole,
        SizeRole,
        PackedRole,
        RatioRole,
        EncryptedRole,
        SelectedRole,
        KindRole,
    };
    enum SortColumn { SortName, SortModified, SortSize, SortRatio, SortPacked, SortPath };
    Q_ENUM(SortColumn)
    enum SelectMode { Replace, Toggle, Extend };
    Q_ENUM(SelectMode)

    explicit ArchiveDocument(QObject *parent = nullptr);
    ~ArchiveDocument() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_path; }
    QString fileName() const;
    bool loaded() const { return !m_path.isEmpty(); }
    QString format() const { return m_format; }
    QString formatName() const { return m_formatName; }
    QString tool() const { return m_tool; }
    bool writable() const;
    QString readOnlyReason() const;
    bool encrypted() const { return m_encrypted; }
    int count() const { return int(m_rows.size()); }
    int fileCount() const;
    QString totalText() const;
    int selectionCount() const;
    QString selectionText() const;
    int sortColumn() const { return m_sortColumn; }
    void setSortColumn(int c);
    bool sortAscending() const { return m_sortAscending; }
    void setSortAscending(bool a);
    bool busy() const { return m_busy; }
    QString operation() const { return m_operation; }
    int percent() const { return m_percent; }
    QString currentFile() const { return m_currentFile; }

    // opening
    Q_INVOKABLE void open(const QString &path);
    // a new, empty archive (written once files are added to it)
    Q_INVOKABLE void create(const QString &path, const QString &format);
    Q_INVOKABLE void close();
    Q_INVOKABLE void reload();

    // selection
    Q_INVOKABLE void select(int row, int mode);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE int firstSelectedRow() const;
    Q_INVOKABLE QStringList selectedPaths() const;
    Q_INVOKABLE QString pathAt(int row) const;

    // jobs (one at a time)
    Q_INVOKABLE void extract(const QString &dest, bool selectedOnly, bool useFolders);
    Q_INVOKABLE void view(int row);
    Q_INVOKABLE void test();
    // files (absolute paths) into the archive; format/level/password for a
    // new one (an existing one keeps its format)
    Q_INVOKABLE void add(const QStringList &files, const QString &format, int level, const QString &password);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void answerOverwrite(int answer); // Overwrite: 0 Yes, 1 Yes to All, 2 No, 3 No to All, 4 Cancel
    // the job that stopped for a password, again with this one
    Q_INVOKABLE void retry(const QString &password);

signals:
    void changed();
    void selectionChanged();
    void sortChanged();
    void busyChanged();
    void progressChanged();
    // open() finished (ok or not: failed() says why)
    void opened();
    void failed(const QString &title, const QString &message);
    void passwordNeeded(const QString &operation, bool wrong);
    void overwriteAsked(const QString &path);
    // a job ended: summary for the status bar / a message; written: what
    // extract wrote (the top-level files and folders)
    void finished(const QString &operation, bool ok, const QString &summary, const QStringList &problems,
                  const QStringList &written);

private:
    struct Row
    {
        ArchiveItem item;
        QString name;
        QString folder;
        bool selected = false;
    };

    using Job = std::function<void(JobContext &)>;
    void run(const QString &operation, const Job &job, const std::function<void()> &done);
    void setItems(const QVector<ArchiveItem> &items);
    void sortRows();
    void setProgress(int percent, const QString &file);
    QStringList pathsForExtract(bool selectedOnly, qint64 *bytes) const;
    void openViewed(const QString &file);
    friend class Archives;

    QString m_path;
    QString m_format;
    QString m_formatName;
    QString m_tool;
    bool m_encrypted = false;
    bool m_isNew = false;
    QString m_password;
    QString m_passwordFor; // the archive m_password was given for
    QVector<Row> m_rows;
    QVector<ArchiveItem> m_all; // dirs too
    int m_anchor = -1;
    int m_sortColumn = SortPath;
    bool m_sortAscending = true;

    bool m_busy = false;
    QString m_operation;
    int m_percent = -1;
    QString m_currentFile;
    std::shared_ptr<JobContext> m_ctx;
    QFutureWatcher<void> m_watcher;
    std::function<void()> m_done;
    std::function<void()> m_retry; // the job that wanted a password

    QMutex m_askMutex;
    QWaitCondition m_askCond;
    int m_answer = -1;

    std::unique_ptr<QTemporaryDir> m_viewDir;
};

// Archive Manager's helpers for QML (a singleton, `Archives`): file names
// and formats, sizes, settings, what's installed.
class Archives : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    Q_INVOKABLE QString formatForName(const QString &fileName) const { return ArchiveEngine::formatForName(fileName); }
    Q_INVOKABLE QString baseName(const QString &fileName) const { return ArchiveEngine::baseName(fileName); }
    Q_INVOKABLE bool canWrite(const QString &format) const { return ArchiveEngine::canWrite(format); }
    Q_INVOKABLE bool isSingleFile(const QString &format) const { return ArchiveEngine::isSingleFile(format); }
    Q_INVOKABLE QString toolNeeded(const QString &tool) const { return ArchiveEngine::toolNeeded(tool); }
    Q_INVOKABLE QString formatSize(qint64 bytes) const;
    Q_INVOKABLE bool exists(const QString &path) const;
    Q_INVOKABLE QStringList urlsToPaths(const QVariant &urls) const;
    // the file manager's window on a folder (or on a file's folder, with it selected)
    Q_INVOKABLE void showFolder(const QString &path) const;
    // what it can read and write, and whether 7-Zip / unar are installed
    Q_INVOKABLE QString formatsText() const;
    Q_INVOKABLE QString setting(const QString &key, const QString &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QString &value);
};
