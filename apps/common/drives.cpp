#include "drives.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QJSEngine>
#include <QLocale>
#include <QQmlEngine>
#include <QSet>
#include <QtQml/qqml.h>

namespace {

const QString Service = QStringLiteral("org.freedesktop.UDisks2");
const QString Root = QStringLiteral("/org/freedesktop/UDisks2");
const QString ObjectManager = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString BlockIface = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString FilesystemIface = QStringLiteral("org.freedesktop.UDisks2.Filesystem");
const QString EncryptedIface = QStringLiteral("org.freedesktop.UDisks2.Encrypted");
const QString DriveIface = QStringLiteral("org.freedesktop.UDisks2.Drive");

// (see the class comment)
QDBusConnection bus()
{
    const bool test = qEnvironmentVariableIsSet("WAYLAND95_NO_SYSTEM_AGENTS")
                      && qEnvironmentVariableIsSet("WAYLAND95_TEST_AGENTS_ON_SESSION_BUS");
    return test ? QDBusConnection::sessionBus() : QDBusConnection::systemBus();
}

QString bytes(const QVariant &v)
{
    QByteArray b = v.toByteArray();
    while (b.endsWith('\0'))
        b.chop(1);
    return QString::fromLocal8Bit(b);
}

QStringList byteArrays(const QVariant &v)
{
    QStringList out;
    if (v.canConvert<QDBusArgument>()) {
        const QDBusArgument a = v.value<QDBusArgument>();
        a.beginArray();
        while (!a.atEnd()) {
            QByteArray b;
            a >> b;
            while (b.endsWith('\0'))
                b.chop(1);
            out << QString::fromLocal8Bit(b);
        }
        a.endArray();
    } else {
        for (const QVariant &e : v.toList())
            out << bytes(e);
    }
    return out;
}

QString objectPath(const QVariant &v)
{
    if (v.canConvert<QDBusObjectPath>())
        return v.value<QDBusObjectPath>().path();
    return v.toString();
}

} // namespace

Drives *Drives::instance()
{
    static Drives *drives = new Drives;
    return drives;
}

Drives *Drives::create(QQmlEngine *, QJSEngine *)
{
    Drives *d = instance();
    QJSEngine::setObjectOwnership(d, QJSEngine::CppOwnership);
    return d;
}

