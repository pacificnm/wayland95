#pragma once

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// What Wayland Studio's QML needs from C++: project files, the element
// registry, and the runner (win95apprun) to check, run and make programs.
// `StudioTools` in QML.
class StudioTools : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit StudioTools(QObject *parent = nullptr);
    ~StudioTools() override;

    bool running() const { return m_run && m_run->state() != QProcess::NotRunning; }

    // elements.json, and one element type's properties (its own, then the
    // sets it takes, then the common ones)
    Q_INVOKABLE QVariantMap registry() const;
    Q_INVOKABLE QVariantList elementProps(const QString &type) const;
    // Wayland 95's icons, for the Icon chooser: [{ name, label }]
    Q_INVOKABLE QVariantList icons() const;
    Q_INVOKABLE QString elementNameProblem(const QString &name) const;
    Q_INVOKABLE QString formNameProblem(const QString &name) const;

    // a project with one empty form
    Q_INVOKABLE QVariantMap newProject() const;
    // { ok, error, project }
    Q_INVOKABLE QVariantMap loadProject(const QString &path) const;
    // "" or what went wrong
    Q_INVOKABLE QString saveProject(const QString &path, const QVariantMap &project) const;
    // the QML a form compiles to
    Q_INVOKABLE QString formQml(const QVariantMap &project, const QString &form, const QString &dir) const;

    Q_INVOKABLE QStringList fontFamilies() const;
    Q_INVOKABLE bool isColor(const QString &text) const;
    // `file` as it's kept in a project in `dir`: relative if it's inside
    Q_INVOKABLE QString projectRelative(const QString &dir, const QString &file) const;
    // a picture's URL for the canvas
    Q_INVOKABLE QString pictureUrl(const QString &dir, const QString &file) const;

    // the manual (docs/studio-manual, installed in share/wayland95/studio-manual):
    // `page` (index.md, building-an-app.md...) in the Markdown Editor's
    // Reading View; "" or what went wrong
    Q_INVOKABLE QString openManual(const QString &page = QStringLiteral("index.md")) const;
    // the manual's Book Club sample, copied to ~/Book Club (once): its
    // project's path ("" if it can't be)
    Q_INVOKABLE QString sampleProject() const;

    // the saved project through the runner: { ok, problems: [...] }
    Q_INVOKABLE QVariantMap check(const QString &projectPath) const;
    Q_INVOKABLE QVariantMap build(const QString &projectPath, const QString &programPath) const;
    // start the project; its output arrives line by line
    Q_INVOKABLE bool run(const QString &projectPath);
    Q_INVOKABLE void stop();

    // Claude's tools (win95studio-mcp): the command an MCP client starts
    // ("" if it isn't installed), whether Claude Code is, and adding the
    // server to it (claude mcp add, for all its projects): "" or what went wrong
    Q_INVOKABLE QString mcpServer() const;
    Q_INVOKABLE bool claudeCodeInstalled() const;
    Q_INVOKABLE QString addToClaudeCode() const;
    Q_INVOKABLE void copyText(const QString &text) const;

    // watch the open project's file ("": none): projectFileChanged when
    // another program (win95studio-mcp, an editor) writes it
    Q_INVOKABLE void watchProject(const QString &path);

signals:
    void runningChanged();
    void output(const QString &line);
    void finished(int exitCode);
    void projectFileChanged();

private:
    void projectFileTouched();

    QString runner() const;
    QString manualDir() const;
    QString claudeCode() const;
    QVariantMap runTool(const QStringList &arguments) const;

    QProcess *m_run = nullptr;
    QByteArray m_pending;
    class QFileSystemWatcher *m_watcher = nullptr;
    class QTimer *m_settle = nullptr;
    QString m_watched;
    QDateTime m_watchedTime;
    qint64 m_watchedSize = -1;
};
