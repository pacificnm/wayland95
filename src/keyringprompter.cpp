#include "keyringprompter.h"

#include <QDBusConnectionInterface>
#include <QtDebug>

namespace {
const QString Service = QStringLiteral("org.wayland95.KeyringPrompter");
const QString Path = QStringLiteral("/org/wayland95/KeyringPrompter");
const QString Canceled = QStringLiteral("org.wayland95.KeyringPrompter.Canceled");
const QString Keyring = QStringLiteral("org.freedesktop.secrets");
}

KeyringPrompterAdaptor::KeyringPrompterAdaptor(KeyringPrompter *prompter)
    : QObject(prompter)
    , m_prompter(prompter)
{
}

QString KeyringPrompterAdaptor::AskPassword(const QString &mode, bool retry)
{
    setDelayedReply(true);
    m_prompter->request(message(), mode, retry);
    return {};
}

void KeyringPrompterAdaptor::Cancel()
{
    m_prompter->cancelRequest(message().service());
}

KeyringPrompter::KeyringPrompter(QObject *parent)
    : QObject(parent)
    , m_bus(QDBusConnection::sessionBus())
{
    if (!m_bus.isConnected())
        return;
    if (!m_bus.registerObject(Path, new KeyringPrompterAdaptor(this), QDBusConnection::ExportAllSlots))
        return;
    // nested inside another Wayland 95: the outer one asks
    if (!m_bus.registerService(Service))
        qInfo() << "Keyring: another desktop asks for the password list's password";
}

void KeyringPrompter::request(const QDBusMessage &call, const QString &mode, bool retry)
{
    // only the keyring itself (whoever owns org.freedesktop.secrets) may
    // ask: nothing else gets to pop up a password box that looks like ours
    const QString owner = m_bus.interface()->serviceOwner(Keyring).value();
    if (owner.isEmpty() || owner != call.service()) {
        m_bus.send(call.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"),
                                         QStringLiteral("Only the keyring asks for its password")));
        return;
    }
    if (m_pending) {
        // (the keyring asks one at a time; a new one replaces the old)
        m_bus.send(m_call.createErrorReply(Canceled, QStringLiteral("Replaced")));
    }
    m_pending = true;
    m_call = call;
    m_mode = mode == QLatin1String("create") ? mode : QStringLiteral("unlock");
    m_retry = retry;
    emit requestChanged();
    emit requested();
}

void KeyringPrompter::provide(const QString &password)
{
    if (!m_pending)
        return;
    m_bus.send(m_call.createReply(password));
    finish();
}

void KeyringPrompter::cancel()
{
    if (!m_pending)
        return;
    m_bus.send(m_call.createErrorReply(Canceled, QStringLiteral("Canceled by the user")));
    finish();
}

void KeyringPrompter::cancelRequest(const QString &caller)
{
    if (m_pending && caller == m_call.service()) {
        m_bus.send(m_call.createErrorReply(Canceled, QStringLiteral("Canceled by the program")));
        finish();
    }
}

void KeyringPrompter::finish()
{
    m_pending = false;
    m_call = QDBusMessage();
    emit requestChanged();
}
