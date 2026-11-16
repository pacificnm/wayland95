#pragma once

// Turning the screens off and on (wlr-output-power-management): Control
// Panel > Power's "turn off the display". Exposed to QML as the singleton
// `DisplayPower`.

#include <QHash>
#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>

#include "qwayland-wlr-output-power-management-unstable-v1.h"

class QScreen;

class OutputPowerManager : public QWaylandClientExtensionTemplate<OutputPowerManager>,
                           public QtWayland::zwlr_output_power_manager_v1
{
    Q_OBJECT
public:
    OutputPowerManager() : QWaylandClientExtensionTemplate<OutputPowerManager>(1) { initialize(); }
};

class DisplayPower : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool on READ on NOTIFY onChanged)

public:
    explicit DisplayPower(QObject *parent = nullptr);
    ~DisplayPower() override;

    bool on() const { return m_on; }
    // every screen
    Q_INVOKABLE void setOn(bool on);

Q_SIGNALS:
    void onChanged();

private:
    OutputPowerManager *m_manager;
    QHash<QScreen *, QtWayland::zwlr_output_power_v1 *> m_outputs;
    bool m_on = true;
};