Drives::Drives(QObject *parent)
    : QObject(parent)
{
    m_reload.setSingleShot(true);
    m_reload.setInterval(150);
    connect(&m_reload, &QTimer::timeout, this, &Drives::load);
    QDBusConnection b = bus();
    for (const char *signal : {"InterfacesAdded", "InterfacesRemoved"})
        b.connect(Service, Root, ObjectManager, QLatin1String(signal), &m_reload, SLOT(start()));
    // a filesystem mounted or unmounted (here or by anything else)
    b.connect(Service, QString(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
              this, SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    load();
}

void Drives::propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &,
                               const QDBusMessage &message)
{
    if (interface == FilesystemIface && changed.contains(QStringLiteral("MountPoints"))) {
        const QStringList mounts = byteArrays(changed.value(QStringLiteral("MountPoints")));
        for (QVariant &v : m_drives) {
            QVariantMap d = v.toMap();
            if (d.value(QStringLiteral("id")) != message.path())
                continue;
            d[QStringLiteral("mounted")] = !mounts.isEmpty();
            d[QStringLiteral("mountPath")] = mounts.value(0);
            v = d;
        }
    }
    m_reload.start();
}

void Drives::refresh()
{
    load();
}

QString Drives::sizeText(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    // drives are sold in powers of ten: a "500 GB" disk says 500 GB here too
    return QLocale::system().formattedDataSize(bytes, bytes >= 1000LL * 1000 * 1000 * 100 ? 0 : 1, QLocale::DataSizeSIFormat);
}

void Drives::load()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service, Root, ObjectManager, QStringLiteral("GetManagedObjects"));
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(msg, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            const bool was = m_available || !m_drives.isEmpty();
            m_available = false;
            m_drives.clear();
            if (was)
                emit drivesChanged();
            return;
        }
        // object path -> interface -> properties
        QHash<QString, QHash<QString, QVariantMap>> objects;
        const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            QDBusObjectPath path;
            arg >> path;
            QHash<QString, QVariantMap> ifaces;
            arg.beginMap();
            while (!arg.atEnd()) {
                arg.beginMapEntry();
                QString name;
                QVariantMap props;
                arg >> name >> props;
                arg.endMapEntry();
                ifaces.insert(name, props);
            }
            arg.endMap();
            arg.endMapEntry();
            objects.insert(path.path(), ifaces);
        }
        arg.endMap();

        QVariantList list;
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            const QVariantMap block = it->value(BlockIface);
            if (block.isEmpty())
                continue;
            const bool fs = it->contains(FilesystemIface);
            const bool crypto = it->contains(EncryptedIface);
            if (!fs && !crypto)
                continue;
            if (block.value(QStringLiteral("HintIgnore")).toBool())
                continue;
            // an unlocked encrypted drive's filesystem: listed itself, under its own name
            const QString drivePath = objectPath(block.value(QStringLiteral("Drive")));
            const QVariantMap drive = objects.value(drivePath).value(DriveIface);
            const QString bus = drive.value(QStringLiteral("ConnectionBus")).toString();
            const bool removable = drive.value(QStringLiteral("Removable")).toBool() || drive.value(QStringLiteral("MediaRemovable")).toBool()
                || drive.value(QStringLiteral("Ejectable")).toBool() || bus == QLatin1String("usb") || bus == QLatin1String("sdio")
                || bus == QLatin1String("ieee1394");
            // the computer's own disks (the system, its EFI partition) are
            // My Computer's mounted drives already; these are the plug-in ones
            if (!removable && block.value(QStringLiteral("HintSystem")).toBool())
                continue;
            if (crypto && !fs) {
                // a locked one; skip it if it's unlocked (its cleartext is listed)
                bool unlocked = false;
                for (auto jt = objects.cbegin(); jt != objects.cend(); ++jt)
                    if (objectPath(jt->value(BlockIface).value(QStringLiteral("CryptoBackingDevice"))) == it.key())
                        unlocked = true;
                if (unlocked)
                    continue;
            }
            const QStringList mounts = fs ? byteArrays(it->value(FilesystemIface).value(QStringLiteral("MountPoints"))) : QStringList();
            const qint64 size = block.value(QStringLiteral("Size")).toLongLong();
            QString name = block.value(QStringLiteral("IdLabel")).toString();
            if (name.isEmpty())
                name = block.value(QStringLiteral("HintName")).toString();
            if (name.isEmpty())
                name = (bus == QLatin1String("usb") ? QStringLiteral("USB Drive") : QStringLiteral("Removable Disk"))
                       + (size > 0 ? QStringLiteral(" (%1)").arg(sizeText(size)) : QString());
            QVariantMap m;
            m[QStringLiteral("id")] = it.key();
            m[QStringLiteral("name")] = name;
            m[QStringLiteral("device")] = bytes(block.value(QStringLiteral("PreferredDevice"))).isEmpty()
                ? bytes(block.value(QStringLiteral("Device"))) : bytes(block.value(QStringLiteral("PreferredDevice")));
            m[QStringLiteral("size")] = size;
            m[QStringLiteral("sizeText")] = sizeText(size);
            m[QStringLiteral("fsType")] = block.value(QStringLiteral("IdType")).toString();
            m[QStringLiteral("mounted")] = !mounts.isEmpty();
            m[QStringLiteral("mountPath")] = mounts.value(0);
            m[QStringLiteral("encrypted")] = crypto && !fs;
            m[QStringLiteral("drive")] = drivePath;
            m[QStringLiteral("driveName")] = (drive.value(QStringLiteral("Vendor")).toString() + QLatin1Char(' ')
                                              + drive.value(QStringLiteral("Model")).toString()).trimmed();
            m[QStringLiteral("removable")] = removable;
            m[QStringLiteral("canEject")] = removable;
            list << m;
        }
        std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(),
                                                                             Qt::CaseInsensitive) < 0;
        });

        QSet<QString> before, after;
        for (const QVariant &v : std::as_const(m_drives))
            before << v.toMap().value(QStringLiteral("id")).toString();
        for (const QVariant &v : std::as_const(list))
            after << v.toMap().value(QStringLiteral("id")).toString();
        const QVariantList old = m_drives;
        const bool wasLoaded = m_loaded;
        const bool changed = !m_available || list != m_drives;
        m_drives = list;
        m_available = true;
        m_loaded = true;
        if (changed)
            emit drivesChanged();
        if (wasLoaded) {
            for (const QVariant &v : std::as_const(list))
                if (!before.contains(v.toMap().value(QStringLiteral("id")).toString()))
                    emit driveAdded(v.toMap());
            for (const QVariant &v : old) {
                QVariantMap gone = v.toMap();
                if (after.contains(gone.value(QStringLiteral("id")).toString()))
                    continue;
                gone[QStringLiteral("ejected")] = m_ejected.contains(gone.value(QStringLiteral("drive")).toString());
                emit driveRemoved(gone);
            }
        }
    });
}

