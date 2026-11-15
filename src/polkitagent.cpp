#include "polkitagent.h"

#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QFileInfo>
#include <QTimer>
#include <QtDebug>

#include <pwd.h>
#include <unistd.h>

namespace {

const QString Authority = QStringLiteral("org.freedesktop.PolicyKit1");
const QString AuthorityPath = QStringLiteral("/org/freedesktop/PolicyKit1/Authority");
const QString AuthorityIface = QStringLiteral("org.freedesktop.PolicyKit1.Authority");
const QString AgentPath = QStringLiteral("/org/wayland95/PolicyKit1/AuthenticationAgent");

QString helperPath()
{
    for (const char *p : { "/usr/lib/polkit-1/polkit-agent-helper-1", "/usr/libexec/polkit-agent-helper-1",
                           "/usr/lib/policykit-1/polkit-agent-helper-1", "/usr/libexec/polkit-1/polkit-agent-helper-1" })
        if (QFileInfo(QLatin1String(p)).isExecutable())
            return QLatin1String(p);
    return QString();
}

QString userName(uint uid)
{
    const passwd *pw = getpwuid(uid);
    return pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(uid);
}

} // namespace

QDBusArgument &operator<<(QDBusArgument &arg, const PolkitIdentity &id)
{
    arg.beginStructure();
    arg << id.kind << id.details;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PolkitIdentity &id)
{
    arg.beginStructure();
    arg >> id.kind >> id.details;
    arg.endStructure();
    return arg;
}

// ---------------------------------------------------------------------------

PolkitAgentAdaptor::PolkitAgentAdaptor(PolkitAgent *agent)
    : QObject(agent)
    , m_agent(agent)
{
}

void PolkitAgentAdaptor::BeginAuthentication(const QString &actionId, const QString &message, const QString &,
                                             const QMap<QString, QString> &, const QString &cookie,
                                             const QList<PolkitIdentity> &identities)
{
    // answered once the user has typed the password (or given up)
    setDelayedReply(true);
    m_agent->begin(this->message(), actionId, message, cookie, identities);
}

void PolkitAgentAdaptor::CancelAuthentication(const QString &cookie)
{
    m_agent->cancelCookie(cookie);
}

// ---------------------------------------------------------------------------

PolkitAgent::PolkitAgent(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<PolkitIdentity>();
    qDBusRegisterMetaType<PolkitIdentity>();
    qDBusRegisterMetaType<QList<PolkitIdentity>>();
    qDBusRegisterMetaType<QMap<QString, QString>>();
    // after startup: not in the way of the desktop appearing
    QTimer::singleShot(0, this, &PolkitAgent::registerAgent);
}

void PolkitAgent::registerAgent()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected() || helperPath().isEmpty())
        return;
    if (!bus.registerObject(AgentPath, new PolkitAgentAdaptor(this), QDBusConnection::ExportAllSlots))
        return;

    // our login session (logind knows it from our process)
    QString session = qEnvironmentVariable("XDG_SESSION_ID");
    if (session.isEmpty()) {
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
                                                           QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("GetSessionByPID"));
        call << uint(getpid());
        const QDBusReply<QDBusObjectPath> path = bus.call(call, QDBus::Block, 3000);
        if (path.isValid()) {
            QDBusMessage get = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"), path.value().path(),
                                                              QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
            get << QStringLiteral("org.freedesktop.login1.Session") << QStringLiteral("Id");
            const QDBusReply<QDBusVariant> id = bus.call(get, QDBus::Block, 3000);
            if (id.isValid())
                session = id.value().variant().toString();
        }
    }
    if (session.isEmpty()) {
        qInfo() << "Password prompts: not in a login session, so no polkit agent";
        return;
    }

    const PolkitIdentity subject{ QStringLiteral("unix-session"), { { QStringLiteral("session-id"), session } } };
    QDBusMessage call = QDBusMessage::createMethodCall(Authority, AuthorityPath, AuthorityIface, QStringLiteral("RegisterAuthenticationAgent"));
    call << QVariant::fromValue(subject) << qEnvironmentVariable("LANG", QStringLiteral("en_US.UTF-8")) << AgentPath;
    const QDBusMessage reply = bus.call(call, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        // usually: the desktop we're nested in has its own agent
        qInfo().noquote() << "Password prompts: left to the existing polkit agent (" + reply.errorMessage() + ")";
        return;
    }
    qInfo() << "Password prompts: Wayland 95 is the polkit agent for session" << session;
    m_registered = true;
    emit registeredChanged();
}

QString PolkitAgent::message() const { return m_requests.isEmpty() ? QString() : m_requests.first().message; }
QString PolkitAgent::actionId() const { return m_requests.isEmpty() ? QString() : m_requests.first().actionId; }
QVariantList PolkitAgent::users() const { return m_requests.isEmpty() ? QVariantList() : m_requests.first().users; }

int PolkitAgent::defaultUser() const
{
    // you, if you're allowed; otherwise the first admin
    const QVariantList list = users();
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).toMap().value(QStringLiteral("uid")).toUInt() == getuid())
            return i;
    return 0;
}

