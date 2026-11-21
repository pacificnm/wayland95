#include "diskmanager.h"
#include "smartinfo.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>

#include <algorithm>
#include <utility>

namespace {
const QString Service = QStringLiteral("org.freedesktop.UDisks2");
const QString Root = QStringLiteral("/org/freedesktop/UDisks2");
const QString BlockIface = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString DriveIface = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString AtaIface = QStringLiteral("org.freedesktop.UDisks2.Drive.Ata");
const QString NvmeIface = QStringLiteral("org.freedesktop.UDisks2.NVMe.Controller");
const QString PartitionIface = QStringLiteral("org.freedesktop.UDisks2.Partition");
const QString TableIface = QStringLiteral("org.freedesktop.UDisks2.PartitionTable");
const QString FilesystemIface = QStringLiteral("org.freedesktop.UDisks2.Filesystem");
const QString LoopIface = QStringLiteral("org.freedesktop.UDisks2.Loop");

constexpr qint64 MiB = 1024 * 1024;
// formatting a big disk, or a password prompt, can take a while
constexpr int JobTimeout = 60 * 60 * 1000;

using Interfaces = QMap<QString, QVariantMap>;
using Objects = QMap<QString, Interfaces>;

QDBusConnection bus() { return QDBusConnection::systemBus(); }

// org.freedesktop.DBus.ObjectManager.GetManagedObjects: a{oa{sa{sv}}}
Objects managedObjects()
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, Root, QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                                       QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = bus().call(call);
    Objects objects;
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return objects;
    const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
    arg.beginMap();
    while (!arg.atEnd()) {
        QDBusObjectPath path;
        Interfaces interfaces;
        arg.beginMapEntry();
        arg >> path;
        arg.beginMap();
        while (!arg.atEnd()) {
            QString name;
            QVariantMap props;
            arg.beginMapEntry();
            arg >> name >> props;
            arg.endMapEntry();
            interfaces.insert(name, props);
        }
        arg.endMap();
        arg.endMapEntry();
        objects.insert(path.path(), interfaces);
    }
    arg.endMap();
    return objects;
}

QString objectPath(const QVariant &v) { return v.value<QDBusObjectPath>().path(); }

// "ay" with a trailing NUL
QString byteString(const QVariant &v)
{
    QByteArray b = v.toByteArray();
    while (b.endsWith('\0'))
        b.chop(1);
    return QString::fromLocal8Bit(b);
}

// Filesystem.MountPoints: aay
QStringList mountPoints(const QVariantMap &filesystem)
{
    QStringList points;
    const QVariant v = filesystem.value(QStringLiteral("MountPoints"));
    if (!v.canConvert<QDBusArgument>())
        return points;
    const QDBusArgument arg = v.value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        QByteArray b;
        arg >> b;
        while (b.endsWith('\0'))
            b.chop(1);
        points << QString::fromLocal8Bit(b);
    }
    arg.endArray();
    return points;
}

// as: (a D-Bus string array arrives as a QStringList, or as a QDBusArgument)
QStringList stringList(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>()) {
        QStringList list;
        v.value<QDBusArgument>() >> list;
        return list;
    }
    return v.toStringList();
}

// mount points that mean "the computer runs from this"
bool isSystemMount(const QString &point)
{
    static const QSet<QString> system{
        QStringLiteral("/"), QStringLiteral("/boot"), QStringLiteral("/boot/efi"), QStringLiteral("/efi"),
        QStringLiteral("/home"), QStringLiteral("/usr"), QStringLiteral("/var"), QStringLiteral("/opt"),
        QStringLiteral("/srv"), QStringLiteral("/run/live/medium"), QStringLiteral("/run/initramfs/live"),
    };
    return system.contains(point);
}

// /dev/sda2 or /dev/mapper/root -> sda2, dm-0 (the name in /sys/class/block)
QString kernelName(const QString &device)
{
    const QString real = QFileInfo(device).canonicalFilePath();
    return QFileInfo(real.isEmpty() ? device : real).fileName();
}

// a device and everything it's built from: an encrypted disk's partition,
// LVM's physical volumes, RAID's members (/sys/class/block/X/slaves)
void addWithSlaves(const QString &name, QSet<QString> &into, int depth = 0)
{
    if (name.isEmpty() || into.contains(name) || depth > 8)
        return;
    into.insert(name);
    const QStringList slaves = QDir(QStringLiteral("/sys/class/block/%1/slaves").arg(name))
                                   .entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    for (const QString &s : slaves)
        addWithSlaves(s, into, depth + 1);
}

bool hasHolders(const QString &name)
{
    return !QDir(QStringLiteral("/sys/class/block/%1/holders").arg(name))
                .entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System)
                .isEmpty();
}

