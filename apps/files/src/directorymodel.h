#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

// The contents of one folder, as a flat list model for the icon and details
// views, plus everything the window does to it: navigation (with history),
// selection, sorting, clipboard, background copy/move, rename, new folder
// and the Recycle Bin (the freedesktop.org trash).
//
// Besides real directories, `path` can be one of two virtual locations:
//   "computer:"  My Computer - home folder, the computer's disks, and the
//                drives plugged in (Drives: UDisks2), opened or not
//   "trash:"     the Recycle Bin (~/.local/share/Trash/files)
class DirectoryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString displayPath READ displayPath NOTIFY pathChanged)
    Q_PROPERTY(QString title READ title NOTIFY pathChanged)
    Q_PROPERTY(bool isComputer READ isComputer NOTIFY pathChanged)
    Q_PROPERTY(bool isTrash READ isTrash NOTIFY pathChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY pathChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(bool writable READ writable NOTIFY pathChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY selectionChanged)
    Q_PROPERTY(QString sizeStatusText READ sizeStatusText NOTIFY selectionChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(int sortColumn READ sortColumn WRITE setSortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY canPasteChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // set false while the user is renaming, so a folder-watcher refresh
    // doesn't rebuild the delegate under the edit box
    Q_PROPERTY(bool autoRefresh READ autoRefresh WRITE setAutoRefresh NOTIFY autoRefreshChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirRole,
        SizeTextRole,
        TypeTextRole,
        ModifiedTextRole,
        IconKindRole,
        HiddenRole,
        SymlinkRole,
        SelectedRole,
    };
    Q_ENUM(Roles)

    enum SortColumn { SortName, SortSize, SortType, SortModified };
    Q_ENUM(SortColumn)

    enum SelectMode { Replace, Toggle, Extend };
    Q_ENUM(SelectMode)

    explicit DirectoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString path() const { return m_path; }
    void setPath(const QString &path); // navigates, recording history
    QString displayPath() const;
    QString title() const;
    bool isComputer() const { return m_path == QLatin1String("computer:"); }
    bool isTrash() const { return m_path == QLatin1String("trash:"); }
    bool canGoUp() const;
    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    bool writable() const;
    int count() const { return int(m_entries.size()); }
    int selectionCount() const;
    QString statusText() const;
    QString sizeStatusText() const;
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool show);
    int sortColumn() const { return m_sortColumn; }
    void setSortColumn(int column);
    bool sortAscending() const { return m_sortAscending; }
    void setSortAscending(bool ascending);
    bool canPaste() const { return m_canPaste; }
    bool busy() const { return m_busy; }
    bool autoRefresh() const { return m_autoRefresh; }
    void setAutoRefresh(bool on);

    // navigation
    Q_INVOKABLE void openRow(int row);
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void goHome();
    Q_INVOKABLE void refresh();

    // selection
    Q_INVOKABLE void select(int row, int mode);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE int firstSelectedRow() const;
    Q_INVOKABLE int findNextByPrefix(const QString &prefix, int fromRow) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE int rowOfName(const QString &name) const;
    Q_INVOKABLE QStringList selectedNames() const;

    // file operations (failures are reported through `error`)
    Q_INVOKABLE void copySelected();
    Q_INVOKABLE void cutSelected();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void trashSelected();
    Q_INVOKABLE void deleteSelectedPermanently();
    Q_INVOKABLE void restoreSelected(); // Recycle Bin only
    Q_INVOKABLE void emptyTrash();
    Q_INVOKABLE bool renameRow(int row, const QString &newName);
    Q_INVOKABLE int newFolder(); // returns the new row, or -1
    Q_INVOKABLE void openTerminalHere();
    Q_INVOKABLE QString propertiesText() const;

    // drives you plug in (My Computer): open one (it's mounted first), and
    // Eject (unmounted and powered off, then safe to pull out)
    Q_INVOKABLE void openDrive(const QString &id);
    Q_INVOKABLE bool selectedIsDrive() const;
    Q_INVOKABLE void ejectSelected();
    Q_INVOKABLE void ejectDrive(const QString &id);

signals:
    void pathChanged();
    void historyChanged();
    void countChanged();
    void selectionChanged();
    void showHiddenChanged();
    void sortChanged();
    void canPasteChanged();
    void busyChanged();
    void autoRefreshChanged();
    // something failed; the window shows it in a message box
    void error(const QString &title, const QString &message);
    // the view should make `row` current (e.g. after New Folder / paste)
    void rowFocusRequested(int row);
    // something worked that's worth saying (a drive is safe to remove)
    void notice(const QString &title, const QString &message);

private:
    struct Entry {
        QString name;
        QString path;
        bool isDir = false;
        bool hidden = false;
        bool symlink = false;
        bool selected = false;
        qint64 size = -1;
        QDateTime modified;
        QString typeText;
        QString iconKind;
        QString sizeText; // precomputed for drives (free of total)
        QString driveId;  // a plug-in drive (Drives' id)
    };

    void navigate(const QString &path, bool recordHistory);
    void reload(bool keepSelection);
    void loadComputer();
    void loadDirectory(const QString &dir);
    void sortEntries();
    void describe(Entry &e) const;
    QString realDir() const; // filesystem directory behind m_path, or empty
    QStringList selectedPaths() const;
    void setClipboard(bool cut);
    void updateCanPaste();
    void startTransfer(const QStringList &sources, const QString &destDir, bool move);
    void emitAllRowsChanged(int role);
public:
    static QString formatSize(qint64 bytes);
    // a file's icon ("folder", "text", ...) and Type column text
    static void describeFile(const QString &path, bool isDir, QString *iconKind, QString *typeText);
    // open a file as a double-click does (a program runs, text goes to
    // Notepad, the rest to its program); "" or what went wrong
    static QString openFile(const QString &path);
private:
    static QString trashFilesDir();
    static QString trashInfoDir();

    QString m_path;
    QVector<Entry> m_entries;
    QStringList m_back;
    QStringList m_forward;
    int m_anchorRow = -1;
    bool m_showHidden = false;
    int m_sortColumn = SortName;
    bool m_sortAscending = true;
    bool m_canPaste = false;
    bool m_busy = false;
    bool m_autoRefresh = true;
    bool m_refreshPending = false;
    QString m_pendingFocusName;

    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    QFutureWatcher<QStringList> m_transfer;
};
