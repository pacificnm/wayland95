#pragma once

#include <QDBusObjectPath>
#include <QObject>

#include <functional>

// Passwords in the desktop's keyring (the freedesktop Secret Service:
// gnome-keyring, KeePassXC...), over D-Bus, never in a file. Each is found
// by its attributes {application: wayland95-email, account: ID}. A locked
// keyring asks for its own password (its prompt) first.
namespace Secrets {

bool available();
void store(const QString &account, const QString &label, const QString &password,
           const std::function<void(bool ok, const QString &error)> &done);
void lookup(const QString &account, const std::function<void(bool found, const QString &password)> &done);
void remove(const QString &account);

} // namespace Secrets