// /proc/self/mounts escapes spaces and such as \040
QString unescapeMount(const QString &s)
{
    QString out;
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == u'\\' && i + 3 < s.size()) {
            bool ok = false;
            const int c = s.mid(i + 1, 3).toInt(&ok, 8);
            if (ok) {
                out += QChar(c);
                i += 3;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

QString fsName(const QString &type, const QString &version)
{
    if (type == QLatin1String("vfat"))
        return version.isEmpty() ? QStringLiteral("FAT") : version; // FAT12/16/32
    static const QHash<QString, QString> names{
        { QStringLiteral("ntfs"), QStringLiteral("NTFS") },
        { QStringLiteral("exfat"), QStringLiteral("exFAT") },
        { QStringLiteral("ext2"), QStringLiteral("ext2") },
        { QStringLiteral("ext3"), QStringLiteral("ext3") },
        { QStringLiteral("ext4"), QStringLiteral("ext4") },
        { QStringLiteral("btrfs"), QStringLiteral("Btrfs") },
        { QStringLiteral("xfs"), QStringLiteral("XFS") },
        { QStringLiteral("f2fs"), QStringLiteral("F2FS") },
        { QStringLiteral("swap"), QStringLiteral("Linux swap") },
        { QStringLiteral("crypto_LUKS"), QStringLiteral("Encrypted (LUKS)") },
        { QStringLiteral("LVM2_member"), QStringLiteral("LVM physical volume") },
        { QStringLiteral("linux_raid_member"), QStringLiteral("RAID member") },
        { QStringLiteral("iso9660"), QStringLiteral("CD image (ISO 9660)") },
        { QStringLiteral("udf"), QStringLiteral("UDF") },
        { QStringLiteral("hfsplus"), QStringLiteral("HFS+") },
        { QStringLiteral("apfs"), QStringLiteral("APFS") },
        { QStringLiteral("zfs_member"), QStringLiteral("ZFS") },
        { QStringLiteral("bcachefs"), QStringLiteral("bcachefs") },
    };
    return names.value(type, type);
}

QString partTypeName(const QString &table, const QString &type)
{
    const QString t = type.toLower();
    if (table == QLatin1String("gpt")) {
        static const QHash<QString, QString> gpt{
            { QStringLiteral("c12a7328-f81f-11d2-ba4b-00a0c93ec93b"), QStringLiteral("EFI System") },
            { QStringLiteral("21686148-6449-6e6f-744e-656564454649"), QStringLiteral("BIOS boot") },
            { QStringLiteral("0fc63daf-8483-4772-8e79-3d69d8477de4"), QStringLiteral("Linux filesystem") },
            { QStringLiteral("4f68bce3-e8cd-4db1-96e7-fbcaf984b709"), QStringLiteral("Linux root (x86-64)") },
            { QStringLiteral("933ac7e1-2eb4-4f13-b844-0e14e2aef915"), QStringLiteral("Linux home") },
            { QStringLiteral("bc13c2ff-59e6-4262-a352-b275fd6f7172"), QStringLiteral("Linux extended boot") },
            { QStringLiteral("0657fd6d-a4ab-43c4-84e5-0933c84b4f4f"), QStringLiteral("Linux swap") },
            { QStringLiteral("e6d6d379-f507-44c2-a23c-238f2a3df928"), QStringLiteral("Linux LVM") },
            { QStringLiteral("a19d880f-05fc-4d3b-a006-743f0f84911e"), QStringLiteral("Linux RAID") },
            { QStringLiteral("ca7d7ccb-63ed-4c53-861c-1742536059cc"), QStringLiteral("Linux LUKS") },
            { QStringLiteral("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7"), QStringLiteral("Microsoft basic data") },
            { QStringLiteral("e3c9e316-0b5c-4db8-817d-f92df00215ae"), QStringLiteral("Microsoft reserved") },
            { QStringLiteral("de94bba4-06d1-4d40-a16a-bfd50179d6ac"), QStringLiteral("Windows recovery") },
            { QStringLiteral("48465300-0000-11aa-aa11-00306543ecac"), QStringLiteral("Apple HFS+") },
            { QStringLiteral("7c3457ef-0000-11aa-aa11-00306543ecac"), QStringLiteral("Apple APFS") },
        };
        return gpt.value(t, t);
    }
    static const QHash<QString, QString> dos{
        { QStringLiteral("0x01"), QStringLiteral("FAT12") },
        { QStringLiteral("0x04"), QStringLiteral("FAT16") },
        { QStringLiteral("0x05"), QStringLiteral("Extended") },
        { QStringLiteral("0x06"), QStringLiteral("FAT16") },
        { QStringLiteral("0x07"), QStringLiteral("NTFS/exFAT") },
        { QStringLiteral("0x0b"), QStringLiteral("FAT32") },
        { QStringLiteral("0x0c"), QStringLiteral("FAT32 (LBA)") },
        { QStringLiteral("0x0e"), QStringLiteral("FAT16 (LBA)") },
        { QStringLiteral("0x0f"), QStringLiteral("Extended (LBA)") },
        { QStringLiteral("0x27"), QStringLiteral("Windows recovery") },
        { QStringLiteral("0x82"), QStringLiteral("Linux swap") },
        { QStringLiteral("0x83"), QStringLiteral("Linux") },
        { QStringLiteral("0x85"), QStringLiteral("Linux extended") },
        { QStringLiteral("0x8e"), QStringLiteral("Linux LVM") },
        { QStringLiteral("0xee"), QStringLiteral("GPT protective") },
        { QStringLiteral("0xef"), QStringLiteral("EFI System") },
        { QStringLiteral("0xfd"), QStringLiteral("Linux RAID") },
    };
    return dos.value(t, t);
}

// the partition type that goes with a filesystem, so Windows and other
// systems recognise it ("" leaves UDisks' default, Linux filesystem)
QString partTypeFor(const QString &table, const QString &fsType)
{
    const bool gpt = table == QLatin1String("gpt");
    if (fsType == QLatin1String("vfat"))
        return gpt ? QStringLiteral("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7") : QStringLiteral("0x0c");
    if (fsType == QLatin1String("ntfs") || fsType == QLatin1String("exfat"))
        return gpt ? QStringLiteral("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7") : QStringLiteral("0x07");
    if (fsType == QLatin1String("swap"))
        return gpt ? QStringLiteral("0657fd6d-a4ab-43c4-84e5-0933c84b4f4f") : QStringLiteral("0x82");
    if (fsType.isEmpty())
        return QString();
    return gpt ? QStringLiteral("0fc63daf-8483-4772-8e79-3d69d8477de4") : QStringLiteral("0x83");
}

// filesystems with owners: the new one's top folder becomes the user's
bool hasOwners(const QString &fsType)
{
    return fsType.startsWith(QLatin1String("ext")) || fsType == QLatin1String("xfs") || fsType == QLatin1String("btrfs")
        || fsType == QLatin1String("f2fs");
}

qint64 alignUp(qint64 v) { return (v + MiB - 1) / MiB * MiB; }
qint64 alignDown(qint64 v) { return v / MiB * MiB; }

bool dismissed(const QString &errorName)
{
    return errorName.endsWith(QLatin1String("NotAuthorizedDismissed")) || errorName.endsWith(QLatin1String("Cancelled"))
        || errorName == QLatin1String("org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain");
}
} // namespace

DiskManager::DiskManager(QObject *parent)
    : QObject(parent)
{
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(300);
    connect(&m_refreshTimer, &QTimer::timeout, this, &DiskManager::refresh);

    // disks and partitions coming, going and changing
    const QString om = QStringLiteral("org.freedesktop.DBus.ObjectManager");
    bus().connect(Service, Root, om, QStringLiteral("InterfacesAdded"), this, SLOT(scheduleRefresh()));
    bus().connect(Service, Root, om, QStringLiteral("InterfacesRemoved"), this, SLOT(scheduleRefresh()));
    bus().connect(Service, QString(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
                  this, SLOT(scheduleRefresh()));

    m_smartctl = !QStandardPaths::findExecutable(QStringLiteral("smartctl"), { QStringLiteral("/usr/sbin"), QStringLiteral("/sbin") }).isEmpty()
              || !QStandardPaths::findExecutable(QStringLiteral("smartctl")).isEmpty();

    // what this computer can format (UDisks needs mkfs.ntfs and so on)
    static const QList<QPair<QString, QString>> kinds{
        { QStringLiteral("ext4"), QStringLiteral("ext4 (Linux)") },
        { QStringLiteral("vfat"), QStringLiteral("FAT32 (any computer)") },
        { QStringLiteral("exfat"), QStringLiteral("exFAT (big files, any computer)") },
        { QStringLiteral("ntfs"), QStringLiteral("NTFS (Windows)") },
        { QStringLiteral("btrfs"), QStringLiteral("Btrfs (Linux)") },
        { QStringLiteral("xfs"), QStringLiteral("XFS (Linux)") },
        { QStringLiteral("swap"), QStringLiteral("Linux swap") },
    };
    for (const auto &k : kinds) {
        QDBusMessage can = QDBusMessage::createMethodCall(Service, Root + QStringLiteral("/Manager"),
                                                          QStringLiteral("org.freedesktop.UDisks2.Manager"), QStringLiteral("CanFormat"));
        can << k.first;
        const QDBusMessage reply = bus().call(can, QDBus::Block, 3000);
        bool available = k.first == QLatin1String("ext4") || k.first == QLatin1String("vfat");
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
            QString missing;
            arg.beginStructure();
            arg >> available >> missing;
            arg.endStructure();
        }
        if (available) {
            m_filesystems << QVariantMap{ { QStringLiteral("type"), k.first }, { QStringLiteral("label"), k.second } };
        }
    }
    refresh();
}

void DiskManager::scheduleRefresh()
{
    if (!m_refreshTimer.isActive())
        m_refreshTimer.start();
}

void DiskManager::readSystemDevices()
{
    m_systemNames.clear();
    m_heldNames.clear();
    QFile mounts(QStringLiteral("/proc/self/mounts"));
    if (mounts.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = mounts.readAll().split('\n');
        for (const QByteArray &line : lines) {
            const QList<QByteArray> f = line.split(' ');
            if (f.size() < 2 || !f[0].startsWith("/dev/"))
                continue;
            const QString name = kernelName(unescapeMount(QString::fromLocal8Bit(f[0])));
            addWithSlaves(name, m_heldNames);
            if (isSystemMount(unescapeMount(QString::fromLocal8Bit(f[1]))))
                addWithSlaves(name, m_systemNames);
        }
    }
    // swap in use: the computer would lose what's in it
    QFile swaps(QStringLiteral("/proc/swaps"));
    if (swaps.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = swaps.readAll().split('\n');
        for (int i = 1; i < lines.size(); ++i) {
            const QByteArray dev = lines[i].split(' ').value(0).split('\t').value(0);
            if (!dev.startsWith("/dev/"))
                continue;
            addWithSlaves(kernelName(unescapeMount(QString::fromLocal8Bit(dev))), m_systemNames);
            addWithSlaves(kernelName(unescapeMount(QString::fromLocal8Bit(dev))), m_heldNames);
        }
    }
}

void DiskManager::refresh()
{
    readSystemDevices();
    const Objects objects = managedObjects();

    // drives the computer runs from: one of their partitions is a system one
    QSet<QString> systemBlocks;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!it->contains(BlockIface))
            continue;
        const QString name = kernelName(byteString(it->value(BlockIface).value(QStringLiteral("Device"))));
        if (!m_systemNames.contains(name))
            continue;
        systemBlocks.insert(it.key());
        const QString table = objectPath(it->value(PartitionIface).value(QStringLiteral("Table")));
        if (!table.isEmpty())
            systemBlocks.insert(table);
    }

    static const bool testLoops = qEnvironmentVariableIsSet("WIN95_DISKS_LOOP");
    QVariantList list;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        // whole disks only
        if (!it->contains(BlockIface) || it->contains(PartitionIface))
            continue;
        const QVariantMap block = it->value(BlockIface);
        const qint64 size = block.value(QStringLiteral("Size")).toLongLong();
        if (size <= 0)
            continue;
        const QString device = byteString(block.value(QStringLiteral("Device")));
        const QString drivePath = objectPath(block.value(QStringLiteral("Drive")));
        const bool loop = it->contains(LoopIface);
        // for testing without spare disks: loop devices you set up yourself
        // (udisksctl loop-setup -f FILE)
        if (loop && !testLoops)
            continue;
        if (!loop && (drivePath.isEmpty() || drivePath == QLatin1String("/")))
            continue; // device-mapper, RAID arrays...: not disks of their own

        const QVariantMap drive = objects.value(drivePath).value(DriveIface);
        if (drive.value(QStringLiteral("Optical")).toBool())
            continue;
        const QString busName = drive.value(QStringLiteral("ConnectionBus")).toString();
        const int rpm = drive.value(QStringLiteral("RotationRate"), -1).toInt();
        const QString model = drive.value(QStringLiteral("Model")).toString().simplified();
        const QString vendor = drive.value(QStringLiteral("Vendor")).toString().simplified();
        const QString kname = kernelName(device);

        QString kind;
        if (loop)
            kind = QStringLiteral("virtual");
        else if (busName == QLatin1String("usb"))
            kind = QStringLiteral("usb");
        else if (busName == QLatin1String("sdio") || kname.startsWith(QLatin1String("mmcblk")))
            kind = QStringLiteral("card");
        else if (kname.startsWith(QLatin1String("nvme")))
            kind = QStringLiteral("nvme");
        else if (kname.startsWith(QLatin1String("vd")) || kname.startsWith(QLatin1String("xvd"))
                 || model.contains(QLatin1String("QEMU")) || model.contains(QLatin1String("VBOX"))
                 || model.contains(QLatin1String("Virtual"), Qt::CaseInsensitive))
            kind = QStringLiteral("virtual");
        else if (rpm == 0)
            kind = QStringLiteral("ssd");
        else
            kind = QStringLiteral("hdd");

        QString name = (vendor + u' ' + model).simplified();
        if (loop)
            name = QStringLiteral("Loop device (%1)").arg(byteString(it->value(LoopIface).value(QStringLiteral("BackingFile"))));
        if (name.isEmpty())
            name = kind == QLatin1String("usb") ? QStringLiteral("USB drive")
                 : kind == QLatin1String("card") ? QStringLiteral("Memory card")
                 : QStringLiteral("Disk");

        // S.M.A.R.T., as far as UDisks has read it (no password needed)
        QString health;
        double temperature = 0;
        const QVariantMap ata = objects.value(drivePath).value(AtaIface);
        const QVariantMap nvme = objects.value(drivePath).value(NvmeIface);
        if (ata.value(QStringLiteral("SmartEnabled")).toBool() && ata.value(QStringLiteral("SmartUpdated")).toULongLong() > 0) {
            if (ata.value(QStringLiteral("SmartFailing")).toBool())
                health = QStringLiteral("failing");
            else if (ata.value(QStringLiteral("SmartNumAttributesFailing")).toInt() > 0
                     || ata.value(QStringLiteral("SmartNumBadSectors")).toLongLong() > 0)
                health = QStringLiteral("warning");
            else
                health = QStringLiteral("ok");
            const double k = ata.value(QStringLiteral("SmartTemperature")).toDouble();
            if (k > 0)
                temperature = k - 273.15;
        } else if (nvme.value(QStringLiteral("SmartUpdated")).toULongLong() > 0) {
            const QStringList warnings = stringList(nvme.value(QStringLiteral("SmartCriticalWarning")));
            health = warnings.isEmpty() ? QStringLiteral("ok")
                   : (warnings.contains(QLatin1String("degraded")) || warnings.contains(QLatin1String("readonly")))
                     ? QStringLiteral("failing") : QStringLiteral("warning");
            const double k = nvme.value(QStringLiteral("SmartTemperature")).toDouble();
            if (k > 0)
                temperature = k - 273.15;
        }
        // smartctl's word, once it's been asked, is the last word
        if (m_smartSeen.contains(device)) {
            const QVariantMap seen = m_smartSeen.value(device);
            if (!seen.value(QStringLiteral("health")).toString().isEmpty())
                health = seen.value(QStringLiteral("health")).toString();
            if (seen.value(QStringLiteral("temperature")).toDouble() > 0)
                temperature = seen.value(QStringLiteral("temperature")).toDouble();
        }

        list << QVariantMap{
            { QStringLiteral("path"), it.key() },
            { QStringLiteral("drive"), drivePath },
            { QStringLiteral("device"), device },
            { QStringLiteral("name"), name },
            { QStringLiteral("model"), model },
            { QStringLiteral("serial"), drive.value(QStringLiteral("Serial")).toString() },
            { QStringLiteral("size"), size },
            { QStringLiteral("kind"), kind },
            { QStringLiteral("rpm"), rpm },
            { QStringLiteral("removable"), drive.value(QStringLiteral("Removable")).toBool()
                                           || drive.value(QStringLiteral("MediaRemovable")).toBool() },
            { QStringLiteral("canPowerOff"), drive.value(QStringLiteral("CanPowerOff")).toBool() },
            { QStringLiteral("system"), systemBlocks.contains(it.key()) },
            { QStringLiteral("table"), it->value(TableIface).value(QStringLiteral("Type")).toString() },
            { QStringLiteral("health"), health },
            { QStringLiteral("temperature"), qRound(temperature) },
        };
    }
    // the computer's own disks first, then by device name
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap ma = a.toMap(), mb = b.toMap();
        const bool ra = ma.value(QStringLiteral("removable")).toBool(), rb = mb.value(QStringLiteral("removable")).toBool();
        if (ra != rb)
            return !ra;
        return ma.value(QStringLiteral("device")).toString() < mb.value(QStringLiteral("device")).toString();
    });
    if (list != m_drives) {
        m_drives = list;
        emit drivesChanged();
    }
    ++m_revision;
    emit revisionChanged();
}

