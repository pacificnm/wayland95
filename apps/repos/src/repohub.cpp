#include "repohub.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QtQml/qqml.h>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QUrl>

namespace {

// the origin remote's URL, read from .git/config (no git run per repository)
QString originUrl(const QString &repoPath)
{
    QString config = repoPath + QStringLiteral("/.git/config");
    if (!QFileInfo(config).isFile()) {
        // a worktree or submodule: .git is a file naming the real one
        QFile gitFile(repoPath + QStringLiteral("/.git"));
        if (gitFile.open(QIODevice::ReadOnly)) {
            const QString line = QString::fromUtf8(gitFile.readLine()).trimmed();
            if (line.startsWith(QLatin1String("gitdir:"))) {
                QString dir = line.mid(7).trimmed();
                if (QFileInfo(dir).isRelative())
                    dir = repoPath + QLatin1Char('/') + dir;
                config = dir + QStringLiteral("/config");
            }
        }
    }
    QFile f(config);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    bool inOrigin = false;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inOrigin = line == QLatin1String("[remote \"origin\"]");
            continue;
        }
        if (inOrigin && line.startsWith(QLatin1String("url"))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0)
                return line.mid(eq + 1).trimmed();
        }
    }
    return QString();
}

bool isRepo(const QString &path)
{
    return QFileInfo::exists(path + QStringLiteral("/.git"));
}

QVariantMap describe(const QString &path)
{
    return {
        {QStringLiteral("path"), path},
        {QStringLiteral("name"), QFileInfo(path).fileName()},
        {QStringLiteral("slug"), RepoHub::slugOf(originUrl(path))},
    };
}

void scan(const QString &dir, int depth, QVariantList *out)
{
    if (isRepo(dir)) {
        out->append(describe(dir));
        return; // (not inside one: submodules and vendored copies aren't ours)
    }
    if (depth <= 0)
        return;
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        const QString name = info.fileName();
        if (info.isSymLink() || name == QLatin1String("node_modules") || name == QLatin1String("snap"))
            continue;
        scan(info.absoluteFilePath(), depth - 1, out);
    }
}

QString executable(const QString &name)
{
    // our own programs live beside this one (a build tree) or on the PATH
    const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath(name);
    if (QFileInfo(beside).isExecutable())
        return beside;
    return QStandardPaths::findExecutable(name);
}

} // namespace

RepoHub::RepoHub(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("win95-wayland"), QStringLiteral("win95repos"))
{
}

RepoHub::~RepoHub()
{
    // a push or clone still going when the window closes: let it be stopped
    for (QProcess *p : std::as_const(m_processes)) {
        p->disconnect();
        p->kill();
        p->waitForFinished(1000);
    }
}

void RepoHub::run(const QString &program, const QStringList &args, const QString &cwd,
                  const QJSValue &callback, const QString &input)
{
    auto *p = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GH_PROMPT_DISABLED"), QStringLiteral("1"));
    env.insert(QStringLiteral("GH_NO_UPDATE_NOTIFIER"), QStringLiteral("1"));
    env.insert(QStringLiteral("GH_SPINNER_DISABLED"), QStringLiteral("1"));
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    env.insert(QStringLiteral("CLICOLOR"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_EDITOR"), QStringLiteral("true"));
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    env.insert(QStringLiteral("GH_PAGER"), QStringLiteral("cat"));
    // messages in English, as the window shows them (and checks some)
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("LANGUAGE"), QStringLiteral("C"));
    p->setProcessEnvironment(env);
    if (!cwd.isEmpty())
        p->setWorkingDirectory(cwd);
    m_processes << p;
    ++m_busy;
    emit busyChanged();

    QJSValue cb = callback;
    auto finish = [this, p, cb](int code, const QString &err) mutable {
        m_processes.removeAll(p);
        --m_busy;
        emit busyChanged();
        const QString out = QString::fromUtf8(p->readAllStandardOutput());
        const QString errText = err.isEmpty() ? QString::fromUtf8(p->readAllStandardError()) : err;
        p->deleteLater();
        if (cb.isCallable())
            cb.call({code, out, errText});
    };
    connect(p, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) mutable {
        finish(status == QProcess::NormalExit ? code : -1, QString());
    });
    connect(p, &QProcess::errorOccurred, this, [finish, p, program](QProcess::ProcessError error) mutable {
        if (error == QProcess::FailedToStart)
            finish(-2, QStringLiteral("'%1' isn't installed (or can't be run).").arg(program));
    });
    p->start(program, args);
    if (!input.isNull()) {
        p->write(input.toUtf8());
        p->closeWriteChannel();
    } else {
        p->closeWriteChannel();
    }
}

