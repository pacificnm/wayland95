#include "batteryinfo.h"

#include <QDir>
#include <QFile>

namespace {

QString readValue(const QString &dir, const char *name)
{
    QFile f(dir + QLatin1Char('/') + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLatin1(f.readAll()).trimmed();
}

} // namespace

BatteryInfo::BatteryInfo(QObject *parent)
    : QObject(parent)
{
    // (the tests' pretend battery is looked at every second)
    const bool test = qEnvironmentVariableIsSet("WAYLAND95_NO_POWER_ACTIONS")
                      && qEnvironmentVariableIsSet("WAYLAND95_TEST_POWER_SUPPLY");
    m_timer.setInterval(test ? 1000 : 15 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &BatteryInfo::refresh);
    m_timer.start();
    refresh();
}

void BatteryInfo::refresh()
{
    // the automated tests' pretend battery (tests/run.sh), never otherwise
    const QString testRoot = qEnvironmentVariableIsSet("WAYLAND95_NO_POWER_ACTIONS")
                                 ? qEnvironmentVariable("WAYLAND95_TEST_POWER_SUPPLY") : QString();
    const QString root = testRoot.isEmpty() ? QStringLiteral("/sys/class/power_supply") : testRoot;
    const QStringList supplies = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    int batteries = 0;
    double percentSum = 0;
    double energyNow = 0, energyFull = 0, power = 0; // µWh / µW (or µAh / µA)
    bool charging = false, discharging = false, mains = false;
    double fullWh = 0, designWh = 0;
    QString model;
    for (const QString &name : supplies) {
        const QString dir = root + QLatin1Char('/') + name;
        const QString type = readValue(dir, "type");
        if (type == QLatin1String("Mains")) {
            mains = mains || readValue(dir, "online") == QLatin1String("1");
            continue;
        }
        // skip peripherals' batteries (a wireless mouse reports one too)
        if (type != QLatin1String("Battery") || readValue(dir, "scope") == QLatin1String("Device"))
            continue;
        ++batteries;
        const QString status = readValue(dir, "status");
        charging = charging || status == QLatin1String("Charging");
        discharging = discharging || status == QLatin1String("Discharging");

        bool ok = false;
        const double capacity = readValue(dir, "capacity").toDouble(&ok);
        QString now = readValue(dir, "energy_now"), full = readValue(dir, "energy_full"),
                rate = readValue(dir, "power_now");
        if (now.isEmpty()) { // batteries that report charge (µAh) instead of energy
            now = readValue(dir, "charge_now");
            full = readValue(dir, "charge_full");
            rate = readValue(dir, "current_now");
        }
        fullWh += readValue(dir, "energy_full").toDouble() / 1e6;
        designWh += readValue(dir, "energy_full_design").toDouble() / 1e6;
        if (model.isEmpty())
            model = (readValue(dir, "manufacturer") + QLatin1Char(' ') + readValue(dir, "model_name")).trimmed();
        energyNow += now.toDouble();
        energyFull += full.toDouble();
        power += qAbs(rate.toDouble());
        percentSum += ok ? capacity : (full.toDouble() > 0 ? 100.0 * now.toDouble() / full.toDouble() : 0);
    }

    const bool present = batteries > 0;
    const int percent = present ? qBound(0, qRound(percentSum / batteries), 100) : 0;
    int minutes = -1;
    if (present && power > 0) {
        if (discharging)
            minutes = qRound(energyNow / power * 60);
        else if (charging && energyFull > energyNow)
            minutes = qRound((energyFull - energyNow) / power * 60);
    }
    const bool pluggedIn = present && (mains || charging || !discharging);

    if (present == m_present && percent == m_percent && charging == m_charging && pluggedIn == m_pluggedIn
        && minutes == m_minutesLeft && qFuzzyCompare(fullWh + 1, m_fullWh + 1) && model == m_model)
        return;
    m_fullWh = fullWh;
    m_designWh = designWh;
    m_model = model;
    m_present = present;
    m_percent = percent;
    m_charging = charging;
    m_pluggedIn = pluggedIn;
    m_minutesLeft = minutes;
    emit changed();
}