QVariantList DiskManager::layout(const QString &drivePath) const
{
    const Objects objects = managedObjects();
    const Interfaces disk = objects.value(drivePath);
    const QVariantMap diskBlock = disk.value(BlockIface);
    const qint64 diskSize = diskBlock.value(QStringLiteral("Size")).toLongLong();
    const QString table = disk.value(TableIface).value(QStringLiteral("Type")).toString();
    const bool hasTable = disk.contains(TableIface);

    auto info = [this, &table](const QString &path, const Interfaces &obj) {
        const QVariantMap block = obj.value(BlockIface);
        const QVariantMap part = obj.value(PartitionIface);
        const QString device = byteString(block.value(QStringLiteral("Device")));
        const QString name = kernelName(device);
        const QString fsType = block.value(QStringLiteral("IdType")).toString();
        const QStringList points = mountPoints(obj.value(FilesystemIface));
        qint64 used = -1;
        if (!points.isEmpty()) {
            const QStorageInfo storage(points.constFirst());
            if (storage.isValid() && storage.bytesTotal() > 0)
                used = storage.bytesTotal() - storage.bytesFree();
        }
        const QString partType = part.value(QStringLiteral("Type")).toString();
        bool system = m_systemNames.contains(name);
        return QVariantMap{
            { QStringLiteral("path"), path },
            { QStringLiteral("device"), device },
            { QStringLiteral("number"), part.value(QStringLiteral("Number")).toInt() },
            { QStringLiteral("offset"), part.value(QStringLiteral("Offset")).toLongLong() },
            { QStringLiteral("size"), block.value(QStringLiteral("Size")).toLongLong() },
            { QStringLiteral("fsType"), fsType },
            { QStringLiteral("fsName"), fsName(fsType, block.value(QStringLiteral("IdVersion")).toString()) },
            { QStringLiteral("usage"), block.value(QStringLiteral("IdUsage")).toString() },
            { QStringLiteral("label"), block.value(QStringLiteral("IdLabel")).toString() },
            { QStringLiteral("partName"), part.value(QStringLiteral("Name")).toString() },
            { QStringLiteral("partType"), partType },
            { QStringLiteral("partTypeName"), partType.isEmpty() ? QString() : partTypeName(table, partType) },
            { QStringLiteral("mountPoints"), points },
            { QStringLiteral("mounted"), !points.isEmpty() },
            { QStringLiteral("used"), used },
            { QStringLiteral("system"), system },
            { QStringLiteral("inUse"), m_heldNames.contains(name) || hasHolders(name) },
            { QStringLiteral("canMount"), obj.contains(FilesystemIface) },
            // MBR's active flag (0x80), GPT's legacy BIOS bootable (bit 2)
            { QStringLiteral("bootable"), table == QLatin1String("dos")
                                          ? (part.value(QStringLiteral("Flags")).toULongLong() & 0x80) != 0
                                          : (part.value(QStringLiteral("Flags")).toULongLong() & 0x4) != 0 },
        };
    };
    auto freeSpace = [](qint64 from, qint64 to, bool logical) -> QVariantMap {
        const qint64 start = alignUp(std::max(from, MiB));
        const qint64 end = alignDown(to);
        // (the alignment gaps partitioning tools leave, a MB or two, aren't worth offering)
        if (end - start < 8 * MiB)
            return {};
        return QVariantMap{
            { QStringLiteral("kind"), QStringLiteral("free") },
            { QStringLiteral("offset"), start },
            { QStringLiteral("size"), end - start },
            { QStringLiteral("logical"), logical },
        };
    };

    QVariantList out;
    if (!hasTable) {
        // a filesystem on the whole disk (some USB sticks), or nothing at all
        QVariantMap whole = info(drivePath, disk);
        whole.insert(QStringLiteral("kind"), whole.value(QStringLiteral("usage")).toString().isEmpty()
                                                 ? QStringLiteral("empty") : QStringLiteral("whole"));
        whole.insert(QStringLiteral("offset"), 0);
        out << whole;
        return out;
    }

    QList<QVariantMap> top, logical;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (objectPath(it->value(PartitionIface).value(QStringLiteral("Table"))) != drivePath)
            continue;
        QVariantMap p = info(it.key(), *it);
        const QVariantMap part = it->value(PartitionIface);
        if (part.value(QStringLiteral("IsContainer")).toBool()) {
            p.insert(QStringLiteral("kind"), QStringLiteral("extended"));
            top << p;
        } else if (part.value(QStringLiteral("IsContained")).toBool()) {
            p.insert(QStringLiteral("kind"), QStringLiteral("logical"));
            logical << p;
        } else {
            p.insert(QStringLiteral("kind"), QStringLiteral("partition"));
            top << p;
        }
    }
    auto byOffset = [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("offset")).toLongLong() < b.value(QStringLiteral("offset")).toLongLong();
    };
    std::sort(top.begin(), top.end(), byOffset);
    std::sort(logical.begin(), logical.end(), byOffset);

    // GPT keeps a copy of itself in the disk's last 33 sectors
    const qint64 limit = table == QLatin1String("gpt") ? diskSize - 33 * 512 : diskSize;
    qint64 cursor = 0;
    for (const QVariantMap &p : std::as_const(top)) {
        const qint64 offset = p.value(QStringLiteral("offset")).toLongLong();
        const qint64 end = offset + p.value(QStringLiteral("size")).toLongLong();
        const QVariantMap gap = freeSpace(cursor, offset, false);
        if (!gap.isEmpty())
            out << gap;
        const int index = out.size();
        out << p;
        if (p.value(QStringLiteral("kind")).toString() == QLatin1String("extended")) {
            // the logical partitions inside it, and the space between them
            // (each needs a little room before it for its own table entry)
            qint64 inner = offset;
            bool held = false;
            for (const QVariantMap &l : std::as_const(logical)) {
                const qint64 lo = l.value(QStringLiteral("offset")).toLongLong();
                if (lo < offset || lo >= end)
                    continue;
                const QVariantMap g = freeSpace(inner + MiB, lo - MiB, true);
                if (!g.isEmpty())
                    out << g;
                out << l;
                held = held || l.value(QStringLiteral("system")).toBool() || l.value(QStringLiteral("inUse")).toBool();
                inner = lo + l.value(QStringLiteral("size")).toLongLong();
            }
            const QVariantMap g = freeSpace(inner + MiB, end, true);
            if (!g.isEmpty())
                out << g;
            if (held) {
                // what's in it is in use: so is the extended partition
                QVariantMap ext = out[index].toMap();
                ext.insert(QStringLiteral("inUse"), true);
                out[index] = ext;
            }
        }
        cursor = std::max(cursor, end);
    }
    const QVariantMap gap = freeSpace(cursor, limit, false);
    if (!gap.isEmpty())
        out << gap;
    return out;
}

