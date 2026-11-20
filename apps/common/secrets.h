#pragma once

#include <QDBusObjectPath>
#include <QMap>
#include <QObject>

#include <functional>

// Passwords in the desktop's keyring (the freedesktop Secret Service:
// Wayland 95's win95keyring, gnome-keyring, KeePassXC...), over D-Bus, never in a file. Each is found
// by its attributes (e.g. {application: wayland95-email, account: ID}). A
// locked keyring asks for its own password (its prompt) first.
namespace Secrets {

using Attributes = QMap<QString, QString>;

bool available();
// (replacing one with the same attributes)
void store(const Attributes &attributes, const QString &label, const QString &password,
           const std::function<void(bool ok, const QString &error)> &done);
void lookup(const Attributes &attributes, const std::function<void(bool found, const QString &password)> &done);
void remove(const Attributes &attributes);

// Email's: by account
inline Attributes emailAccount(const QString &account)
{
    return {{QStringLiteral("application"), QStringLiteral("wayland95-email")}, {QStringLiteral("account"), account}};
}
inline void store(const QString &account, const QString &label, const QString &password,
                  const std::function<void(bool ok, const QString &error)> &done)
{
    store(emailAccount(account), label, password, done);
}
inline void lookup(const QString &account, const std::function<void(bool found, const QString &password)> &done)
{
    lookup(emailAccount(account), done);
}
inline void remove(const QString &account) { remove(emailAccount(account)); }

} // namespace Secrets
