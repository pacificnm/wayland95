#include "studiotools.h"

#include "appcompiler.h"

#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

StudioTools::StudioTools(QObject *parent)
    : QObject(parent)
{
}

StudioTools::~StudioTools()
{
    stop();
}

QVariantMap StudioTools::registry() const
{
    return AppCompiler::registry().toVariantMap();
}

QVariantList StudioTools::elementProps(const QString &type) const
{
    return AppCompiler::elementProps(type).toVariantList();
}

QVariantList StudioTools::icons() const
{
    return AppCompiler::icons().toVariantList();
}

QString StudioTools::elementNameProblem(const QString &name) const
{
    return AppCompiler::nameProblem(name, true);
}

QString StudioTools::formNameProblem(const QString &name) const
{
    return AppCompiler::nameProblem(name, false);
}

QVariantMap StudioTools::newProject() const
{
    const QVariantMap form{
        { QStringLiteral("name"), QStringLiteral("Form1") },
        { QStringLiteral("width"), 400 },
        { QStringLiteral("height"), 296 },
        { QStringLiteral("props"), QVariantMap{ { QStringLiteral("title"), QStringLiteral("Form1") } } },
        { QStringLiteral("code"), QVariantMap() },
        { QStringLiteral("controls"), QVariantList() },
    };
    return {
        { QStringLiteral("format"), 1 },
        { QStringLiteral("app"), QVariantMap{ { QStringLiteral("name"), QStringLiteral("project1") },
                                              { QStringLiteral("title"), QStringLiteral("Project1") },
                                              { QStringLiteral("version"), QStringLiteral("1.0") },
                                              { QStringLiteral("startForm"), QStringLiteral("Form1") } } },
        { QStringLiteral("database"), QVariantMap{ { QStringLiteral("driver"), QStringLiteral("sqlite") },
                                                   { QStringLiteral("file"), QStringLiteral("data.db") } } },
        { QStringLiteral("tables"), QVariantList() },
        { QStringLiteral("forms"), QVariantList{ form } },
    };
}

QVariantMap StudioTools::loadProject(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { { QStringLiteral("ok"), false },
                 { QStringLiteral("error"), QStringLiteral("%1 can't be opened: %2").arg(path, file.errorString()) } };
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    const QJsonObject project = doc.object();
    if (!doc.isObject() || !project.contains(QLatin1String("forms")))
        return { { QStringLiteral("ok"), false },
                 { QStringLiteral("error"), QStringLiteral("%1 isn't a Wayland Studio project.").arg(path) } };
    if (project.value(QLatin1String("format")).toInt(1) > 1)
        return { { QStringLiteral("ok"), false },
                 { QStringLiteral("error"), QStringLiteral("%1 was made by a newer Wayland Studio.").arg(path) } };
    // everything the Studio expects to be there
    QVariantMap map = project.toVariantMap();
    for (const char *key : { "app", "database" }) {
        if (!map.contains(QLatin1String(key)))
            map.insert(QLatin1String(key), newProject().value(QLatin1String(key)));
    }
    if (!map.contains(QStringLiteral("tables")))
        map.insert(QStringLiteral("tables"), QVariantList());
    return { { QStringLiteral("ok"), true }, { QStringLiteral("project"), map } };
}

QString StudioTools::saveProject(const QString &path, const QVariantMap &project) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("%1 can't be written: %2").arg(path, file.errorString());
    file.write(QJsonDocument(QJsonObject::fromVariantMap(project)).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return QStringLiteral("%1 can't be written: %2").arg(path, file.errorString());
    return QString();
}

QString StudioTools::formQml(const QVariantMap &project, const QString &form, const QString &dir) const
{
    const QJsonObject json = QJsonObject::fromVariantMap(project);
    const QJsonArray forms = json.value(QLatin1String("forms")).toArray();
    for (const QJsonValue &f : forms) {
        if (f.toObject().value(QLatin1String("name")).toString() == form)
            return AppCompiler::formQml(f.toObject(), json, dir);
    }
    return QString();
}

QStringList StudioTools::fontFamilies() const
{
    return QFontDatabase::families();
}

bool StudioTools::isColor(const QString &text) const
{
    return QColor::isValidColorName(text);
}

QString StudioTools::projectRelative(const QString &dir, const QString &file) const
{
    if (dir.isEmpty())
        return file;
    const QString relative = QDir(dir).relativeFilePath(file);
    return relative.startsWith(QLatin1String("..")) ? file : relative;
}

QString StudioTools::pictureUrl(const QString &dir, const QString &file) const
{
    if (file.isEmpty())
        return QString();
    QString path = file;
    if (path.startsWith(QLatin1String("~/")))
        path = QDir::homePath() + path.mid(1);
    if (QFileInfo(path).isRelative())
        path = QDir(dir.isEmpty() ? QDir::homePath() : dir).filePath(path);
    return QFileInfo::exists(path) ? QUrl::fromLocalFile(path).toString() : QString();
}

