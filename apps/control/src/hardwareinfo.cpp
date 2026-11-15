#include "hardwareinfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

QByteArray readBytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QStringList entries(const QString &dir)
{
    return QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
}

QString driverOf(const QString &sysPath)
{
    const QString link = QFileInfo(sysPath + QStringLiteral("/driver")).symLinkTarget();
    return link.isEmpty() ? QString() : QFileInfo(link).fileName();
}

QString hex4(QString id)
{
    id = id.trimmed().toLower();
    if (id.startsWith(QLatin1String("0x")))
        id = id.mid(2);
    return id.rightJustified(4, QLatin1Char('0'));
}

QString humanSize(double bytes)
{
    const char *units[] = { "bytes", "KB", "MB", "GB", "TB" };
    int u = 0;
    while (bytes >= 1000 && u < 4) {
        bytes /= 1000; // disks are sold in decimal units
        ++u;
    }
    return u == 0 ? QStringLiteral("%1 bytes").arg(bytes, 0, 'f', 0)
                  : QStringLiteral("%1 %2").arg(bytes, 0, 'f', bytes < 10 ? 1 : 0).arg(QLatin1String(units[u]));
}

// input device capability bitmaps: hex words, most significant first
bool hasBit(const QString &bitmap, int bit)
{
    const QStringList words = bitmap.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const int word = bit / 64, offset = bit % 64;
    if (word >= words.size())
        return false;
    bool ok = false;
    const quint64 value = words.at(words.size() - 1 - word).toULongLong(&ok, 16);
    return ok && (value >> offset) & 1;
}

// PCI base class -> category; finer for the ones that matter
struct PciClass {
    const char *prefix; // hex class code prefix
    const char *category;
    const char *type;
};
const PciClass pciClasses[] = {
    { "0300", "display", "VGA compatible controller" },
    { "0302", "display", "3D controller" },
    { "0380", "display", "Display controller" },
    { "0403", "sound", "Audio device" },
    { "0401", "sound", "Multimedia audio controller" },
    { "0106", "storage", "SATA controller" },
    { "0108", "storage", "Non-Volatile memory controller" },
    { "0104", "storage", "RAID controller" },
    { "0101", "storage", "IDE controller" },
    { "01", "storage", "Mass storage controller" },
    { "02", "network", "Network controller" },
    { "0c03", "usb", "USB controller" },
    { "0d", "network", "Wireless controller" },
    { "0480", "system", "Multimedia controller" },
    { "0600", "system", "Host bridge" },
    { "0601", "system", "ISA bridge" },
    { "0604", "system", "PCI bridge" },
    { "06", "system", "Bridge" },
    { "0c05", "system", "SMBus" },
    { "0c80", "system", "Serial bus controller" },
    { "0c", "system", "Serial bus controller" },
    { "05", "system", "Memory controller" },
    { "08", "system", "System peripheral" },
    { "11", "system", "Signal processing controller" },
    { "0b", "system", "Processor" },
    { "", "system", "PCI device" },
};

const struct {
    const char *id;
    const char *name;
    const char *icon;
    bool needsDriver; // flag devices without one
} categoryInfo[] = {
    { "batteries", "Batteries", "battery", false },
    { "bluetooth", "Bluetooth", "bluetooth", true },
    { "cdrom", "CD-ROM drives", "cdrom", false },
    { "disk", "Disk drives", "disk", false },
    { "display", "Display adapters", "display", true },
    { "imaging", "Imaging devices", "camera", false },
    { "keyboard", "Keyboards", "keyboard", false },
    { "monitor", "Monitors", "monitor", false },
    { "mouse", "Mice and other pointing devices", "mouse", false },
    { "network", "Network adapters", "network", true },
    { "cpu", "Processors", "cpu", false },
    { "sound", "Sound controllers", "sound", true },
    { "storage", "Storage controllers", "storage", true },
    { "system", "System devices", "system", false },
    { "usb", "Universal Serial Bus controllers", "usb", true },
    { "usbdevices", "USB devices", "usb", false },
};

} // namespace

