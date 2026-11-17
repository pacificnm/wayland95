#pragma once

#include <QJSValue>
#include <QObject>
#include <QSettings>
#include <QVariant>

class QProcess;

// What Repositories (Main.qml) needs from outside QML: running gh and git
// without blocking the window, finding the clones on this computer, and
// opening things in the desktop's other programs.
class RepoHub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int busy READ busy NOTIFY busyChanged)

public:
    explicit RepoHub(QObject *parent = nullptr);
    ~RepoHub() override;

    int busy() const { return m_busy; }

    // runs PROGRAM ARGS in CWD (gh or git, never a shell), then
    // callback(exitCode, stdout, stderr); input goes to its stdin. Neither
    // ever asks for anything on a terminal (GH_PROMPT_DISABLED,
    // GIT_TERMINAL_PROMPT=0): they fail and say why instead.
    Q_INVOKABLE void run(const QString &program, const QStringList &args, const QString &cwd,
                         const QJSValue &callback, const QString &input = QString());

    // the git repositories under these folders (depth folders down; hidden
    // folders and node_modules aren't looked in), on a worker thread, then
    // callback([{ path, name, slug }]): slug is "owner/name" when its
    // origin is on github.com, else ""
    Q_INVOKABLE void scanLocal(const QStringList &roots, int depth, const QJSValue &callback);
    // one folder: { path, name, slug }, or {} if it isn't a git repository
    Q_INVOKABLE QVariantMap localRepo(const QString &path) const;
    // "https://github.com/a/b.git", "git@github.com:a/b" ... -> "a/b"
    Q_INVOKABLE static QString slugOf(const QString &url);

    Q_INVOKABLE QString homePath() const;
    Q_INVOKABLE bool exists(const QString &path) const;
    Q_INVOKABLE bool isDir(const QString &path) const;
    Q_INVOKABLE QString joinPath(const QString &dir, const QString &name) const;
    Q_INVOKABLE QString expandPath(const QString &path) const;
    // a file's text (the start of it, for a new file's "diff"); "" if binary
    Q_INVOKABLE QString readText(const QString &path, int maxBytes = 200000) const;

    Q_INVOKABLE bool openUrl(const QString &url) const;
    Q_INVOKABLE bool openFolder(const QString &path) const;
    Q_INVOKABLE bool openTerminal(const QString &path, const QStringList &command = {}) const;

    // settings: ~/.config/win95-wayland/win95repos.conf
    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);

signals:
    void busyChanged();

private:
    bool startProgram(const QString &program, const QStringList &args, const QString &cwd = QString()) const;

    int m_busy = 0;
    QList<QProcess *> m_processes;
    QSettings m_settings;
};