// ---- jobs ------------------------------------------------------------------

DiskManager::Step DiskManager::call(const QString &path, const QString &iface, const QString &method,
                                    const QVariantList &args, const QString &status) const
{
    QDBusMessage m = QDBusMessage::createMethodCall(Service, path, iface, method);
    m.setArguments(args);
    m.setInteractiveAuthorizationAllowed(true);
    return Step{ m, status, nullptr };
}

void DiskManager::run(const QList<Step> &steps, const QString &success)
{
    if (m_busy)
        return;
    m_steps = steps;
    m_success = success;
    m_busy = true;
    next();
}

void DiskManager::next()
{
    if (m_steps.isEmpty()) {
        m_busy = false;
        m_status.clear();
        emit busyChanged();
        refresh();
        emit finished(true, m_success);
        return;
    }
    const Step step = m_steps.takeFirst();
    m_status = step.status;
    emit busyChanged();
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(step.call, JobTimeout), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, step] {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        QString error;
        if (reply.type() == QDBusMessage::ErrorMessage) {
            // the password box's Cancel: nothing to say
            error = dismissed(reply.errorName()) ? QString() : reply.errorMessage();
            if (error.isEmpty() && !dismissed(reply.errorName()))
                error = reply.errorName();
            m_steps.clear();
            m_busy = false;
            m_status.clear();
            emit busyChanged();
            refresh();
            // UDisks' messages start "Error formatting /dev/sdb1: ..."
            emit finished(false, error);
            return;
        }
        if (step.check) {
            error = step.check(reply);
            if (!error.isEmpty()) {
                m_steps.clear();
                m_busy = false;
                m_status.clear();
                emit busyChanged();
                refresh();
                emit finished(false, error);
                return;
            }
        }
        next();
    });
}