QVariantMap Drives::drive(const QString &id) const
{
    for (const QVariant &v : m_drives)
        if (v.toMap().value(QStringLiteral("id")) == id)
            return v.toMap();
    return {};
}

QVariantMap Drives::driveAt(const QString &path) const
{
    const QString p = QDir::cleanPath(path);
    for (const QVariant &v : m_drives) {
        const QString mp = v.toMap().value(QStringLiteral("mountPath")).toString();
        if (!mp.isEmpty() && (p == mp || p.startsWith(mp + QLatin1Char('/'))))
            return v.toMap();
    }
    return {};
}

void Drives::call(const QString &path, const QString &interface, const QString &method, const QVariantMap &options,
                  const std::function<void(bool, const QVariant &, const QString &)> &done)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(Service, path, interface, method);
    msg << options;
    msg.setInteractiveAuthorizationAllowed(true); // a password box, if polkit wants one
    // mounting can wait for a password: up to a few minutes
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(msg, 5 * 60 * 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [done](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ReplyMessage)
            done(true, reply.arguments().value(0), QString());
        else
            done(false, QVariant(), reply.errorName() + QLatin1Char('\n') + reply.errorMessage());
    });
}

QString Drives::explain(const QString &errorName, const QString &message, const QString &name)
{
    if (errorName.endsWith(QLatin1String(".DeviceBusy")) || message.contains(QLatin1String("target is busy")))
        return QStringLiteral("%1 is still in use. Close the files and windows open on it (a terminal in it, too), then try again.").arg(name);
    if (errorName.endsWith(QLatin1String(".NotAuthorized")) || errorName.endsWith(QLatin1String(".NotAuthorizedCanObtain"))
        || errorName.endsWith(QLatin1String(".NotAuthorizedDismissed")))
        return QStringLiteral("You aren't allowed to do that with %1 without an administrator's password.").arg(name);
    if (errorName.endsWith(QLatin1String(".Cancelled")))
        return QStringLiteral("Cancelled.");
    if (errorName.endsWith(QLatin1String(".NoReply")) || errorName.endsWith(QLatin1String(".ServiceUnknown")))
        return QStringLiteral("The drive service (UDisks2) isn't answering, so %1 can't be opened.").arg(name);
    QString m = message.trimmed();
    // "Error mounting /dev/sda1 at ...: wrong fs type..." is all there is
    return m.isEmpty() ? QStringLiteral("Something went wrong with %1.").arg(name) : m;
}

