#include "accountpassword.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <pty.h>
#include <pwd.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace {

QString chkpwdPath()
{
    // the automated tests' desktops never check your real password: they
    // use a stand-in (tests/fakes/unix_chkpwd)
    if (qEnvironmentVariableIsSet("WAYLAND95_NO_SYSTEM_AGENTS")) {
        const QString test = qEnvironmentVariable("WAYLAND95_TEST_CHKPWD");
        return QFileInfo(test).isExecutable() ? test : QString();
    }
    for (const char *p : { "/usr/sbin/unix_chkpwd", "/sbin/unix_chkpwd", "/usr/libexec/unix_chkpwd" })
        if (QFileInfo(QLatin1String(p)).isExecutable())
            return QLatin1String(p);
    return QString();
}

} // namespace

AccountPassword::AccountPassword(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { endChange(false, QStringLiteral("The password program didn't answer.")); });
}

AccountPassword::~AccountPassword()
{
    if (m_childPid > 0) {
        ::kill(pid_t(m_childPid), SIGKILL);
        ::waitpid(pid_t(m_childPid), nullptr, 0);
    }
    if (m_ptyFd >= 0)
        ::close(m_ptyFd);
}

QString AccountPassword::userName() const
{
    const passwd *pw = getpwuid(getuid());
    return pw ? QString::fromLocal8Bit(pw->pw_name) : QString();
}

bool AccountPassword::canCheck() const { return !chkpwdPath().isEmpty(); }

QStringList AccountPassword::administrators() const
{
    QStringList users;
    QFile group(QStringLiteral("/etc/group"));
    if (!group.open(QIODevice::ReadOnly))
        return users;
    for (const QByteArray &line : group.readAll().split('\n')) {
        const QList<QByteArray> f = line.split(':');
        if (f.size() < 4 || (f.at(0) != "sudo" && f.at(0) != "wheel" && f.at(0) != "admin"))
            continue;
        for (const QByteArray &u : f.at(3).split(','))
            if (!u.trimmed().isEmpty() && !users.contains(QString::fromLocal8Bit(u.trimmed())))
                users << QString::fromLocal8Bit(u.trimmed());
    }
    users.sort();
    return users;
}

void AccountPassword::check(const QString &password)
{
    if (m_check)
        return;
    const QString helper = chkpwdPath();
    if (helper.isEmpty()) {
        emit checked(false);
        return;
    }
    auto *p = new QProcess(this);
    m_check = p;
    emit busyChanged();
    p->setProgram(helper);
    // nullok: an account without a password has "" as its password
    p->setArguments({ userName(), QStringLiteral("nullok") });
    connect(p, &QProcess::started, p, [p, password] {
        p->write(password.toUtf8() + '\0');
        p->closeWriteChannel();
    });
    connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        const bool ok = status == QProcess::NormalExit && code == 0;
        // a wrong guess costs a moment, like a login prompt
        QTimer::singleShot(ok ? 0 : 1500, this, [this, ok] {
            m_check = nullptr;
            emit busyChanged();
            emit checked(ok);
        });
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        m_check = nullptr;
        emit busyChanged();
        emit checked(false);
    });
    p->start();
}

void AccountPassword::change(const QString &oldPassword, const QString &newPassword)
{
    if (m_childPid > 0)
        return;
    m_old = oldPassword;
    m_new = newPassword;
    m_newPrompts = 0;
    m_oldSent = false;
    m_output.clear();

    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, nullptr);
    if (pid < 0) {
        emit changeFinished(false, QStringLiteral("Couldn't start the password program."));
        return;
    }
    if (pid == 0) {
        // passwd's English prompts, which we answer
        setenv("LC_ALL", "C", 1);
        setenv("LANG", "C", 1);
        execlp("passwd", "passwd", static_cast<char *>(nullptr));
        _exit(127);
    }
    m_childPid = pid;
    m_ptyFd = master;
    ::fcntl(m_ptyFd, F_SETFL, ::fcntl(m_ptyFd, F_GETFL) | O_NONBLOCK);
    m_notifier = new QSocketNotifier(m_ptyFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &AccountPassword::onPtyReadable);
    m_timeout.start();
    emit busyChanged();
}

void AccountPassword::onPtyReadable()
{
    char buf[1024];
    for (;;) {
        const ssize_t n = ::read(m_ptyFd, buf, sizeof buf);
        if (n > 0) {
            m_output.append(buf, int(n));
            continue;
        }
        if (n < 0 && errno == EAGAIN)
            break;
        // EOF / EIO: passwd is done
        int status = 0;
        ::waitpid(pid_t(m_childPid), &status, 0);
        m_childPid = 0;
        const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        // the last thing it said, without the "passwd: " prefix
        QString message;
        const QStringList lines = QString::fromLocal8Bit(m_output).split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
        for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
            const QString l = it->trimmed();
            if (!l.isEmpty() && !l.endsWith(QLatin1Char(':'))) {
                message = l;
                break;
            }
        }
        message.remove(QRegularExpression(QStringLiteral("^passwd:\\s*")));
        endChange(ok, message);
        return;
    }

    // answer the prompt at the end of what it has said so far
    const QString tail = QString::fromLocal8Bit(m_output).section(QLatin1Char('\n'), -1).trimmed().toLower();
    if (!tail.endsWith(QLatin1Char(':')))
        return;
    QString answer;
    if (tail.contains(QLatin1String("current")) || (tail.contains(QLatin1String("password")) && !tail.contains(QLatin1String("new"))
                                                    && !tail.contains(QLatin1String("retype")) && !m_oldSent)) {
        answer = m_old;
        m_oldSent = true;
    } else if (tail.contains(QLatin1String("retype")) || tail.contains(QLatin1String("again"))) {
        answer = m_new;
    } else if (tail.contains(QLatin1String("new"))) {
        if (m_newPrompts++ > 0) {
            // asked again: it didn't like the new one; say why
            const QStringList lines = QString::fromLocal8Bit(m_output).split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
            QString why;
            for (int i = lines.size() - 2; i >= 0 && why.isEmpty(); --i)
                if (!lines.at(i).trimmed().endsWith(QLatin1Char(':')))
                    why = lines.at(i).trimmed();
            why.remove(QRegularExpression(QStringLiteral("^(passwd:|BAD PASSWORD:)\\s*")));
            ::kill(pid_t(m_childPid), SIGTERM);
            ::waitpid(pid_t(m_childPid), nullptr, 0);
            m_childPid = 0;
            endChange(false, why.isEmpty() ? QStringLiteral("The new password wasn't accepted.") : why);
            return;
        }
        answer = m_new;
    } else {
        return;
    }
    m_output.append('\n'); // what's been answered isn't a prompt any more
    const QByteArray line = answer.toLocal8Bit() + '\n';
    if (::write(m_ptyFd, line.constData(), size_t(line.size())) < 0) {
        // the read side notices it ending
    }
}

void AccountPassword::endChange(bool ok, const QString &message)
{
    m_timeout.stop();
    m_old.clear();
    m_new.clear();
    if (m_childPid > 0) {
        ::kill(pid_t(m_childPid), SIGKILL);
        ::waitpid(pid_t(m_childPid), nullptr, 0);
        m_childPid = 0;
    }
    delete m_notifier;
    m_notifier = nullptr;
    if (m_ptyFd >= 0) {
        ::close(m_ptyFd);
        m_ptyFd = -1;
    }
    emit busyChanged();
    emit changeFinished(ok, ok ? QStringLiteral("Your password has been changed.") : message);
}