// unmount everything mounted from a partition, or from every partition of a drive
QList<DiskManager::Step> DiskManager::unmountSteps(const QString &blockPath, bool wholeDrive) const
{
    QList<Step> steps;
    const Objects objects = managedObjects();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!it->contains(FilesystemIface))
            continue;
        const bool match = it.key() == blockPath
                        || (wholeDrive && objectPath(it->value(PartitionIface).value(QStringLiteral("Table"))) == blockPath);
        if (!match || mountPoints(it->value(FilesystemIface)).isEmpty())
            continue;
        steps << call(it.key(), FilesystemIface, QStringLiteral("Unmount"), { QVariantMap{} },
                      QStringLiteral("Unmounting %1...").arg(byteString(it->value(BlockIface).value(QStringLiteral("Device")))));
    }
    return steps;
}

// why a partition (or with forDrive, a whole drive) mustn't be erased, or ""
QString DiskManager::refusal(const QString &blockPath, bool forDrive) const
{
    const Objects objects = managedObjects();
    auto check = [this](const QString &path, const Interfaces &obj) -> QString {
        const QString device = byteString(obj.value(BlockIface).value(QStringLiteral("Device")));
        const QString name = kernelName(device);
        if (m_systemNames.contains(name))
            return QStringLiteral("%1 holds the system this computer is running, so it can't be changed while it's running. "
                                  "Start the computer from a Live USB drive to change it.").arg(device);
        // held by something UDisks can't just unmount: an open encrypted
        // disk, LVM, RAID, swap in use
        if (hasHolders(name) || (m_heldNames.contains(name) && mountPoints(obj.value(FilesystemIface)).isEmpty()))
            return QStringLiteral("%1 is in use (by an unlocked encrypted disk, LVM, RAID or swap). Stop using it first.").arg(device);
        Q_UNUSED(path);
        return QString();
    };
    QString why = check(blockPath, objects.value(blockPath));
    if (!why.isEmpty() || !forDrive)
        return why;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (objectPath(it->value(PartitionIface).value(QStringLiteral("Table"))) != blockPath)
            continue;
        why = check(it.key(), *it);
        if (!why.isEmpty())
            return why;
    }
    return QString();
}

