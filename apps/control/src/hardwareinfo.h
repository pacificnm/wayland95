#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

// The Hardware applet's Device Manager: the machine's devices sorted into
// categories, read from the kernel's /sys and /proc (no other libraries).
// PCI and USB devices are named from the system's pci.ids / usb.ids when
// they're installed.
//
// Exposed to QML as the singleton `Hardware`.
class HardwareInfo : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Hardware)
    QML_SINGLETON

public:
    explicit HardwareInfo(QObject *parent = nullptr);

    // { name, details: [[label, value], ...] } for the computer itself
    // the touchpads (names), from udev's device database
    Q_INVOKABLE QStringList touchpads() const;
    Q_INVOKABLE QVariantMap computer() const;
    // [{ id, name, icon, devices: [{ name, icon, problem, status,
    //    details: [[label, value], ...] }] }], categories in A-Z order
    Q_INVOKABLE QVariantList categories();

signals:
    // something was plugged in or removed
    void changed();

private:
    struct Ids {
        QHash<QString, QString> vendors;  // "8086" -> "Intel Corporation"
        QHash<QString, QString> devices;  // "8086:7d45" -> "Meteor Lake-P ..."
        bool loaded = false;
    };
    void loadIds(Ids &ids, const QStringList &candidates);
    QString pciName(const QString &vendor, const QString &device);
    QString pciVendor(const QString &vendor);
    QString usbName(const QString &vendor, const QString &product);
    QString usbVendor(const QString &vendor);
    // A readable name for the PCI or USB device a /sys path belongs to:
    // its own name if known, else "<maker> <what>" (newer hardware is
    // often missing from the ID databases). Empty if it's neither.
    QString deviceName(const QString &sysPath, const QString &what = QStringLiteral("device"));

    QFileSystemWatcher m_watcher;
    QTimer m_changeTimer;
    Ids m_pci;
    Ids m_usb;
};
