#include "updatecheck.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QtDebug>

namespace {
const QString PkService = QStringLiteral("org.freedesktop.PackageKit");
const QString PkPath = QStringLiteral("/org/freedesktop/PackageKit");
const QString PkTransaction = QStringLiteral("org.freedesktop.PackageKit.Transaction");
// pk-enum.h
constexpr quint64 FilterNone = 1 << 1;
constexpr uint InfoSecurity = 8, InfoBlocked = 9;
constexpr uint ExitSuccess = 1;

QDBusConnection bus() { return QDBusConnection::systemBus(); }
}

UpdateCheck::UpdateCheck(QObject *parent)
    : QObject(parent)
{
    const QString platform = QGuiApplication::platformName();
    const bool session = qEnvironmentVariableIsSet("WAYLAND95_SESSION")
                      && (platform == QLatin1String("eglfs") || platform == QLatin1String("linuxfb"));
    QDBusConnectionInterface *iface = bus().isConnected() ? bus().interface() : nullptr;
    const bool packageKit = iface && (iface->isServiceRegistered(PkService).value()
                                      || iface->activatableServiceNames().value().contains(PkService));
    m_enabled = packageKit && (session || qEnvironmentVariableIsSet("WAYLAND95_CHECK_UPDATES"));

    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &UpdateCheck::check);
    if (m_enabled) {
        // MEMBER properties are set from QML after construction: start then
        QTimer::singleShot(0, this, [this] {
            m_timer.start(m_firstDelay);
        });
    }
}

void UpdateCheck::scheduleNext()
{
    m_timer.start(m_interval);
}

void UpdateCheck::check()
{
    if (!m_enabled || m_busy)
        return;
    m_busy = true;
    m_found = m_foundSecurity = 0;
    m_foundDesktop = false;
    m_foundVersion.clear();
    // the latest lists first (allowed without a password)
    m_step = QStringLiteral("refresh");
    transaction(QStringLiteral("RefreshCache"), { false });
}

void UpdateCheck::transaction(const QString &method, const QVariantList &args)
{
    QDBusMessage create = QDBusMessage::createMethodCall(PkService, PkPath, PkService, QStringLiteral("CreateTransaction"));
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(create), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, method, args] {
        watcher->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError()) {
            qInfo().noquote() << "Update check: PackageKit isn't answering (" + reply.error().message() + ")";
            m_busy = false;
            scheduleNext();
            return;
        }
        m_transaction = reply.value().path();
        bus().connect(PkService, m_transaction, PkTransaction, QStringLiteral("Package"), this,
                      SLOT(onPackage(uint, QString, QString)));
        bus().connect(PkService, m_transaction, PkTransaction, QStringLiteral("Finished"), this,
                      SLOT(onFinished(uint, uint)));
        // never ask for anything: this runs unattended
        QDBusMessage hints = QDBusMessage::createMethodCall(PkService, m_transaction, PkTransaction, QStringLiteral("SetHints"));
        hints << QStringList{ QStringLiteral("interactive=false"), QStringLiteral("background=true") };
        bus().asyncCall(hints);
        QDBusMessage call = QDBusMessage::createMethodCall(PkService, m_transaction, PkTransaction, method);
        call.setArguments(args);
        auto *started = new QDBusPendingCallWatcher(bus().asyncCall(call), this);
        connect(started, &QDBusPendingCallWatcher::finished, this, [this, started] {
            started->deleteLater();
            if (started->isError())
                onFinished(0, 0);
        });
    });
}

void UpdateCheck::onPackage(uint info, const QString &id, const QString &)
{
    if (m_step != QLatin1String("list") || info == InfoBlocked)
        return;
    ++m_found;
    if (info == InfoSecurity)
        ++m_foundSecurity;
    // package ids are "name;version;arch;repo"
    const QStringList parts = id.split(QLatin1Char(';'));
    if (parts.value(0) == QLatin1String("wayland95")) {
        m_foundDesktop = true;
        m_foundVersion = parts.value(1);
    }
}

void UpdateCheck::onFinished(uint exit, uint)
{
    if (!m_transaction.isEmpty()) {
        bus().disconnect(PkService, m_transaction, PkTransaction, QStringLiteral("Package"), this,
                         SLOT(onPackage(uint, QString, QString)));
        bus().disconnect(PkService, m_transaction, PkTransaction, QStringLiteral("Finished"), this,
                         SLOT(onFinished(uint, uint)));
        m_transaction.clear();
    }
    if (m_step == QLatin1String("refresh")) {
        // even if refreshing failed (offline), the lists we have still say
        // something
        m_step = QStringLiteral("list");
        transaction(QStringLiteral("GetUpdates"), { QVariant::fromValue(FilterNone) });
        return;
    }
    m_busy = false;
    m_step.clear();
    if (exit == ExitSuccess) {
        m_count = m_found;
        m_security = m_foundSecurity;
        m_desktop = m_foundDesktop;
        m_desktopVersion = m_foundVersion;
        emit checked();
    }
    scheduleNext();
}
