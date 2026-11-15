#include "seatsettings.h"

#include <QWaylandKeyboard>
#include <QWaylandKeymap>
#include <QWaylandSeat>

SeatSettings::SeatSettings(QObject *parent)
    : QObject(parent)
{
}

void SeatSettings::setSeat(QWaylandSeat *seat)
{
    if (seat == m_seat)
        return;
    m_seat = seat;
    if (m_seat && m_seat->keymap())
        m_defaultLayout = m_seat->keymap()->layout();
    emit seatChanged();
    apply();
}

void SeatSettings::setLayout(const QString &layout)
{
    if (layout == m_layout)
        return;
    m_layout = layout;
    emit layoutChanged();
    apply();
}

void SeatSettings::setRepeatDelay(int ms)
{
    if (ms == m_repeatDelay)
        return;
    m_repeatDelay = ms;
    emit repeatChanged();
    apply();
}

void SeatSettings::setRepeatRate(int rate)
{
    if (rate == m_repeatRate)
        return;
    m_repeatRate = rate;
    emit repeatChanged();
    apply();
}

void SeatSettings::apply()
{
    if (!m_seat)
        return;
    if (QWaylandKeyboard *keyboard = m_seat->keyboard()) {
        keyboard->setRepeatDelay(quint32(qMax(0, m_repeatDelay)));
        keyboard->setRepeatRate(quint32(qMax(1, m_repeatRate)));
    }
    if (QWaylandKeymap *keymap = m_seat->keymap()) {
        // an empty layout means "back to whatever the seat started with"
        const QString layout = m_layout.isEmpty() ? m_defaultLayout : m_layout;
        if (keymap->layout() != layout)
            keymap->setLayout(layout);
    }
}