void Drives::mount(const QString &id, const Done &done)
{
    const QVariantMap d = drive(id);
    if (d.isEmpty()) {
        done(false, QStringLiteral("That drive isn't plugged in any more."));
        return;
    }
    if (d.value(QStringLiteral("mounted")).toBool()) {
        done(true, d.value(QStringLiteral("mountPath")).toString());
        return;
    }
    const QString name = d.value(QStringLiteral("name")).toString();
    if (d.value(QStringLiteral("encrypted")).toBool()) {
        done(false, QStringLiteral("%1 is encrypted. Opening encrypted drives isn't done here yet.").arg(name));
        return;
    }
    call(id, FilesystemIface, QStringLiteral("Mount"), {}, [this, done, name](bool ok, const QVariant &result, const QString &err) {
        if (!ok) {
            done(false, explain(err.section(QLatin1Char('\n'), 0, 0), err.section(QLatin1Char('\n'), 1), name));
            return;
        }
        m_reload.start();
        done(true, result.toString());
    });
}

void Drives::unmount(const QString &id, const Done &done)
{
    const QVariantMap d = drive(id);
    const QString name = d.value(QStringLiteral("name"), QStringLiteral("The drive")).toString();
    if (!d.isEmpty() && !d.value(QStringLiteral("mounted")).toBool()) {
        done(true, QString());
        return;
    }
    call(id, FilesystemIface, QStringLiteral("Unmount"), {}, [this, done, name](bool ok, const QVariant &, const QString &err) {
        m_reload.start();
        done(ok, ok ? QString() : explain(err.section(QLatin1Char('\n'), 0, 0), err.section(QLatin1Char('\n'), 1), name));
    });
}

void Drives::eject(const QString &id, const Done &done)
{
    const QVariantMap d = drive(id);
    if (d.isEmpty()) {
        done(false, QStringLiteral("That drive isn't plugged in any more."));
        return;
    }
    const QString drivePath = d.value(QStringLiteral("drive")).toString();
    // every mounted filesystem on the same drive, then the drive itself
    QStringList mounted;
    for (const QVariant &v : std::as_const(m_drives)) {
        const QVariantMap o = v.toMap();
        if (o.value(QStringLiteral("drive")) == drivePath && o.value(QStringLiteral("mounted")).toBool())
            mounted << o.value(QStringLiteral("id")).toString();
    }
    m_ejected.insert(drivePath);
    auto powerOff = [this, drivePath, done]() {
        if (drivePath.isEmpty() || drivePath == QLatin1String("/")) {
            done(true, QString());
            return;
        }
        call(drivePath, DriveIface, QStringLiteral("PowerOff"), {}, [this, drivePath, done](bool ok, const QVariant &, const QString &err) {
            if (ok) {
                m_reload.start();
                done(true, QString());
                return;
            }
            // a card reader's medium: eject it instead (the reader stays).
            // Unmounted is what matters: it's safe to pull out either way.
            Q_UNUSED(err);
            call(drivePath, DriveIface, QStringLiteral("Eject"), {}, [this, done](bool, const QVariant &, const QString &) {
                m_reload.start();
                done(true, QString());
            });
        });
    };
    auto next = std::make_shared<std::function<void()>>();
    auto remaining = std::make_shared<QStringList>(mounted);
    *next = [this, remaining, next, powerOff, done]() {
        if (remaining->isEmpty()) {
            powerOff();
            return;
        }
        const QString one = remaining->takeFirst();
        unmount(one, [this, next, done](bool ok, const QString &err) {
            if (!ok) {
                m_ejected.clear();
                done(false, err);
                return;
            }
            (*next)();
        });
    };
    (*next)();
}

std::function<void(bool, const QString &)> Drives::wrap(const QJSValue &done)
{
    QJSValue cb = done;
    return [cb](bool ok, const QString &s) mutable {
        if (cb.isCallable())
            cb.call({ok, s});
    };
}

void Drives::mount(const QString &id, const QJSValue &done) { mount(id, Done(wrap(done))); }
void Drives::unmount(const QString &id, const QJSValue &done) { unmount(id, Done(wrap(done))); }
void Drives::eject(const QString &id, const QJSValue &done) { eject(id, Done(wrap(done))); }