void DiskManager::mount(const QString &path)
{
    Step s = call(path, FilesystemIface, QStringLiteral("Mount"), { QVariantMap{} }, QStringLiteral("Mounting..."));
    s.check = [this](const QDBusMessage &reply) {
        m_success = QStringLiteral("Mounted at %1.").arg(reply.arguments().value(0).toString());
        return QString();
    };
    run({ s }, QString());
}

void DiskManager::unmount(const QString &path)
{
    readSystemDevices();
    const QString device = byteString(managedObjects().value(path).value(BlockIface).value(QStringLiteral("Device")));
    if (m_systemNames.contains(kernelName(device))) {
        emit finished(false, QStringLiteral("%1 holds the system this computer is running, so it stays mounted.").arg(device));
        return;
    }
    run({ call(path, FilesystemIface, QStringLiteral("Unmount"), { QVariantMap{} }, QStringLiteral("Unmounting...")) },
        QStringLiteral("Unmounted. It's safe to remove now."));
}

void DiskManager::format(const QString &path, const QString &fsType, const QString &label)
{
    readSystemDevices();
    const QString why = refusal(path, false);
    if (!why.isEmpty()) {
        emit finished(false, why);
        return;
    }
    const QString labelProblem = checkLabel(fsType, label);
    if (!labelProblem.isEmpty()) {
        emit finished(false, labelProblem);
        return;
    }
    const Objects objects = managedObjects();
    const bool isPartition = objects.value(path).contains(PartitionIface);
    QVariantMap options{ { QStringLiteral("label"), label } };
    if (hasOwners(fsType))
        options.insert(QStringLiteral("take-ownership"), true);
    QList<Step> steps = unmountSteps(path, false);
    if (isPartition) {
        // the partition's type follows (Microsoft basic data for NTFS...)
        options.insert(QStringLiteral("update-partition-type"), true);
    }
    steps << call(path, BlockIface, QStringLiteral("Format"), { fsType, options }, QStringLiteral("Formatting..."));
    run(steps, QStringLiteral("Formatting is done."));
}

