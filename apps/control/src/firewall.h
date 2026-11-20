#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// Firewall: Wayland 95's own nftables table (inet wayland95), which drops
// connections coming in unless they answer one of ours or match an
// exception. Its settings live in /etc/wayland95/firewall.conf, which
// anyone may read; session/wayland95-services fw-save (as root, through
// Services.admin) writes them, the rules and wayland95-firewall.service.
//
// Exposed to QML as the singleton `Firewall`.
class Firewall : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Firewall)
    QML_SINGLETON

    // { enabled, exceptions (allowed at all), ping, log, saved (the file
    //   exists), allow: [{ key, name, tcp, udp ("" for none), scope
    //   (any|local), on, builtIn }] }
    Q_PROPERTY(QVariantMap config READ config NOTIFY configChanged)
    // the rules are loaded now (wayland95-firewall.service is active)
    Q_PROPERTY(bool active READ active NOTIFY statusChanged)
    // nft is installed
    Q_PROPERTY(bool available READ available CONSTANT)
    // other firewalls that are on: "ufw", "firewalld"...
    Q_PROPERTY(QStringList others READ others NOTIFY statusChanged)

public:
    explicit Firewall(QObject *parent = nullptr);

    QVariantMap config() const { return m_config; }
    bool active() const { return m_active; }
    bool available() const { return m_available; }
    QStringList others() const { return m_others; }

    Q_INVOKABLE void refresh();
    // what a new firewall starts with
    Q_INVOKABLE QVariantMap defaults() const;
    // config in fw-save's KEY=VALUE lines
    Q_INVOKABLE QString toLines(const QVariantMap &config) const;
    // "" if `ports` are numbers or ranges, comma-separated; else what's wrong
    Q_INVOKABLE QString checkPorts(const QString &ports) const;
    // the kernel's log of blocked connections, in System Log
    Q_INVOKABLE void showBlocked() const;

signals:
    void configChanged();
    void statusChanged();

private:
    QVariantList builtIns() const;

    QVariantMap m_config;
    bool m_active = false;
    bool m_available = false;
    QStringList m_others;
    quint64 m_generation = 0;
};
