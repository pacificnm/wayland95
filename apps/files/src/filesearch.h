#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>
#include <QVariantMap>

#include <atomic>
#include <memory>

// Find: All Files (win95files --find). Walks the folders on a worker
// thread and hands what matches to the window in batches, as a list
// model: Name, In Folder, Size, Type, Modified.
//
// What to look for (start()'s criteria):
//   named        names to match: "*.txt; report" (several, ; between them;
//                one without * or ? matches anywhere in the name). Any case
//                unless caseSensitive.
//   lookIn       the folder to start in; subfolders  look in those too
//   days         modified in the last N days (0: any time)
//   containing   text inside the file (plain-text files; any case unless
//                caseSensitive)
//   hidden       look at hidden files and folders too (names starting ".")
//
// It never follows a link to a folder, and leaves out /proc, /sys, /dev
// and /run (not files anyone means to find).
class FileSearch : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString searchingIn READ searchingIn NOTIFY searchingInChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY runningChanged)
    Q_PROPERTY(int sortColumn READ sortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        FolderRole,
        PathRole,
        IconKindRole,
        TypeTextRole,
        SizeTextRole,
        ModifiedTextRole,
        IsDirRole,
        SelectedRole,
    };
    enum Column { SortName, SortFolder, SortSize, SortType, SortModified };
    Q_ENUM(Column)

    static constexpr int MaxResults = 10000;

    explicit FileSearch(QObject *parent = nullptr);
    ~FileSearch() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool running() const { return m_running; }
    int count() const { return int(m_results.size()); }
    QString searchingIn() const { return m_searchingIn; }
    bool truncated() const { return m_truncated; }
    int sortColumn() const { return m_sortColumn; }
    bool sortAscending() const { return m_sortAscending; }

    Q_INVOKABLE void start(const QVariantMap &criteria);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void sortBy(int column);

    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE bool isDirAt(int row) const;
    Q_INVOKABLE void select(int row, int mode); // 0 only this, 1 toggle (Ctrl), 2 range (Shift)
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE QStringList selectedPaths() const;
    // open a result: a folder in the file manager, a file as a double-click
    // in it does; "" or what went wrong
    Q_INVOKABLE QString open(int row);
    // the folder a result is in, in the file manager, with it selected
    Q_INVOKABLE QString openContainingFolder(int row);
    // what a home-relative path looks like ("~/Documents"), for the Look in box
    Q_INVOKABLE QString expandPath(const QString &path) const;
    Q_INVOKABLE QString properties(int row) const;

    struct Result {
        QString name, folder, path, iconKind, typeText;
        qint64 size = -1;
        QDateTime modified;
        bool isDir = false;
        bool selected = false;
    };

signals:
    void runningChanged();
    void countChanged();
    void searchingInChanged();
    void sortChanged();
    void finished(int found);

private:
    void addBatch(quint64 generation, const QList<Result> &batch);
    void noteFolder(quint64 generation, const QString &folder);
    void done(quint64 generation, bool truncated);
    void sortResults();
    bool lessThan(const Result &a, const Result &b) const;

    QList<Result> m_results;
    bool m_running = false;
    bool m_truncated = false;
    QString m_searchingIn;
    int m_anchor = -1;
    int m_sortColumn = -1; // in the order found until a header is clicked
    bool m_sortAscending = true;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    QThread *m_thread = nullptr;
};