void DiskManager::deletePartition(const QString &path)
{
    readSystemDevices();
    const QString why = refusal(path, false);
    if (!why.isEmpty()) {
        emit finished(false, why);
        return;
    }
    QList<Step> steps = unmountSteps(path, false);
    steps << call(path, PartitionIface, QStringLiteral("Delete"), { QVariantMap{} }, QStringLiteral("Deleting the partition..."));
    run(steps, QStringLiteral("The partition was deleted."));
}

void DiskManager::createPartition(const QString &drivePath, qint64 offset, qint64 size, const QString &fsType,
                                  const QString &label)
{
    const QString labelProblem = checkLabel(fsType, label);
    if (!labelProblem.isEmpty()) {
        emit finished(false, labelProblem);
        return;
    }
    const Objects objects = managedObjects();
    const QString table = objects.value(drivePath).value(TableIface).value(QStringLiteral("Type")).toString();
    if (table.isEmpty()) {
        emit finished(false, QStringLiteral("This disk has no partition table. Make one first (Disk > New Partition Table)."));
        return;
    }
    // inside an extended partition, the new one is a logical partition
    QVariantMap options;
    if (table == QLatin1String("dos")) {
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            const QVariantMap part = it->value(PartitionIface);
            if (objectPath(part.value(QStringLiteral("Table"))) != drivePath || !part.value(QStringLiteral("IsContainer")).toBool())
                continue;
            const qint64 o = part.value(QStringLiteral("Offset")).toLongLong();
            if (offset >= o && offset < o + part.value(QStringLiteral("Size")).toLongLong())
                options.insert(QStringLiteral("partition-type"), QStringLiteral("logical"));
        }
    }
    QVariantMap formatOptions{ { QStringLiteral("label"), label } };
    if (hasOwners(fsType))
        formatOptions.insert(QStringLiteral("take-ownership"), true);
    const QString type = partTypeFor(table, fsType);
    QList<Step> steps;
    if (fsType.isEmpty()) {
        steps << call(drivePath, TableIface, QStringLiteral("CreatePartition"),
                      { QVariant::fromValue<quint64>(quint64(offset)), QVariant::fromValue<quint64>(quint64(size)), type,
                        QString(), options },
                      QStringLiteral("Making the partition..."));
    } else {
        steps << call(drivePath, TableIface, QStringLiteral("CreatePartitionAndFormat"),
                      { QVariant::fromValue<quint64>(quint64(offset)), QVariant::fromValue<quint64>(quint64(size)), type,
                        QString(), options, fsType, formatOptions },
                      QStringLiteral("Making the partition and formatting it..."));
    }
    run(steps, QStringLiteral("The new partition is ready."));
}

void DiskManager::newPartitionTable(const QString &drivePath, const QString &type)
{
    if (type != QLatin1String("gpt") && type != QLatin1String("dos"))
        return;
    readSystemDevices();
    const QString why = refusal(drivePath, true);
    if (!why.isEmpty()) {
        emit finished(false, why);
        return;
    }
    QList<Step> steps = unmountSteps(drivePath, true);
    steps << call(drivePath, BlockIface, QStringLiteral("Format"), { type, QVariantMap{} },
                  QStringLiteral("Making a new partition table..."));
    run(steps, QStringLiteral("The disk is empty now, with a new partition table. Make partitions in its free space."));
}

void DiskManager::setLabel(const QString &path, const QString &label)
{
    const Objects objects = managedObjects();
    const QString problem = checkLabel(objects.value(path).value(BlockIface).value(QStringLiteral("IdType")).toString(), label);
    if (!problem.isEmpty()) {
        emit finished(false, problem);
        return;
    }
    run({ call(path, FilesystemIface, QStringLiteral("SetLabel"), { label, QVariantMap{} }, QStringLiteral("Changing the label...")) },
        QStringLiteral("The label was changed."));
}

void DiskManager::check(const QString &path)
{
    readSystemDevices();
    if (m_systemNames.contains(kernelName(byteString(managedObjects().value(path).value(BlockIface).value(QStringLiteral("Device")))))) {
        emit finished(false, QStringLiteral("This is in use by the running system, so it can't be checked now. "
                                            "It's checked when the computer starts."));
        return;
    }
    QList<Step> steps = unmountSteps(path, false);
    Step s = call(path, FilesystemIface, QStringLiteral("Check"), { QVariantMap{} }, QStringLiteral("Checking for problems..."));
    s.check = [this](const QDBusMessage &reply) {
        m_success = reply.arguments().value(0).toBool()
                  ? QStringLiteral("No problems were found.")
                  : QStringLiteral("Problems were found. Use Repair to fix them (back up what's on it first, if you can).");
        return QString();
    };
    steps << s;
    run(steps, QString());
}

void DiskManager::repair(const QString &path)
{
    readSystemDevices();
    const QString why = refusal(path, false);
    if (!why.isEmpty()) {
        emit finished(false, why);
        return;
    }
    QList<Step> steps = unmountSteps(path, false);
    Step s = call(path, FilesystemIface, QStringLiteral("Repair"), { QVariantMap{} }, QStringLiteral("Repairing..."));
    s.check = [this](const QDBusMessage &reply) {
        m_success = reply.arguments().value(0).toBool() ? QStringLiteral("Repair is done: the filesystem is in order now.")
                                                        : QStringLiteral("Not everything could be repaired.");
        return QString();
    };
    steps << s;
    run(steps, QString());
}

void DiskManager::powerOff(const QString &drivePath)
{
    readSystemDevices();
    const QString why = refusal(drivePath, true);
    if (!why.isEmpty()) {
        emit finished(false, why);
        return;
    }
    const Objects objects = managedObjects();
    const QString drive = objectPath(objects.value(drivePath).value(BlockIface).value(QStringLiteral("Drive")));
    QList<Step> steps = unmountSteps(drivePath, true);
    steps << call(drive, DriveIface, QStringLiteral("PowerOff"), { QVariantMap{} }, QStringLiteral("Switching the drive off..."));
    run(steps, QStringLiteral("The drive is switched off. You can unplug it now."));
}

