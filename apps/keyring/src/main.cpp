// win95keyring - Wayland 95's keyring: the freedesktop Secret Service
// (org.freedesktop.secrets) that programs keep passwords in, encrypted in
// your password list (~/.local/share/wayland95/passwords.pwl) and unlocked
// by your login password. D-Bus starts it when something asks.
//
//   win95keyring                    the service (D-Bus starts it)
//   win95keyring --login            the login's password from standard
//                                   input: unlocks the list (from
//                                   pam_wayland95_keyring at login)
//   win95keyring --change-password  old and new, each ending in a NUL, from
//                                   standard input (pam_wayland95_keyring,
//                                   when your login password changes)
//   win95keyring --lock             locks it until it's next needed
//
// Qt Core and D-Bus and OpenSSL's libcrypto only: no display needed (the
// desktop shows its prompts).
#include "crypto.h"
#include "passwordlist.h"
#include "secretservice.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThread>
#include <QtDebug>

#include <sys/prctl.h>
#include <unistd.h>

namespace {
const QString Service = QStringLiteral("org.freedesktop.secrets");
const QString Path = QStringLiteral("/org/freedesktop/secrets");
const QString KeyringIface = QStringLiteral("org.wayland95.Keyring");

QByteArray readStdin()
{
    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly))
        return {};
    QByteArray data = in.read(64 * 1024);
    return data;
}

// a call to the running service (D-Bus starts it if need be); at login the
// session bus may take a moment to come up
int callService(const QString &method, const QVariantList &args)
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        const QString name = QStringLiteral("win95keyring-%1").arg(attempt);
        QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, name);
        if (bus.isConnected()) {
            QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, KeyringIface, method);
            call.setArguments(args);
            const QDBusMessage r = bus.call(call, QDBus::Block, 120000);
            const QString error = r.errorName();
            QDBusConnection::disconnectFromBus(name);
            if (r.type() == QDBusMessage::ReplyMessage)
                return (r.arguments().isEmpty() || r.arguments().at(0).toBool()) ? 0 : 1;
            // another keyring has the name (gnome-keyring): it doesn't know us
            if (error == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")
                || error == QLatin1String("org.freedesktop.DBus.Error.UnknownInterface")) {
                qWarning().noquote() << "win95keyring: another keyring answers for" << Service << "(" + r.errorMessage() + ")";
                return 2;
            }
            qWarning().noquote() << "win95keyring:" << r.errorMessage();
        } else {
            QDBusConnection::disconnectFromBus(name);
        }
        QThread::msleep(500);
    }
    return 1;
}
} // namespace

int main(int argc, char *argv[])
{
    // secrets stay out of core dumps, and other programs can't read this
    // one's memory (ptrace) even as the same user
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("win95keyring"));
    const QStringList args = app.arguments().mid(1);
    const QString mode = args.value(0);

    if (mode == QLatin1String("--login")) {
        QByteArray password = readStdin();
        const int rc = callService(QStringLiteral("LoginPassword"), { QString::fromUtf8(password) });
        Crypto::wipe(password);
        return rc;
    }
    if (mode == QLatin1String("--change-password")) {
        QByteArray data = readStdin();
        const QList<QByteArray> parts = data.split('\0');
        Crypto::wipe(data);
        if (parts.size() < 2)
            return 1;
        return callService(QStringLiteral("ChangePassword"),
                           { QString::fromUtf8(parts.at(0)), QString::fromUtf8(parts.at(1)) });
    }
    if (mode == QLatin1String("--lock"))
        return callService(QStringLiteral("Lock"), {});
    if (!mode.isEmpty()) {
        fprintf(stderr, "usage: win95keyring [--login | --change-password | --lock]\n");
        return 2;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCritical() << "win95keyring: no session bus";
        return 1;
    }

    // (WAYLAND95_KEYRING_FILE: the tests' own)
    QString file = qEnvironmentVariable("WAYLAND95_KEYRING_FILE");
    if (file.isEmpty())
        file = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
               + QStringLiteral("/wayland95/passwords.pwl");
    PasswordList list(file);
    list.load();
    // a list with no password (made that way) opens by itself
    if (list.state() == PasswordList::Locked)
        list.unlock(QByteArray());

    SecretService service(&list, bus);
    if (!bus.registerVirtualObject(Path, &service, QDBusConnection::SubPath)) {
        qCritical() << "win95keyring: couldn't register" << Path;
        return 1;
    }
    if (!bus.registerService(Service)) {
        qCritical().noquote() << "win95keyring: another keyring is running (" + Service + " is taken)";
        return 1;
    }
    qInfo().noquote() << "win95keyring: answering for" << Service << "with" << file
                      << (list.state() == PasswordList::NoFile   ? "(none yet)"
                          : list.state() == PasswordList::Locked ? "(locked)"
                                                                 : "(unlocked)");
    return app.exec();
}
