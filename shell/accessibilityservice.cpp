#include "accessibilityservice.h"

#include <QDBusConnection>

AccessibilityService::AccessibilityService(QObject *parent) : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.registerService(QStringLiteral("org.wayland95.Accessibility")))
        bus.registerObject(QStringLiteral("/org/wayland95/Accessibility"), this, QDBusConnection::ExportScriptableSlots
                           | QDBusConnection::ExportAllSlots);
}