void DiskManager::openFolder(const QString &mountPoint) const
{
    QProcess::startDetached(QStringLiteral("win95files"), { mountPoint });
}

QString DiskManager::checkLabel(const QString &fsType, const QString &label) const
{
    if (label.isEmpty())
        return QString();
    int max = 0;
    if (fsType == QLatin1String("vfat"))
        max = 11;
    else if (fsType == QLatin1String("exfat"))
        max = 15;
    else if (fsType.startsWith(QLatin1String("ext")) || fsType == QLatin1String("swap"))
        max = 16;
    else if (fsType == QLatin1String("xfs"))
        max = 12;
    else if (fsType == QLatin1String("ntfs"))
        max = 128;
    else
        max = 255;
    // ext's limit is in bytes
    const int length = fsType.startsWith(QLatin1String("ext")) ? int(label.toUtf8().size()) : int(label.size());
    if (length > max)
        return QStringLiteral("The label is too long: %1 can have %2 characters at most.").arg(fsName(fsType, QString())).arg(max);
    if (fsType == QLatin1String("vfat")) {
        static const QRegularExpression bad(QStringLiteral("[\"*/:<>?\\\\|+,.;=\\[\\]]"));
        if (label.contains(bad))
            return QStringLiteral("A FAT32 label can't have any of these: \" * / : < > ? \\ | + , . ; = [ ]");
    }
    return QString();
}

// ---- S.M.A.R.T. --------------------------------------------------------------

QString DiskManager::helperPath() const
{
#ifdef WAYLAND95_SERVICES_HELPER_SRC
    // run from a build tree: its own sources' helper, which may know
    // commands (smart) an older installed one doesn't
    if (QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/CMakeCache.txt"))
        && QFileInfo::exists(QStringLiteral(WAYLAND95_SERVICES_HELPER_SRC)))
        return QStringLiteral(WAYLAND95_SERVICES_HELPER_SRC);
#endif
#ifdef WAYLAND95_SERVICES_HELPER
    if (QFileInfo::exists(QStringLiteral(WAYLAND95_SERVICES_HELPER)))
        return QStringLiteral(WAYLAND95_SERVICES_HELPER);
#endif
#ifdef WAYLAND95_SERVICES_HELPER_SRC
    return QStringLiteral(WAYLAND95_SERVICES_HELPER_SRC);
#else
    return QString();
#endif
}

void DiskManager::runHelper(const QStringList &args, std::function<void(int, const QByteArray &, const QString &)> done)
{
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty()) {
        done(1, QByteArray(), QStringLiteral("pkexec isn't installed (sudo apt install pkexec)."));
        return;
    }
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [p, done](int code, QProcess::ExitStatus) {
        const QByteArray out = p->readAllStandardOutput();
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        p->deleteLater();
        done(code, out, err);
    });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        done(1, QByteArray(), p->errorString());
    });
    p->start(pkexec, QStringList{ helperPath() } + args);
}

void DiskManager::readSmart(const QString &device)
{
    if (!m_smartctl) {
        emit smartReady(device, QVariantMap{
            { QStringLiteral("ok"), false },
            { QStringLiteral("error"), QStringLiteral("smartctl isn't installed, so the disk's health report can't be read.\n\n"
                                                      "Install it with Control Panel > Add/Remove Programs (smartmontools), "
                                                      "or: sudo apt install smartmontools") },
        });
        return;
    }
    runHelper({ QStringLiteral("smart"), device }, [this, device](int code, const QByteArray &out, const QString &err) {
        QVariantMap report;
        // pkexec: 126 the password box was cancelled, 127 not allowed
        if (out.trimmed().isEmpty()) {
            report = QVariantMap{
                { QStringLiteral("ok"), false },
                { QStringLiteral("cancelled"), code == 126 },
                { QStringLiteral("error"), code == 126 ? QString()
                                         : err.isEmpty() ? QStringLiteral("The disk's health report couldn't be read.") : err },
            };
        } else {
            report = parseSmartctl(out);
            // W95_DISKS_SMART_DUMP=file: smartctl's own JSON, kept (for tests)
            const QString dump = qEnvironmentVariable("W95_DISKS_SMART_DUMP");
            if (!dump.isEmpty()) {
                QFile f(dump);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    f.write(out);
            }
        }
        if (report.value(QStringLiteral("ok")).toBool()) {
            m_smartSeen.insert(device, QVariantMap{
                { QStringLiteral("health"), report.value(QStringLiteral("health")) },
                { QStringLiteral("temperature"), report.value(QStringLiteral("temperature")) },
            });
            refresh();
        }
        emit smartReady(device, report);
    });
}

void DiskManager::startSelfTest(const QString &device, const QString &type)
{
    if (type != QLatin1String("short") && type != QLatin1String("long") && type != QLatin1String("abort"))
        return;
    runHelper({ QStringLiteral("smart-test"), device, type }, [this, device, type](int code, const QByteArray &out, const QString &err) {
        if (out.trimmed().isEmpty()) {
            emit selfTestStarted(device, false, code == 126 ? QString() : (err.isEmpty() ? QStringLiteral("The test couldn't be started.") : err));
            return;
        }
        const QVariantMap r = parseSmartctl(out);
        const QString message = smartctlMessage(out);
        // exit status bit 2: a command to the disk failed
        const bool ok = !(r.value(QStringLiteral("exitStatus")).toInt() & 0x7);
        emit selfTestStarted(device, ok,
                             !message.isEmpty() ? message
                             : type == QLatin1String("abort") ? QStringLiteral("The test was stopped.")
                                                              : QStringLiteral("The test has started. It runs inside the disk, "
                                                                               "and you can keep using the computer meanwhile."));
    });
}
