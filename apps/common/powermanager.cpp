#include "powermanager.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QGuiApplication>

#include <tuple>

namespace {

const QString Logind = QStringLiteral("org.freedesktop.login1");
const QString LogindPath = QStringLiteral("/org/freedesktop/login1");
const QString LogindManager = QStringLiteral("org.freedesktop.login1.Manager");
const QString Properties = QStringLiteral("org.freedesktop.DBus.Properties");

QDBusConnection bus() { return QDBusConnection::systemBus(); }

QString readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed() : QString();
}

QVariant getProperty(const QString &service, const QString &path, const QString &interface, const QString &name)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, Properties, QStringLiteral("Get"));
    call << interface << name;
    const QDBusMessage reply = bus().call(call, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

bool logindCan(const QString &what)
{
    const QDBusMessage call = QDBusMessage::createMethodCall(Logind, LogindPath, LogindManager, QStringLiteral("Can") + what);
    const QDBusReply<QString> reply = bus().call(call, QDBus::Block, 2000);
    // "challenge" means it needs a password; with no way to ask, treat as no
    return reply.isValid() && reply.value() == QLatin1String("yes");
}

} // namespace

PowerManager::PowerManager(QObject *parent)
    : QObject(parent)
{
    // Wayland 95 is the desktop when it runs straight on the screen
    // hardware. Not otherwise: a copy started from a terminal inside the
    // session (or inside another desktop) runs as a window and mustn't sleep
    // the computer or take over the lid, whatever its environment says.
    const QString platform = QGuiApplication::platformName();
    m_primary = platform == QLatin1String("eglfs") || platform == QLatin1String("linuxfb");

    m_canSuspend = logindCan(QStringLiteral("Suspend"));
    m_canHibernate = logindCan(QStringLiteral("Hibernate"));

    const QString chassis = getProperty(QStringLiteral("org.freedesktop.hostname1"), QStringLiteral("/org/freedesktop/hostname1"),
                                        QStringLiteral("org.freedesktop.hostname1"), QStringLiteral("Chassis")).toString();
    m_hasLid = QDir(QStringLiteral("/proc/acpi/button/lid")).exists()
               || chassis == QLatin1String("laptop") || chassis == QLatin1String("convertible");
    m_isLaptop = m_hasLid || chassis == QLatin1String("tablet");
    m_lidClosed = getProperty(Logind, LogindPath, LogindManager, QStringLiteral("LidClosed")).toBool();
    bus().connect(Logind, LogindPath, Properties, QStringLiteral("PropertiesChanged"), this,
                  SLOT(onLogindProperties(QString,QVariantMap,QStringList)));

    // the built-in screen's backlight: firmware/platform interfaces control
    // it best; a "raw" one is the fallback
    int best = -1;
    for (const QString &name : QDir(QStringLiteral("/sys/class/backlight")).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System)) {
        const QString type = readText(QStringLiteral("/sys/class/backlight/") + name + QStringLiteral("/type"));
        const int rank = type == QLatin1String("firmware") ? 2 : type == QLatin1String("platform") ? 1 : 0;
        if (rank > best) {
            best = rank;
            m_backlight = name;
        }
    }
    if (!m_backlight.isEmpty())
        m_maxBrightness = readText(QStringLiteral("/sys/class/backlight/") + m_backlight + QStringLiteral("/max_brightness")).toInt();
    if (m_maxBrightness <= 0)
        m_backlight.clear();
    readBrightness();

    // power-profiles-daemon, under its current name or the older one
    for (const auto &[service, path, interface] : { std::tuple{ QStringLiteral("org.freedesktop.UPower.PowerProfiles"),
                                                                QStringLiteral("/org/freedesktop/UPower/PowerProfiles"),
                                                                QStringLiteral("org.freedesktop.UPower.PowerProfiles") },
                                                    std::tuple{ QStringLiteral("net.hadess.PowerProfiles"),
                                                                QStringLiteral("/net/hadess/PowerProfiles"),
                                                                QStringLiteral("net.hadess.PowerProfiles") } }) {
        if (getProperty(service, path, interface, QStringLiteral("ActiveProfile")).isValid()) {
            m_profileService = service;
            m_profilePath = path;
            m_profileInterface = interface;
            bus().connect(service, path, Properties, QStringLiteral("PropertiesChanged"), this,
                          SLOT(onProfileProperties(QString,QVariantMap,QStringList)));
            break;
        }
    }
    readProfiles();

    // brightness keys handled by firmware, or another program, change it too
    m_poll.setInterval(3000);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        readBrightness();
        const bool closed = getProperty(Logind, LogindPath, LogindManager, QStringLiteral("LidClosed")).toBool();
        if (closed != m_lidClosed) {
            m_lidClosed = closed;
            emit lidClosedChanged();
        }
    });
    m_poll.start();
}