// win95apprun: beside this program (a build tree, an install), else on PATH
QString StudioTools::manualDir() const
{
    QStringList places;
    const QString env = qEnvironmentVariable("W95_STUDIO_MANUAL");
    if (!env.isEmpty())
        places << env;
    places << QStringLiteral(WAYLAND95_STUDIO_MANUAL)
           << QCoreApplication::applicationDirPath() + QStringLiteral("/../share/wayland95/studio-manual")
           << QStringLiteral(WAYLAND95_STUDIO_MANUAL_SOURCE); // (a build that isn't installed)
    for (const QString &dir : std::as_const(places)) {
        if (QFileInfo::exists(dir + QStringLiteral("/index.md")))
            return QDir(dir).absolutePath();
    }
    return QString();
}

QString StudioTools::openManual(const QString &page) const
{
    const QString dir = manualDir();
    if (dir.isEmpty())
        return QStringLiteral("The Wayland Studio manual isn't installed.");
    QString file = QDir(dir).filePath(page.isEmpty() ? QStringLiteral("index.md") : page);
    if (!QFileInfo::exists(file))
        file = QDir(dir).filePath(QStringLiteral("index.md"));
    QString editor = QCoreApplication::applicationDirPath() + QStringLiteral("/win95markdown");
    if (!QFileInfo(editor).isExecutable())
        editor = QStandardPaths::findExecutable(QStringLiteral("win95markdown"));
    if (editor.isEmpty())
        return QStringLiteral("The manual opens in the Markdown Editor (win95markdown), which isn't installed.");
    if (!QProcess::startDetached(editor, { QStringLiteral("--reader"), file }))
        return QStringLiteral("The Markdown Editor didn't start.");
    return QString();
}

QString StudioTools::sampleProject() const
{
    const QString dir = manualDir();
    const QString from = QDir(dir).filePath(QStringLiteral("sample"));
    if (dir.isEmpty() || !QFileInfo::exists(from + QStringLiteral("/bookclub.w95proj")))
        return QString();
    const QString to = QDir::homePath() + QStringLiteral("/Book Club");
    const QString project = to + QStringLiteral("/bookclub.w95proj");
    if (QFileInfo::exists(project))
        return project;
    if (!QDir().mkpath(to))
        return QString();
    for (const QString &name : { QStringLiteral("bookclub.w95proj"), QStringLiteral("bookclub.db") }) {
        QFile::copy(from + QLatin1Char('/') + name, to + QLatin1Char('/') + name);
        QFile(to + QLatin1Char('/') + name).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                            | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }
    return QFileInfo::exists(project) ? project : QString();
}

QString StudioTools::runner() const
{
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/win95apprun");
    if (QFileInfo(beside).isExecutable())
        return beside;
    return QStandardPaths::findExecutable(QStringLiteral("win95apprun"));
}

QVariantMap StudioTools::runTool(const QStringList &arguments) const
{
    const QString program = runner();
    if (program.isEmpty())
        return { { QStringLiteral("ok"), false },
                 { QStringLiteral("problems"), QStringList{ QStringLiteral("win95apprun, which runs and makes apps, isn't installed.") } } };
    QProcess tool;
    // no window of its own while it checks
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    tool.setProcessEnvironment(env);
    tool.start(program, arguments);
    if (!tool.waitForFinished(30000)) {
        tool.kill();
        tool.waitForFinished(1000);
        return { { QStringLiteral("ok"), false },
                 { QStringLiteral("problems"), QStringList{ QStringLiteral("win95apprun didn't answer.") } } };
    }
    QStringList problems;
    const QString text = QString::fromUtf8(tool.readAllStandardOutput()) + QString::fromUtf8(tool.readAllStandardError());
    for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        problems << line;
    const bool ok = tool.exitStatus() == QProcess::NormalExit && tool.exitCode() == 0;
    if (!ok && problems.isEmpty())
        problems << QStringLiteral("win95apprun stopped (exit %1).").arg(tool.exitCode());
    return { { QStringLiteral("ok"), ok }, { QStringLiteral("problems"), problems } };
}

QVariantMap StudioTools::check(const QString &projectPath) const
{
    return runTool({ QStringLiteral("--check"), projectPath });
}

QVariantMap StudioTools::build(const QString &projectPath, const QString &programPath) const
{
    return runTool({ QStringLiteral("--build"), projectPath, QStringLiteral("-o"), programPath });
}

