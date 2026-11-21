#pragma once

#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

// Disk Manager's engine: the computer's disks and their partitions, and the
// jobs done to them (mount, unmount, format, new and deleted partitions, a
// new partition table, labels, checking and repairing), all through UDisks2,
// which asks for the administrator password (polkit, answered by Wayland
// 95's own dialog) when it's needed. The program never runs as root.
//
// S.M.A.R.T.: a summary for every disk without a password (what UDisks
// already reads: ATA's failing flag and temperature, NVMe's warnings), and
// the whole report and self-tests from smartctl, run as root through
// wayland95-services (pkexec).
//
// The disks the computer runs from (/, /boot, /home... and swap, through
// encryption and LVM too) are marked "system": they can't be formatted,
// deleted or given a new partition table.
//
// Exposed to QML as the singleton `Disks`.
class DiskManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Disks)
    QML_SINGLETON

    // [{ path (UDisks block object), drive (UDisks drive object), device,
    //    name, model, serial, size, kind ("hdd", "ssd", "nvme", "usb",
    //    "card", "virtual"), rpm, removable, canPowerOff, system, table
    //    ("gpt", "dos", ""), health ("ok", "failing", "warning", "" unknown),
    //    temperature (°C, 0 unknown) }]
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
    // a job running (UDisks working, or waiting for the password)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY busyChanged)
    // [{ type ("ext4"...), label ("ext4 (Linux)"), maxLabel }] that this
    // computer can make (UDisks has the tool for)
    Q_PROPERTY(QVariantList filesystems READ filesystems CONSTANT)
    Q_PROPERTY(bool smartctlInstalled READ smartctlInstalled CONSTANT)
    // goes up whenever anything on any disk changes: layout() is worth asking again
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    explicit DiskManager(QObject *parent = nullptr);

    QVariantList drives() const { return m_drives; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QVariantList filesystems() const { return m_filesystems; }
    bool smartctlInstalled() const { return m_smartctl; }
    int revision() const { return m_revision; }

    // A drive's layout, in order on the disk: [{ kind ("partition",
    // "extended", "logical", "free", "whole" (a filesystem on the whole
    // disk), "empty" (nothing recognised)), path, device, number, offset,
    // size, fsType, fsVersion, usage ("filesystem", "crypto", "other"...),
    // label, partName, partType, partTypeName, mountPoints, mounted,
    // used (bytes, -1 unknown), system, inUse (held by encryption, LVM, RAID
    // or swap), canMount, bootable }]
    Q_INVOKABLE QVariantList layout(const QString &drivePath) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void mount(const QString &path);
    Q_INVOKABLE void unmount(const QString &path);
    // erase a partition (or a whole-disk filesystem) and make `fsType` on it
    Q_INVOKABLE void format(const QString &path, const QString &fsType, const QString &label);
    Q_INVOKABLE void deletePartition(const QString &path);
    // a new partition in the drive's free space: `offset` and `size` in bytes
    Q_INVOKABLE void createPartition(const QString &drivePath, qint64 offset, qint64 size,
                                     const QString &fsType, const QString &label);
    // erase the whole drive: an empty partition table ("gpt" or "dos")
    Q_INVOKABLE void newPartitionTable(const QString &drivePath, const QString &type);
    Q_INVOKABLE void setLabel(const QString &path, const QString &label);
    Q_INVOKABLE void check(const QString &path);
    Q_INVOKABLE void repair(const QString &path);
    // a USB drive: unmounted and switched off, safe to pull out
    Q_INVOKABLE void powerOff(const QString &drivePath);
    Q_INVOKABLE void openFolder(const QString &mountPoint) const;

    // a filesystem label that's too long or has odd characters: why, or ""
    Q_INVOKABLE QString checkLabel(const QString &fsType, const QString &label) const;

    // S.M.A.R.T., from smartctl (asks for the password): `smartReady`
    // brings the report (see smartinfo.h)
    Q_INVOKABLE void readSmart(const QString &device);
    Q_INVOKABLE void startSelfTest(const QString &device, const QString &type); // short, long, abort

signals:
    void drivesChanged();
    void revisionChanged();
    void busyChanged();
    // a job ended: ok, and what to tell
    void finished(bool ok, const QString &message);
    void smartReady(const QString &device, const QVariantMap &report);
    void selfTestStarted(const QString &device, bool ok, const QString &message);

private slots:
    void scheduleRefresh();

private:
    struct Step {
        QDBusMessage call;
        QString status;
        // the reply: "" when it's fine, else what to say instead of success
        std::function<QString(const QDBusMessage &)> check;
    };
    void run(const QList<Step> &steps, const QString &success);
    void next();
    QList<Step> unmountSteps(const QString &blockPath, bool wholeDrive) const;
    Step call(const QString &path, const QString &iface, const QString &method, const QVariantList &args,
              const QString &status) const;
    QString refusal(const QString &blockPath, bool forDrive) const;
    void runHelper(const QStringList &args, std::function<void(int, const QByteArray &, const QString &)> done);
    void readSystemDevices();
    QString helperPath() const;

    QVariantList m_drives;
    QVariantList m_filesystems;
    bool m_smartctl = false;
    bool m_busy = false;
    int m_revision = 0;
    QString m_status;
    QList<Step> m_steps;
    QString m_success;
    QTimer m_refreshTimer;

    // kernel names (sda2, nvme0n1p3, dm-0) the system runs from, and under
    // anything at all (mounted somewhere, swap, an open encrypted disk, LVM)
    QSet<QString> m_systemNames;
    QSet<QString> m_heldNames;
    // the last smartctl report's verdict for each device: { health, temperature }
    QHash<QString, QVariantMap> m_smartSeen;
};