HardwareInfo::HardwareInfo(QObject *parent)
    : QObject(parent)
{
    // Device nodes come and go as hardware is plugged in or removed.
    m_watcher.addPaths({ QStringLiteral("/dev"), QStringLiteral("/dev/input"), QStringLiteral("/sys/class/power_supply") });
    m_changeTimer.setSingleShot(true);
    m_changeTimer.setInterval(700); // let udev finish naming things
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_changeTimer, qOverload<>(&QTimer::start));
    connect(&m_changeTimer, &QTimer::timeout, this, &HardwareInfo::changed);
}

void HardwareInfo::loadIds(Ids &ids, const QStringList &candidates)
{
    if (ids.loaded)
        return;
    ids.loaded = true;
    for (const QString &path : candidates) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QString vendor;
        while (!f.atEnd()) {
            const QByteArray line = f.readLine();
            if (line.isEmpty() || line.startsWith('#') || line.trimmed().isEmpty())
                continue;
            if (line.startsWith("\t\t"))
                continue; // subsystems
            if (line.startsWith('\t')) {
                if (!vendor.isEmpty() && line.size() > 7)
                    ids.devices.insert(vendor + QLatin1Char(':') + QString::fromLatin1(line.mid(1, 4)).toLower(),
                                       QString::fromUtf8(line.mid(7)).trimmed());
                continue;
            }
            // vendor lines start with 4 hex digits; the class tables after
            // them ("C 00", "AT ...") end the part we need
            const QChar first = QLatin1Char(line.at(0));
            if (!first.isDigit() && !(first >= QLatin1Char('a') && first <= QLatin1Char('f')))
                break;
            vendor = QString::fromLatin1(line.left(4)).toLower();
            ids.vendors.insert(vendor, QString::fromUtf8(line.mid(6)).trimmed());
        }
        return;
    }
}

QString HardwareInfo::pciVendor(const QString &vendor)
{
    loadIds(m_pci, { QStringLiteral("/usr/share/hwdata/pci.ids"), QStringLiteral("/usr/share/misc/pci.ids"),
                     QStringLiteral("/usr/share/pci.ids") });
    return m_pci.vendors.value(hex4(vendor));
}

QString HardwareInfo::pciName(const QString &vendor, const QString &device)
{
    pciVendor(vendor);
    return m_pci.devices.value(hex4(vendor) + QLatin1Char(':') + hex4(device));
}

QString HardwareInfo::usbVendor(const QString &vendor)
{
    loadIds(m_usb, { QStringLiteral("/usr/share/hwdata/usb.ids"), QStringLiteral("/usr/share/misc/usb.ids"),
                     QStringLiteral("/usr/share/usb.ids") });
    return m_usb.vendors.value(hex4(vendor));
}

QString HardwareInfo::usbName(const QString &vendor, const QString &product)
{
    usbVendor(vendor);
    return m_usb.devices.value(hex4(vendor) + QLatin1Char(':') + hex4(product));
}

QString HardwareInfo::deviceName(const QString &sysPath, const QString &what)
{
    const QString path = QFileInfo(sysPath).canonicalFilePath();
    // walk up to the PCI or USB device this belongs to
    for (QString p = path; p.startsWith(QLatin1String("/sys/devices")); p = QFileInfo(p).path()) {
        if (QFileInfo::exists(p + QStringLiteral("/idVendor"))) {
            const QString product = readText(p + QStringLiteral("/product"));
            if (!product.isEmpty())
                return product;
            const QString vendor = readText(p + QStringLiteral("/idVendor"));
            const QString n = usbName(vendor, readText(p + QStringLiteral("/idProduct")));
            if (!n.isEmpty())
                return n;
            const QString maker = usbVendor(vendor);
            return (maker.isEmpty() ? QStringLiteral("USB") : maker) + QLatin1Char(' ') + what;
        }
        if (QFileInfo::exists(p + QStringLiteral("/vendor")) && QFileInfo::exists(p + QStringLiteral("/class"))
            && QFileInfo::exists(p + QStringLiteral("/device"))) {
            const QString vendor = readText(p + QStringLiteral("/vendor"));
            const QString n = pciName(vendor, readText(p + QStringLiteral("/device")));
            if (!n.isEmpty())
                return n;
            const QString maker = pciVendor(vendor);
            return (maker.isEmpty() ? QStringLiteral("PCI") : maker) + QLatin1Char(' ') + what;
        }
    }
    return {};
}