void PolkitAgent::begin(const QDBusMessage &call, const QString &actionId, const QString &message,
                        const QString &cookie, const QList<PolkitIdentity> &identities)
{
    Request r{ call, actionId, message, cookie, {} };
    for (const PolkitIdentity &id : identities) {
        if (id.kind != QLatin1String("unix-user"))
            continue; // groups come already expanded into their users
        const uint uid = id.details.value(QStringLiteral("uid")).toUInt();
        r.users << QVariantMap{ { QStringLiteral("name"), userName(uid) }, { QStringLiteral("uid"), uid } };
    }
    if (r.users.isEmpty()) {
        QDBusConnection::systemBus().send(call.createErrorReply(QStringLiteral("org.freedesktop.PolicyKit1.Error.Failed"),
                                                                QStringLiteral("No user to authenticate as")));
        return;
    }
    const bool first = m_requests.isEmpty();
    m_requests << r;
    if (first) {
        m_error.clear();
        emit requestChanged();
        emit stateChanged();
        emit requested();
    }
}

void PolkitAgent::authenticate(int userIndex, const QString &password)
{
    if (m_requests.isEmpty() || m_helper)
        return;
    const Request &r = m_requests.first();
    const QString user = r.users.value(userIndex).toMap().value(QStringLiteral("name")).toString();
    if (user.isEmpty())
        return;

    m_error.clear();
    m_password = password;
    m_helperMessages.clear();
    auto *helper = new QProcess(this);
    m_helper = helper;
    helper->setProgram(helperPath());
    helper->setArguments({ user });
    helper->setProcessChannelMode(QProcess::SeparateChannels);
    const QString cookie = r.cookie;

    connect(helper, &QProcess::started, helper, [helper, cookie] {
        // the cookie goes on stdin, not the command line, where anyone could read it
        helper->write(cookie.toUtf8() + '\n');
    });
    connect(helper, &QProcess::readyReadStandardOutput, this, [this, helper] {
        while (helper->canReadLine()) {
            const QString line = QString::fromUtf8(helper->readLine()).trimmed();
            if (line.startsWith(QLatin1String("PAM_PROMPT_ECHO_OFF")) || line.startsWith(QLatin1String("PAM_PROMPT_ECHO_ON"))) {
                // the password prompt (any later prompt gets it too: we have
                // nothing else to give)
                helper->write(m_password.toUtf8() + '\n');
            } else if (line.startsWith(QLatin1String("PAM_ERROR_MSG")) || line.startsWith(QLatin1String("PAM_TEXT_INFO"))) {
                m_helperMessages = line.section(QLatin1Char(' '), 1).trimmed();
            } else if (line == QLatin1String("SUCCESS")) {
                m_password.clear();
                stopHelper();
                finish(false);
                return;
            } else if (line == QLatin1String("FAILURE")) {
                m_password.clear();
                stopHelper();
                m_error = m_helperMessages.isEmpty() ? QStringLiteral("The password is incorrect. Try again.") : m_helperMessages;
                emit stateChanged();
                return;
            }
        }
    });
    connect(helper, &QProcess::finished, this, [this, helper] {
        if (m_helper != helper)
            return; // already handled
        m_password.clear();
        stopHelper();
        m_error = QStringLiteral("The password couldn't be checked.");
        emit stateChanged();
    });
    helper->start();
    emit stateChanged();
}

void PolkitAgent::stopHelper()
{
    if (!m_helper)
        return;
    QProcess *h = m_helper;
    m_helper = nullptr;
    h->disconnect(this);
    h->closeWriteChannel();
    if (h->state() != QProcess::NotRunning)
        QTimer::singleShot(2000, h, [h] { h->kill(); });
    connect(h, &QProcess::finished, h, &QObject::deleteLater);
    if (h->state() == QProcess::NotRunning)
        h->deleteLater();
    emit stateChanged();
}

void PolkitAgent::cancel()
{
    if (m_requests.isEmpty())
        return;
    m_password.clear();
    stopHelper();
    finish(true);
}

void PolkitAgent::cancelCookie(const QString &cookie)
{
    for (int i = 0; i < m_requests.size(); ++i) {
        if (m_requests.at(i).cookie != cookie)
            continue;
        if (i == 0) {
            cancel();
        } else {
            QDBusConnection::systemBus().send(m_requests.at(i).call.createErrorReply(
                QStringLiteral("org.freedesktop.PolicyKit1.Error.Cancelled"), QStringLiteral("Cancelled")));
            m_requests.removeAt(i);
        }
        return;
    }
}

void PolkitAgent::finish(bool cancelled)
{
    const Request r = m_requests.takeFirst();
    QDBusConnection::systemBus().send(cancelled
        ? r.call.createErrorReply(QStringLiteral("org.freedesktop.PolicyKit1.Error.Cancelled"), QStringLiteral("Cancelled by the user"))
        : r.call.createReply());
    m_error.clear();
    emit requestChanged();
    emit stateChanged();
    if (!m_requests.isEmpty())
        emit requested();
}
