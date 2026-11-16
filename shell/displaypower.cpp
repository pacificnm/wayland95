#include "displaypower.h"

#include <QGuiApplication>
#include <QScreen>
#include <cstdio>

DisplayPower::DisplayPower(QObject *parent)
    : QObject(parent)
    , m_manager(new OutputPowerManager)
{
    m_manager->setParent(this);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        if (auto *power = m_outputs.take(screen)) {
            power->destroy();
            delete power;
        }
    });
}

DisplayPower::~DisplayPower()
{
    for (auto *power : std::as_const(m_outputs)) {
        power->destroy();
        delete power;
    }
}

void DisplayPower::setOn(bool on)
{
    if (!m_manager->isActive()) {
        std::fprintf(stderr, "win95shell: the compositor can't turn the display %s\n", on ? "on" : "off");
        return;
    }
    for (QScreen *screen : QGuiApplication::screens()) {
        auto *native = screen->nativeInterface<QNativeInterface::QWaylandScreen>();
        if (!native || !native->output())
            continue;
        auto *&power = m_outputs[screen];
        if (!power)
            power = new QtWayland::zwlr_output_power_v1(m_manager->get_output_power(native->output()));
        power->set_mode(on ? QtWayland::zwlr_output_power_v1::mode_on : QtWayland::zwlr_output_power_v1::mode_off);
    }
    std::fprintf(stderr, "win95shell: display %s\n", on ? "on" : "off");
    if (on != m_on) {
        m_on = on;
        emit onChanged();
    }
}