void RepoHub::scanLocal(const QStringList &roots, int depth, const QJSValue &callback)
{
    QPointer<RepoHub> self(this);
    QJSValue cb = callback;
    ++m_busy;
    emit busyChanged();
    QThread *thread = QThread::create([self, roots, depth, cb]() mutable {
        QVariantList found;
        QStringList seen;
        for (const QString &root : roots) {
            const QString dir = QDir::cleanPath(root);
            if (!QFileInfo(dir).isDir())
                continue;
            QVariantList here;
            scan(dir, depth, &here);
            for (const QVariant &v : std::as_const(here)) {
                const QString path = v.toMap().value(QStringLiteral("path")).toString();
                if (!seen.contains(path)) {
                    seen << path;
                    found << v;
                }
            }
        }
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, cb, found]() mutable {
            if (!self)
                return;
            --self->m_busy;
            emit self->busyChanged();
            QJSEngine *engine = qjsEngine(self.data());
            if (cb.isCallable() && engine)
                cb.call({engine->toScriptValue(found)});
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QVariantMap RepoHub::localRepo(const QString &path) const
{
    const QString dir = QDir::cleanPath(expandPath(path));
    if (!isRepo(dir))
        return {};
    return describe(dir);
}

QString RepoHub::slugOf(const QString &url)
{
    static const QRegularExpression re(QStringLiteral(
        "^(?:https?://(?:[^@/]+@)?github\\.com/|git@github\\.com:|ssh://git@github\\.com/|git://github\\.com/)"
        "([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\\.git)?/?$"));
    const QRegularExpressionMatch m = re.match(url.trimmed());
    return m.hasMatch() ? m.captured(1) + QLatin1Char('/') + m.captured(2) : QString();
}

QString RepoHub::homePath() const
{
    return QDir::homePath();
}

bool RepoHub::exists(const QString &path) const
{
    return QFileInfo::exists(expandPath(path));
}

bool RepoHub::isDir(const QString &path) const
{
    return QFileInfo(expandPath(path)).isDir();
}

QString RepoHub::joinPath(const QString &dir, const QString &name) const
{
    return QDir::cleanPath(expandPath(dir) + QLatin1Char('/') + name);
}

QString RepoHub::expandPath(const QString &path) const
{
    const QString p = path.trimmed();
    if (p == QLatin1String("~"))
        return QDir::homePath();
    if (p.startsWith(QLatin1String("~/")))
        return QDir::homePath() + p.mid(1);
    return p;
}

QString RepoHub::readText(const QString &path, int maxBytes) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray data = f.read(maxBytes);
    if (data.left(8192).contains('\0'))
        return QString();
    return QString::fromUtf8(data);
}

bool RepoHub::startProgram(const QString &program, const QStringList &args, const QString &cwd) const
{
    const QString exe = executable(program);
    return !exe.isEmpty() && QProcess::startDetached(exe, args, cwd);
}

bool RepoHub::openUrl(const QString &url) const
{
    return QDesktopServices::openUrl(QUrl(url));
}

bool RepoHub::openFolder(const QString &path) const
{
    return startProgram(QStringLiteral("win95files"), {path});
}

bool RepoHub::openTerminal(const QString &path, const QStringList &command) const
{
    QStringList args{QStringLiteral("--workdir"), path};
    if (!command.isEmpty())
        args << QStringLiteral("-e") << command;
    return startProgram(QStringLiteral("win95terminal"), args, path);
}

QVariant RepoHub::setting(const QString &key, const QVariant &fallback) const
{
    return m_settings.value(key, fallback);
}

void RepoHub::setSetting(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
    m_settings.sync();
}