void PowerManager::onLogindProperties(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != LogindManager || !changed.contains(QStringLiteral("LidClosed")))
        return;
    const bool closed = changed.value(QStringLiteral("LidClosed")).toBool();
    if (closed != m_lidClosed) {
        m_lidClosed = closed;
        emit lidClosedChanged();
    }
}

void PowerManager::onProfileProperties(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface == m_profileInterface && (changed.contains(QStringLiteral("ActiveProfile")) || changed.contains(QStringLiteral("Profiles"))))
        readProfiles();
}

void PowerManager::readProfiles()
{
    if (m_profileService.isEmpty())
        return;
    QStringList profiles;
    const QVariant list = getProperty(m_profileService, m_profilePath, m_profileInterface, QStringLiteral("Profiles"));
    if (list.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = list.value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            QVariantMap entry;
            arg >> entry;
            profiles << entry.value(QStringLiteral("Profile")).toString();
        }
        arg.endArray();
    }
    const QString active = getProperty(m_profileService, m_profilePath, m_profileInterface, QStringLiteral("ActiveProfile")).toString();
    if (profiles != m_profiles || active != m_profile) {
        m_profiles = profiles;
        m_profile = active;
        emit powerProfileChanged();
    }
}

void PowerManager::setPowerProfile(const QString &profile)
{
    if (m_profileService.isEmpty() || !m_profiles.contains(profile))
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(m_profileService, m_profilePath, Properties, QStringLiteral("Set"));
    call << m_profileInterface << QStringLiteral("ActiveProfile") << QVariant::fromValue(QDBusVariant(profile));
    bus().asyncCall(call);
}

void PowerManager::readBrightness()
{
    if (m_backlight.isEmpty())
        return;
    const QString dir = QStringLiteral("/sys/class/backlight/") + m_backlight;
    QString value = readText(dir + QStringLiteral("/actual_brightness"));
    if (value.isEmpty())
        value = readText(dir + QStringLiteral("/brightness"));
    const int percent = qBound(0, qRound(100.0 * value.toInt() / m_maxBrightness), 100);
    if (percent != m_brightness) {
        m_brightness = percent;
        emit brightnessChanged();
    }
}

void PowerManager::setBrightness(int percent)
{
    if (m_backlight.isEmpty())
        return;
    percent = qBound(1, percent, 100);
    // logind lets the user's own session set it, no root needed
    QDBusMessage call = QDBusMessage::createMethodCall(Logind, QStringLiteral("/org/freedesktop/login1/session/auto"),
                                                       QStringLiteral("org.freedesktop.login1.Session"),
                                                       QStringLiteral("SetBrightness"));
    call << QStringLiteral("backlight") << m_backlight << uint(qMax(1, qRound(m_maxBrightness * percent / 100.0)));
    bus().asyncCall(call);
    if (percent != m_brightness) {
        m_brightness = percent;
        emit brightnessChanged();
    }
}

void PowerManager::changeBrightness(int deltaPercent)
{
    setBrightness(m_brightness + deltaPercent);
}

void PowerManager::logindCall(const QString &method)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Logind, LogindPath, LogindManager, method);
    call << false; // not interactive: polkit decides, no password prompt
    bus().asyncCall(call);
}

void PowerManager::suspend() { if (m_canSuspend) logindCall(QStringLiteral("Suspend")); }
void PowerManager::hibernate() { if (m_canHibernate) logindCall(QStringLiteral("Hibernate")); }
void PowerManager::powerOff() { logindCall(QStringLiteral("PowerOff")); }
void PowerManager::reboot() { logindCall(QStringLiteral("Reboot")); }

void PowerManager::logOff()
{
    const QString session = qEnvironmentVariable("XDG_SESSION_ID");
    if (session.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(Logind, LogindPath, LogindManager, QStringLiteral("TerminateSession"));
    call << session;
    bus().asyncCall(call);
}

bool PowerManager::restartRequired() const
{
    return QFile::exists(QStringLiteral("/run/reboot-required"));
}

bool PowerManager::takeOverPowerKeys()
{
    if (!m_primary)
        return false;
    if (m_inhibitor.isValid())
        return true;
    QDBusMessage call = QDBusMessage::createMethodCall(Logind, LogindPath, LogindManager, QStringLiteral("Inhibit"));
    call << QStringLiteral("handle-power-key:handle-suspend-key:handle-hibernate-key:handle-lid-switch")
         << QStringLiteral("Wayland 95")
         << QStringLiteral("Wayland 95 handles the power keys and the lid (Control Panel > Power)")
         << QStringLiteral("block");
    const QDBusReply<QDBusUnixFileDescriptor> reply = bus().call(call, QDBus::Block, 3000);
    if (!reply.isValid())
        return false;
    // held for as long as we run; logind takes over again if we quit
    m_inhibitor = reply.value();
    return m_inhibitor.isValid();
}