bool StudioTools::run(const QString &projectPath)
{
    if (running())
        return false;
    const QString program = runner();
    if (program.isEmpty()) {
        emit output(QStringLiteral("win95apprun, which runs apps, isn't installed."));
        return false;
    }
    if (m_run)
        m_run->deleteLater();
    m_run = new QProcess(this);
    m_run->setProcessChannelMode(QProcess::MergedChannels);
    m_pending.clear();
    connect(m_run, &QProcess::readyRead, this, [this] {
        m_pending += m_run->readAll();
        int end;
        while ((end = m_pending.indexOf('\n')) >= 0) {
            emit output(QString::fromUtf8(m_pending.left(end)));
            m_pending.remove(0, end + 1);
        }
    });
    connect(m_run, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!m_pending.isEmpty())
            emit output(QString::fromUtf8(m_pending));
        m_pending.clear();
        emit runningChanged();
        emit finished(status == QProcess::NormalExit ? code : -1);
    });
    connect(m_run, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit output(QStringLiteral("The app didn't start: ") + m_run->errorString());
            emit runningChanged();
        }
    });
    m_run->start(program, { projectPath });
    emit runningChanged();
    return true;
}

void StudioTools::stop()
{
    if (!m_run || m_run->state() == QProcess::NotRunning)
        return;
    m_run->terminate();
    if (!m_run->waitForFinished(1500)) {
        m_run->kill();
        m_run->waitForFinished(1000);
    }
}

// The file and its folder are both watched: a program that saves by writing
// a new file and renaming it over the old one (QSaveFile, as the Studio and
// win95studio-mcp do) replaces the file the watcher was following. What
// changed is told by the file's time and size, a moment after the writes
// stop.
void StudioTools::watchProject(const QString &path)
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        m_settle = new QTimer(this);
        m_settle->setSingleShot(true);
        m_settle->setInterval(150);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &StudioTools::projectFileTouched);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &StudioTools::projectFileTouched);
        connect(m_settle, &QTimer::timeout, this, [this]() {
            const QFileInfo info(m_watched);
            if (!m_watched.isEmpty() && !m_watcher->files().contains(m_watched) && info.exists())
                m_watcher->addPath(m_watched);
            if (!info.exists() || (info.lastModified() == m_watchedTime && info.size() == m_watchedSize))
                return;
            m_watchedTime = info.lastModified();
            m_watchedSize = info.size();
            emit projectFileChanged();
        });
    }
    const QString file = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    if (file == m_watched) {
        // (saved by the Studio: that's the file as it is now)
        const QFileInfo info(file);
        m_watchedTime = info.lastModified();
        m_watchedSize = info.size();
        return;
    }
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    m_watched = file;
    if (file.isEmpty())
        return;
    const QFileInfo info(file);
    m_watchedTime = info.lastModified();
    m_watchedSize = info.size();
    m_watcher->addPath(info.absolutePath());
    if (info.exists())
        m_watcher->addPath(file);
}

void StudioTools::projectFileTouched()
{
    m_settle->start();
}

QString StudioTools::mcpServer() const
{
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/win95studio-mcp");
    if (QFileInfo(beside).isExecutable())
        return beside;
    return QStandardPaths::findExecutable(QStringLiteral("win95studio-mcp"));
}

// claude: on PATH, or where its installer puts it
QString StudioTools::claudeCode() const
{
    QString found = QStandardPaths::findExecutable(QStringLiteral("claude"));
    for (const QString &place : { QStringLiteral("/.local/bin/claude"), QStringLiteral("/.claude/local/claude") }) {
        if (found.isEmpty() && QFileInfo(QDir::homePath() + place).isExecutable())
            found = QDir::homePath() + place;
    }
    return found;
}

bool StudioTools::claudeCodeInstalled() const
{
    return !claudeCode().isEmpty();
}

QString StudioTools::addToClaudeCode() const
{
    const QString claude = claudeCode();
    const QString server = mcpServer();
    if (claude.isEmpty())
        return QStringLiteral("Claude Code isn't installed.");
    if (server.isEmpty())
        return QStringLiteral("win95studio-mcp isn't installed.");
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(claude, { QStringLiteral("mcp"), QStringLiteral("add"), QStringLiteral("--scope"), QStringLiteral("user"),
                      QStringLiteral("wayland-studio"), QStringLiteral("--"), server });
    if (!p.waitForStarted(10000) || !p.waitForFinished(30000)) {
        p.kill();
        return QStringLiteral("claude didn't answer.");
    }
    const QString said = QString::fromUtf8(p.readAll()).trimmed();
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
        return QString();
    if (said.contains(QLatin1String("already exists")))
        return QString(); // (added before)
    return said.isEmpty() ? QStringLiteral("claude mcp add failed.") : said;
}

void StudioTools::copyText(const QString &text) const
{
    if (QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(text);
}
