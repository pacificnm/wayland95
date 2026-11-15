#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <QWaylandSeat>

// Applies the Control Panel's keyboard settings (layout, repeat delay and
// rate) to a compositor seat, so every client gets them.
//
//   SeatSettings {
//       seat: compositor.defaultSeat
//       layout: Ui.Wayland95Settings.keyboardLayout
//       ...
//   }
class SeatSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QWaylandSeat *seat READ seat WRITE setSeat NOTIFY seatChanged)
    Q_PROPERTY(QString layout MEMBER m_layout WRITE setLayout NOTIFY layoutChanged)
    Q_PROPERTY(int repeatDelay MEMBER m_repeatDelay WRITE setRepeatDelay NOTIFY repeatChanged)
    Q_PROPERTY(int repeatRate MEMBER m_repeatRate WRITE setRepeatRate NOTIFY repeatChanged)

public:
    explicit SeatSettings(QObject *parent = nullptr);

    QWaylandSeat *seat() const { return m_seat; }
    void setSeat(QWaylandSeat *seat);
    void setLayout(const QString &layout);
    void setRepeatDelay(int ms);
    void setRepeatRate(int rate);

signals:
    void seatChanged();
    void layoutChanged();
    void repeatChanged();

private:
    void apply();

    QPointer<QWaylandSeat> m_seat;
    QString m_layout;
    QString m_defaultLayout; // what the seat had before we touched it
    int m_repeatDelay = 600;
    int m_repeatRate = 25;
};
