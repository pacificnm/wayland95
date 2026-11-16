#include "screensaverservice.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QtDebug>

namespace {

class FreedesktopScreenSaver : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")
public:
    explicit FreedesktopScreenSaver(ScreenSaverService *service) : QObject(service), m_service(service)
    {
        connect(service, &ScreenSaverService::activeChanged, this, [this] { emit ActiveChanged(m_service->active()); });
    }

public Q_SLOTS:
    uint Inhibit(const QString &application, const QString &reason)
    {
        return m_service->addInhibitor(calledFromDBus() ? message().service() : QString(), application, reason);
    }
    void UnInhibit(uint cookie) { m_service->removeInhibitor(cookie); }
    bool GetActive() { return m_service->active(); }
    bool SetActive(bool active)
    {
        emit m_service->activateRequested(active);
        return true;
    }
    uint GetActiveTime()
    {
        return m_service->active() ? uint((QDateTime::currentMSecsSinceEpoch() - m_service->activeSince()) / 1000) : 0;
    }
    uint GetSessionIdleTime() { return 0; } // the compositor knows; nothing asks
    void SimulateUserActivity() { emit m_service->activityReported(); }

Q_SIGNALS:
    void ActiveChanged(bool active);

private:
    ScreenSaverService *m_service;
};

class Wayland95ScreenSaver : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wayland95.ScreenSaver")
public:
    explicit Wayland95ScreenSaver(ScreenSaverService *service) : QObject(service), m_service(service) {}
public Q_SLOTS:
    void Preview() { emit m_service->previewRequested(); }
private:
    ScreenSaverService *m_service;
};

} // namespace

ScreenSaverService::ScreenSaverService(QObject *parent) : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    if (bus.registerService(QStringLiteral("org.wayland95.ScreenSaver")))
        bus.registerObject(QStringLiteral("/org/wayland95/ScreenSaver"), new Wayland95ScreenSaver(this),
                           QDBusConnection::ExportAllSlots);
    if (bus.registerService(QStringLiteral("org.freedesktop.ScreenSaver"))) {
        auto *freedesktop = new FreedesktopScreenSaver(this);
        const auto options = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals;
        bus.registerObject(QStringLiteral("/org/freedesktop/ScreenSaver"), freedesktop, options);
        bus.registerObject(QStringLiteral("/ScreenSaver"), freedesktop, options);
        m_watcher = new QDBusServiceWatcher(this);
        m_watcher->setConnection(bus);
        m_watcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &owner) {
            bool changed = false;
            for (auto it = m_inhibitors.begin(); it != m_inhibitors.end();) {
                if (it.value() == owner) {
                    it = m_inhibitors.erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
            m_watcher->removeWatchedService(owner);
            if (changed) {
                qInfo().noquote() << "Screen saver: a program that held it off went away";
                if (m_inhibitors.isEmpty())
                    emit inhibitedChanged();
            }
        });
    } else {
        qInfo("Screen saver: another desktop answers org.freedesktop.ScreenSaver");
    }
}

void ScreenSaverService::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    m_activeSince = QDateTime::currentMSecsSinceEpoch();
    emit activeChanged();
}

uint ScreenSaverService::addInhibitor(const QString &owner, const QString &application, const QString &reason)
{
    const uint cookie = m_nextCookie++;
    const bool was = inhibited();
    m_inhibitors.insert(cookie, owner);
    if (!owner.isEmpty() && m_watcher)
        m_watcher->addWatchedService(owner);
    qInfo().noquote() << "Screen saver: held off by" << application << (reason.isEmpty() ? QString() : "(" + reason + ")");
    if (!was)
        emit inhibitedChanged();
    return cookie;
}

void ScreenSaverService::removeInhibitor(uint cookie)
{
    if (!m_inhibitors.remove(cookie))
        return;
    qInfo() << "Screen saver: no longer held off by cookie" << cookie;
    if (m_inhibitors.isEmpty())
        emit inhibitedChanged();
}

#include "screensaverservice.moc"