// the driver of a device, or of the device it's part of (a disk's driver
// sits on its controller's node, for example)
static QString driverUp(const QString &sysPath)
{
    QString p = QFileInfo(sysPath).canonicalFilePath();
    for (int i = 0; i < 3 && p.startsWith(QLatin1String("/sys/devices")); ++i, p = QFileInfo(p).path()) {
        const QString d = driverOf(p);
        if (!d.isEmpty())
            return d;
    }
    return {};
}

QVariantMap HardwareInfo::computer() const
{
    const QString dmi = QStringLiteral("/sys/class/dmi/id/");
    const QString vendor = readText(dmi + QStringLiteral("sys_vendor"));
    QString model = readText(dmi + QStringLiteral("product_version")); // Lenovo puts the name here
    if (model.isEmpty() || model.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0
        || model.contains(QLatin1String("Not Specified"), Qt::CaseInsensitive))
        model = readText(dmi + QStringLiteral("product_name"));
    QString name = model.startsWith(vendor, Qt::CaseInsensitive) ? model : (vendor + QLatin1Char(' ') + model).trimmed();
    if (name.isEmpty())
        name = QStringLiteral("Computer");
    QVariantList details;
    auto add = [&details](const QString &label, const QString &value) {
        if (!value.isEmpty())
            details << QVariant(QVariantList{ label, value });
    };
    add(QStringLiteral("Manufacturer"), vendor);
    add(QStringLiteral("Model"), readText(dmi + QStringLiteral("product_name")));
    add(QStringLiteral("Board"), readText(dmi + QStringLiteral("board_name")));
    add(QStringLiteral("BIOS"), (readText(dmi + QStringLiteral("bios_vendor")) + QLatin1Char(' ')
                                 + readText(dmi + QStringLiteral("bios_version"))).trimmed());
    add(QStringLiteral("BIOS date"), readText(dmi + QStringLiteral("bios_date")));
    add(QStringLiteral("Firmware"), QFileInfo::exists(QStringLiteral("/sys/firmware/efi")) ? QStringLiteral("UEFI")
                                                                                        : QStringLiteral("BIOS (legacy)"));
    return { { QStringLiteral("name"), name }, { QStringLiteral("details"), details } };
}

QVariantList HardwareInfo::categories()
{
    QHash<QString, QVariantList> devices; // category -> devices
    auto add = [&devices](const QString &category, const QString &name, const QString &driver,
                          const QVariantList &details, const QString &icon = {}) {
        QVariantList all = details;
        all << QVariant(QVariantList{ QStringLiteral("Driver"), driver.isEmpty() ? QStringLiteral("(none)") : driver });
        devices[category] << QVariantMap{
            { QStringLiteral("name"), name },
            { QStringLiteral("driver"), driver },
            { QStringLiteral("icon"), icon },
            { QStringLiteral("details"), all },
        };
    };
    auto pair = [](const QString &a, const QString &b) { return QVariant(QVariantList{ a, b }); };

    // ---- processors (each logical processor, as Device Manager lists them)
    {
        QString model;
        for (const QString &line : readText(QStringLiteral("/proc/cpuinfo")).split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("model name"))) {
                model = line.section(QLatin1Char(':'), 1).trimmed();
                add(QStringLiteral("cpu"), model, QStringLiteral("(built in)"),
                    { pair(QStringLiteral("Processor"), model) });
            }
        }
    }

    // ---- PCI devices
    QSet<QString> networkPci, soundPci;
    for (const QString &entry : entries(QStringLiteral("/sys/bus/pci/devices"))) {
        const QString p = QStringLiteral("/sys/bus/pci/devices/") + entry;
        const QString cls = readText(p + QStringLiteral("/class")).mid(2); // "030000"
        const QString vendor = readText(p + QStringLiteral("/vendor")), device = readText(p + QStringLiteral("/device"));
        const PciClass *match = nullptr;
        for (const PciClass &c : pciClasses) {
            if (cls.startsWith(QLatin1String(c.prefix))) {
                match = &c;
                break;
            }
        }
        QString name = pciName(vendor, device);
        const QString vendorName = pciVendor(vendor);
        if (name.isEmpty())
            name = vendorName.isEmpty() ? QStringLiteral("%1 (%2:%3)").arg(QLatin1String(match->type), hex4(vendor), hex4(device))
                                        : vendorName + QLatin1Char(' ') + QLatin1String(match->type);
        // "0000:00:02.0" -> bus 0, device 2, function 0
        const QString location = QStringLiteral("PCI bus %1, device %2, function %3")
                                     .arg(entry.section(QLatin1Char(':'), 1, 1).toInt(nullptr, 16))
                                     .arg(entry.section(QLatin1Char(':'), 2).section(QLatin1Char('.'), 0, 0).toInt(nullptr, 16))
                                     .arg(entry.section(QLatin1Char('.'), 1));
        const QString category = QLatin1String(match->category);
        const QVariantList details = {
            pair(QStringLiteral("Device type"), QLatin1String(match->type)),
            pair(QStringLiteral("Manufacturer"), vendorName.isEmpty() ? hex4(vendor) : vendorName),
            pair(QStringLiteral("Location"), location),
            pair(QStringLiteral("Hardware ID"), QStringLiteral("PCI %1:%2").arg(hex4(vendor), hex4(device))),
        };
        // network and sound cards are listed from their own subsystems
        // below, which also catches USB ones; remember these to skip
        if (category == QLatin1String("network"))
            networkPci << QFileInfo(p).canonicalFilePath();
        if (category == QLatin1String("sound"))
            soundPci << QFileInfo(p).canonicalFilePath();
        if (category != QLatin1String("network") && category != QLatin1String("sound"))
            add(category, name, driverOf(p), details);
    }

    // ---- network adapters (physical ones)
    for (const QString &iface : entries(QStringLiteral("/sys/class/net"))) {
        const QString p = QStringLiteral("/sys/class/net/") + iface;
        if (!QFileInfo::exists(p + QStringLiteral("/device")))
            continue; // loopback, bridges, VPNs...
        const QString dev = QFileInfo(p + QStringLiteral("/device")).canonicalFilePath();
        networkPci.remove(dev);
        const bool wireless = QFileInfo::exists(p + QStringLiteral("/wireless")) || QFileInfo::exists(p + QStringLiteral("/phy80211"));
        QString name = deviceName(p + QStringLiteral("/device"),
                                  wireless ? QStringLiteral("Wireless network adapter") : QStringLiteral("Network adapter"));
        if (name.isEmpty())
            name = wireless ? QStringLiteral("Wireless network adapter") : QStringLiteral("Network adapter");
        add(QStringLiteral("network"), name, driverOf(p + QStringLiteral("/device")),
            { pair(QStringLiteral("Device type"), wireless ? QStringLiteral("Wireless (Wi-Fi)") : QStringLiteral("Wired (Ethernet)")),
              pair(QStringLiteral("Interface"), iface),
              pair(QStringLiteral("Hardware address"), readText(p + QStringLiteral("/address"))),
              pair(QStringLiteral("Connected"), readText(p + QStringLiteral("/carrier")) == QLatin1String("1")
                                                    ? QStringLiteral("Yes") : QStringLiteral("No")) });
    }
    // network controllers with no interface: no driver, or switched off
    for (const QString &dev : std::as_const(networkPci)) {
        const QString n = deviceName(dev, QStringLiteral("Network controller"));
        add(QStringLiteral("network"), n.isEmpty() ? QStringLiteral("Network controller") : n, driverOf(dev),
            { pair(QStringLiteral("Interface"), QStringLiteral("(none)")) });
    }

    // ---- sound
    for (const QString &card : entries(QStringLiteral("/sys/class/sound"))) {
        if (!card.startsWith(QLatin1String("card")))
            continue;
        const QString p = QStringLiteral("/sys/class/sound/") + card;
        const QString dev = QFileInfo(p + QStringLiteral("/device")).canonicalFilePath();
        QString controllerDriver;
        for (QString up = dev; up.startsWith(QLatin1String("/sys/devices")); up = QFileInfo(up).path()) {
            if (soundPci.remove(up))
                controllerDriver = driverOf(up);
        }
        const QString id = readText(p + QStringLiteral("/id"));
        QString name = deviceName(p + QStringLiteral("/device"), QStringLiteral("Audio controller"));
        if (name.isEmpty())
            name = id;
        QVariantList details = { pair(QStringLiteral("Sound card"), id), pair(QStringLiteral("Card number"), card.mid(4)) };
        if (!controllerDriver.isEmpty() && controllerDriver != driverOf(dev))
            details << pair(QStringLiteral("Controller driver"), controllerDriver);
        add(QStringLiteral("sound"), name, driverOf(dev), details);
    }
    for (const QString &dev : std::as_const(soundPci)) {
        const QString n = deviceName(dev, QStringLiteral("Audio controller"));
        add(QStringLiteral("sound"), n.isEmpty() ? QStringLiteral("Audio controller") : n, driverOf(dev), {});
    }

    // ---- monitors
    for (const QString &conn : entries(QStringLiteral("/sys/class/drm"))) {
        const QString p = QStringLiteral("/sys/class/drm/") + conn;
        if (!conn.contains(QLatin1Char('-')) || readText(p + QStringLiteral("/status")) != QLatin1String("connected"))
            continue;
        const QString port = conn.section(QLatin1Char('-'), 1); // "eDP-1"
        const QByteArray edid = readBytes(p + QStringLiteral("/edid"));
        QString name, maker;
        int widthCm = 0, heightCm = 0;
        if (edid.size() >= 128) {
            // the monitor name descriptor (tag 0xFC) in one of four slots
            for (int at : { 54, 72, 90, 108 }) {
                if (edid.at(at) == 0 && edid.at(at + 1) == 0 && quint8(edid.at(at + 3)) == 0xFC)
                    name = QString::fromLatin1(edid.mid(at + 5, 13)).section(QLatin1Char('\n'), 0, 0).trimmed();
            }
            const quint16 id = (quint8(edid.at(8)) << 8) | quint8(edid.at(9));
            maker = QString(QChar('A' - 1 + ((id >> 10) & 31))) + QChar('A' - 1 + ((id >> 5) & 31)) + QChar('A' - 1 + (id & 31));
            widthCm = quint8(edid.at(21));
            heightCm = quint8(edid.at(22));
        }
        const bool builtIn = port.startsWith(QLatin1String("eDP")) || port.startsWith(QLatin1String("LVDS")) || port.startsWith(QLatin1String("DSI"));
        if (name.isEmpty())
            name = builtIn ? QStringLiteral("Built-in display") : QStringLiteral("Monitor");
        const QString mode = readText(p + QStringLiteral("/modes")).section(QLatin1Char('\n'), 0, 0);
        QVariantList details = { pair(QStringLiteral("Connected to"), port + (builtIn ? QStringLiteral(" (built-in)") : QString())) };
        if (!maker.isEmpty())
            details << pair(QStringLiteral("Manufacturer code"), maker);
        if (!mode.isEmpty())
            details << pair(QStringLiteral("Best resolution"), mode);
        if (widthCm > 0 && heightCm > 0)
            details << pair(QStringLiteral("Screen size"),
                            QStringLiteral("%1 x %2 cm (%3\")").arg(widthCm).arg(heightCm)
                                .arg(std::sqrt(double(widthCm * widthCm + heightCm * heightCm)) / 2.54, 0, 'f', 1));
        add(QStringLiteral("monitor"), name, QStringLiteral("(display adapter)"), details);
    }

    // ---- disks and CD-ROM drives
    for (const QString &block : entries(QStringLiteral("/sys/block"))) {
        if (block.startsWith(QLatin1String("loop")) || block.startsWith(QLatin1String("ram"))
            || block.startsWith(QLatin1String("zram")) || block.startsWith(QLatin1String("dm-"))
            || block.startsWith(QLatin1String("md")) || block.startsWith(QLatin1String("nbd")))
            continue;
        const QString p = QStringLiteral("/sys/block/") + block;
        const QString real = QFileInfo(p).canonicalFilePath();
        const bool cdrom = block.startsWith(QLatin1String("sr"));
        const bool removable = readText(p + QStringLiteral("/removable")) == QLatin1String("1");
        QString model = readText(p + QStringLiteral("/device/model"));
        if (model.isEmpty())
            model = readText(p + QStringLiteral("/device/name")); // SD cards
        const QString vendor = readText(p + QStringLiteral("/device/vendor"));
        if (!vendor.isEmpty() && !vendor.startsWith(QLatin1String("0x")) && !model.startsWith(vendor))
            model = vendor + QLatin1Char(' ') + model;
        if (model.isEmpty())
            model = cdrom ? QStringLiteral("CD-ROM drive") : QStringLiteral("Disk drive");
        const QString bus = real.contains(QLatin1String("/usb")) ? QStringLiteral("USB")
                          : block.startsWith(QLatin1String("nvme")) ? QStringLiteral("NVMe")
                          : block.startsWith(QLatin1String("mmc")) ? QStringLiteral("SD/MMC card reader")
                          : real.contains(QLatin1String("virtio")) ? QStringLiteral("Virtual (VM)")
                          : real.contains(QLatin1String("/ata")) ? QStringLiteral("SATA") : QStringLiteral("Other");
        const double size = readText(p + QStringLiteral("/size")).toDouble() * 512;
        const bool spinning = readText(p + QStringLiteral("/queue/rotational")) == QLatin1String("1");
        QVariantList details = {
            pair(QStringLiteral("Device type"), cdrom ? QStringLiteral("CD/DVD drive")
                                                : spinning ? QStringLiteral("Hard disk") : QStringLiteral("Solid-state drive")),
            pair(QStringLiteral("Connection"), bus),
            pair(QStringLiteral("Capacity"), size > 0 ? humanSize(size) : QStringLiteral("(no disc)")),
            pair(QStringLiteral("Removable"), removable ? QStringLiteral("Yes") : QStringLiteral("No")),
            pair(QStringLiteral("Device"), QStringLiteral("/dev/") + block),
        };
        add(cdrom ? QStringLiteral("cdrom") : QStringLiteral("disk"), model.simplified(),
            driverUp(p + QStringLiteral("/device")), details);
    }

    // ---- keyboards and pointing devices
    for (const QString &input : entries(QStringLiteral("/sys/class/input"))) {
        if (!input.startsWith(QLatin1String("input")))
            continue;
        const QString p = QStringLiteral("/sys/class/input/") + input;
        const QString name = readText(p + QStringLiteral("/name"));
        const QString ev = readText(p + QStringLiteral("/capabilities/ev")), key = readText(p + QStringLiteral("/capabilities/key")),
                      rel = readText(p + QStringLiteral("/capabilities/rel")), abs = readText(p + QStringLiteral("/capabilities/abs"));
        const bool hasKeys = hasBit(ev, 1);
        const QString bus = QFileInfo(p).canonicalFilePath().contains(QLatin1String("/usb")) ? QStringLiteral("USB")
                          : QFileInfo(p).canonicalFilePath().contains(QLatin1String("bluetooth")) || name.contains(QLatin1String("Bluetooth"))
                              ? QStringLiteral("Bluetooth") : QStringLiteral("Built-in (PS/2 or I2C)");
        const QVariantList details = { pair(QStringLiteral("Connection"), bus), pair(QStringLiteral("Input device"), input) };
        // keyboards have letter keys (Q, A, Z)
        if (hasKeys && hasBit(key, 16) && hasBit(key, 30) && hasBit(key, 44)) {
            add(QStringLiteral("keyboard"), name, QStringLiteral("(built in)"), details);
        } else if (hasBit(abs, 0) && hasBit(abs, 1) && hasBit(key, 325)) { // BTN_TOOL_FINGER
            add(QStringLiteral("mouse"), name, QStringLiteral("(built in)"),
                QVariantList{ pair(QStringLiteral("Device type"), QStringLiteral("Touchpad")) } + details);
        } else if (hasBit(abs, 0) && hasBit(abs, 1) && hasBit(key, 330)) { // BTN_TOUCH
            add(QStringLiteral("mouse"), name, QStringLiteral("(built in)"),
                QVariantList{ pair(QStringLiteral("Device type"), QStringLiteral("Touch screen or tablet")) } + details);
        } else if (hasBit(rel, 0) && hasBit(rel, 1) && hasBit(key, 272)) { // BTN_LEFT
            add(QStringLiteral("mouse"), name, QStringLiteral("(built in)"),
                QVariantList{ pair(QStringLiteral("Device type"), QStringLiteral("Mouse")) } + details);
        }
    }

    // ---- Bluetooth adapters
    for (const QString &hci : entries(QStringLiteral("/sys/class/bluetooth"))) {
        if (hci.contains(QLatin1Char(':')))
            continue; // connections, not adapters
        const QString p = QStringLiteral("/sys/class/bluetooth/") + hci;
        QString name = deviceName(p + QStringLiteral("/device"), QStringLiteral("Bluetooth adapter"));
        if (name.isEmpty())
            name = QStringLiteral("Bluetooth adapter");
        add(QStringLiteral("bluetooth"), name, driverOf(p + QStringLiteral("/device")),
            { pair(QStringLiteral("Adapter"), hci) });
    }

    // ---- cameras (one per physical device; each has several nodes)
    QSet<QString> cameras;
    for (const QString &video : entries(QStringLiteral("/sys/class/video4linux"))) {
        const QString p = QStringLiteral("/sys/class/video4linux/") + video;
        QString dev = QFileInfo(p + QStringLiteral("/device")).canonicalFilePath();
        // a USB camera's interfaces share the USB device (the parent)
        if (QFileInfo::exists(QFileInfo(dev).path() + QStringLiteral("/idVendor")))
            dev = QFileInfo(dev).path();
        if (cameras.contains(dev))
            continue;
        cameras << dev;
        QString name = deviceName(p + QStringLiteral("/device"), QStringLiteral("Camera"));
        if (name.isEmpty())
            name = readText(p + QStringLiteral("/name"));
        add(QStringLiteral("imaging"), name, driverOf(p + QStringLiteral("/device")),
            { pair(QStringLiteral("Device"), QStringLiteral("/dev/") + video) });
    }

    // ---- batteries
    for (const QString &supply : entries(QStringLiteral("/sys/class/power_supply"))) {
        const QString p = QStringLiteral("/sys/class/power_supply/") + supply;
        if (readText(p + QStringLiteral("/type")) != QLatin1String("Battery")
            || readText(p + QStringLiteral("/scope")) == QLatin1String("Device"))
            continue;
        const QString maker = readText(p + QStringLiteral("/manufacturer")), model = readText(p + QStringLiteral("/model_name"));
        QString name = (maker + QLatin1Char(' ') + model).trimmed();
        if (name.isEmpty())
            name = QStringLiteral("Battery");
        QVariantList details = {
            pair(QStringLiteral("Charge"), readText(p + QStringLiteral("/capacity")) + QStringLiteral("%")),
            pair(QStringLiteral("Status"), readText(p + QStringLiteral("/status"))),
            pair(QStringLiteral("Technology"), readText(p + QStringLiteral("/technology"))),
        };
        const double design = readText(p + QStringLiteral("/energy_full_design")).toDouble();
        const double full = readText(p + QStringLiteral("/energy_full")).toDouble();
        if (design > 0) {
            details << pair(QStringLiteral("Design capacity"), QStringLiteral("%1 Wh").arg(design / 1e6, 0, 'f', 1));
            if (full > 0)
                details << pair(QStringLiteral("Health"), QStringLiteral("%1% of new").arg(qRound(100 * full / design)));
        }
        add(QStringLiteral("batteries"), name, QStringLiteral("(built in)"), details);
    }

    // ---- USB devices (hubs are listed with the USB controllers)
    for (const QString &entry : entries(QStringLiteral("/sys/bus/usb/devices"))) {
        if (entry.contains(QLatin1Char(':')) || entry.startsWith(QLatin1String("usb")))
            continue; // interfaces, and the controllers' root hubs
        const QString p = QStringLiteral("/sys/bus/usb/devices/") + entry;
        const QString vendor = readText(p + QStringLiteral("/idVendor")), product = readText(p + QStringLiteral("/idProduct"));
        QString name = readText(p + QStringLiteral("/product"));
        if (name.isEmpty())
            name = usbName(vendor, product);
        QString maker = readText(p + QStringLiteral("/manufacturer"));
        if (maker.isEmpty())
            maker = usbVendor(vendor);
        if (name.isEmpty())
            name = maker.isEmpty() ? QStringLiteral("USB device (%1:%2)").arg(vendor, product)
                                   : maker + QStringLiteral(" USB device");
        // the drivers of its interfaces ("1-3:1.0/driver")
        QStringList drivers;
        for (const QString &sub : entries(p)) {
            if (sub.startsWith(entry + QLatin1Char(':'))) {
                const QString d = driverOf(p + QLatin1Char('/') + sub);
                if (!d.isEmpty() && !drivers.contains(d))
                    drivers << d;
            }
        }
        const bool hub = readText(p + QStringLiteral("/bDeviceClass")) == QLatin1String("09");
        const QString speed = readText(p + QStringLiteral("/speed"));
        const QVariantList details = {
            pair(QStringLiteral("Manufacturer"), maker),
            pair(QStringLiteral("Location"), QStringLiteral("USB port %1").arg(entry)),
            pair(QStringLiteral("Speed"), speed.isEmpty() ? QString() : speed + QStringLiteral(" Mbit/s")),
            pair(QStringLiteral("Hardware ID"), QStringLiteral("USB %1:%2").arg(vendor, product)),
        };
        add(hub ? QStringLiteral("usb") : QStringLiteral("usbdevices"), name, drivers.join(QStringLiteral(", ")), details);
    }

    // ---- assemble, with problem flags and status text
    QVariantList result;
    for (const auto &info : categoryInfo) {
        QVariantList list = devices.value(QLatin1String(info.id));
        if (list.isEmpty())
            continue;
        for (QVariant &v : list) {
            QVariantMap d = v.toMap();
            const bool problem = info.needsDriver && d.value(QStringLiteral("driver")).toString().isEmpty();
            d.insert(QStringLiteral("problem"), problem);
            d.insert(QStringLiteral("status"), problem
                ? QStringLiteral("No driver is installed for this device, so it isn't working. The kernel may "
                                 "not support it, or it may need firmware that isn't installed.")
                : QStringLiteral("This device is working properly."));
            if (d.value(QStringLiteral("icon")).toString().isEmpty())
                d.insert(QStringLiteral("icon"), QLatin1String(info.icon));
            v = d;
        }
        std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("name")).toString().compare(
                       b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
        });
        result << QVariantMap{
            { QStringLiteral("id"), QLatin1String(info.id) },
            { QStringLiteral("name"), QLatin1String(info.name) },
            { QStringLiteral("icon"), QLatin1String(info.icon) },
            { QStringLiteral("devices"), list },
        };
    }
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString() < b.toMap().value(QStringLiteral("name")).toString();
    });
    return result;
}

QStringList HardwareInfo::touchpads() const
{
    QStringList names;
    for (const QFileInfo &e : QDir(QStringLiteral("/sys/class/input")).entryInfoList({ QStringLiteral("event*") }, QDir::Dirs | QDir::System)) {
        QFile dev(e.absoluteFilePath() + QStringLiteral("/dev"));
        if (!dev.open(QIODevice::ReadOnly))
            continue;
        const QByteArray numbers = dev.readAll().trimmed(); // "13:69"
        QFile data(QStringLiteral("/run/udev/data/c") + QString::fromLatin1(numbers));
        if (!data.open(QIODevice::ReadOnly) || !data.readAll().contains("E:ID_INPUT_TOUCHPAD=1"))
            continue;
        QFile name(e.absoluteFilePath() + QStringLiteral("/device/name"));
        if (name.open(QIODevice::ReadOnly))
            names << QString::fromUtf8(name.readAll().trimmed());
    }
    return names;
}
