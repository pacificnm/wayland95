#include "imagewriter.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace {
const QString Service = QStringLiteral("org.freedesktop.UDisks2");
const QString BlockIface = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString DriveIface = QStringLiteral("org.freedesktop.UDisks2.Drive");
const QString PartitionIface = QStringLiteral("org.freedesktop.UDisks2.Partition");
const QString FilesystemIface = QStringLiteral("org.freedesktop.UDisks2.Filesystem");

constexpr qint64 Chunk = 4 * 1024 * 1024;
// a password prompt can take a while
constexpr int PromptTimeout = 5 * 60 * 1000;

using Interfaces = QMap<QString, QVariantMap>;
using Objects = QMap<QString, Interfaces>;

QDBusConnection bus() { return QDBusConnection::systemBus(); }

// org.freedesktop.DBus.ObjectManager.GetManagedObjects: a{oa{sa{sv}}}
Objects managedObjects()
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/freedesktop/UDisks2"),
                                                       QStringLiteral("org.freedesktop.DBus.ObjectManager"),
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

// mount points that mean "this is the computer's own disk"
bool isSystemMount(const QString &point)
{
    static const QSet<QString> system{ QStringLiteral("/"), QStringLiteral("/boot"), QStringLiteral("/boot/efi"),
                                       QStringLiteral("/efi"), QStringLiteral("/home"), QStringLiteral("/usr"),
                                       QStringLiteral("/var"), QStringLiteral("/run/live/medium") };
    return system.contains(point);
}

QString errnoText() { return QString::fromLocal8Bit(std::strerror(errno)); }

bool writeAll(int fd, const char *data, qint64 size)
{
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size_t(size));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += n;
        size -= n;
    }
    return true;
}

qint64 readAll(int fd, char *data, qint64 size)
{
    qint64 got = 0;
    while (got < size) {
        const ssize_t n = ::read(fd, data + got, size_t(size - got));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;
        got += n;
    }
    return got;
}
} // namespace

ImageWriter::ImageWriter(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(250);
    connect(&m_poll, &QTimer::timeout, this, &ImageWriter::pollProgress);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(300);
    connect(&m_refreshTimer, &QTimer::timeout, this, &ImageWriter::refresh);

    // drives coming and going
    const QString path = QStringLiteral("/org/freedesktop/UDisks2");
    const QString om = QStringLiteral("org.freedesktop.DBus.ObjectManager");
    bus().connect(Service, path, om, QStringLiteral("InterfacesAdded"), this, SLOT(scheduleRefresh()));
    bus().connect(Service, path, om, QStringLiteral("InterfacesRemoved"), this, SLOT(scheduleRefresh()));
    refresh();
}

ImageWriter::~ImageWriter()
{
    if (m_worker) {
        m_cancel = true;
        m_worker->wait();
    }
}

bool ImageWriter::busy() const
{
    return m_state == QLatin1String("preparing") || m_state == QLatin1String("writing")
        || m_state == QLatin1String("syncing") || m_state == QLatin1String("verifying");
}

void ImageWriter::scheduleRefresh()
{
    m_refreshTimer.start();
}

void ImageWriter::refresh()
{
    const Objects objects = managedObjects();

    // drives with the computer's own filesystems on them are never offered
    QSet<QString> systemDrives;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!it->contains(FilesystemIface))
            continue;
        const QStringList points = mountPoints(it->value(FilesystemIface));
        for (const QString &p : points) {
            if (isSystemMount(p))
                systemDrives.insert(objectPath(it->value(BlockIface).value(QStringLiteral("Drive"))));
        }
    }

    static const bool testLoops = qEnvironmentVariableIsSet("WIN95_IMAGEWRITER_LOOP");
    QVariantList list;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        // whole disks only, not their partitions
        if (!it->contains(BlockIface) || it->contains(PartitionIface))
            continue;
        const QVariantMap block = it->value(BlockIface);
        const QString drivePath = objectPath(block.value(QStringLiteral("Drive")));
        // for testing without a USB stick: loop devices you set up yourself
        // (udisksctl loop-setup -f FILE), which UDisks opens without a password
        if (testLoops && it->contains(QStringLiteral("org.freedesktop.UDisks2.Loop"))
            && block.value(QStringLiteral("Size")).toLongLong() > 0) {
            list << QVariantMap{
                { QStringLiteral("path"), it.key() },
                { QStringLiteral("device"), byteString(block.value(QStringLiteral("Device"))) },
                { QStringLiteral("name"), QStringLiteral("Loop device") },
                { QStringLiteral("size"), block.value(QStringLiteral("Size")).toLongLong() },
            };
            continue;
        }
        if (drivePath.isEmpty() || drivePath == QLatin1String("/") || systemDrives.contains(drivePath))
            continue;
        if (block.value(QStringLiteral("HintIgnore")).toBool())
            continue;
        const qint64 size = block.value(QStringLiteral("Size")).toLongLong();
        if (size <= 0)
            continue;
        const QVariantMap drive = objects.value(drivePath).value(DriveIface);
        const QString busName = drive.value(QStringLiteral("ConnectionBus")).toString();
        const bool removable = drive.value(QStringLiteral("Removable")).toBool()
                            || drive.value(QStringLiteral("MediaRemovable")).toBool();
        if (busName != QLatin1String("usb") && busName != QLatin1String("sdio") && !removable)
            continue;
        // optical drives can't be written like this
        if (drive.value(QStringLiteral("Optical")).toBool()
            || drive.value(QStringLiteral("MediaCompatibility")).toStringList().join(u' ').contains(QLatin1String("optical")))
            continue;

        QString name = (drive.value(QStringLiteral("Vendor")).toString() + u' '
                        + drive.value(QStringLiteral("Model")).toString()).simplified();
        if (name.isEmpty())
            name = busName == QLatin1String("sdio") ? QStringLiteral("SD card") : QStringLiteral("USB drive");
        list << QVariantMap{
            { QStringLiteral("path"), it.key() },
            { QStringLiteral("device"), byteString(block.value(QStringLiteral("PreferredDevice"))).isEmpty()
                                        ? byteString(block.value(QStringLiteral("Device")))
                                        : byteString(block.value(QStringLiteral("PreferredDevice"))) },
            { QStringLiteral("name"), name },
            { QStringLiteral("size"), size },
        };
    }
    if (list != m_drives) {
        m_drives = list;
        emit drivesChanged();
    }
}

QVariantMap ImageWriter::imageInfo(const QString &path) const
{
    QVariantMap info;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        info.insert(QStringLiteral("ok"), false);
        info.insert(QStringLiteral("error"), file.errorString());
        return info;
    }
    const QByteArray head = file.read(512);
    info.insert(QStringLiteral("ok"), file.size() > 0);
    info.insert(QStringLiteral("size"), file.size());
    // a PC boot record (hybrid ISOs and disk images have one; plain CD
    // images, like Windows', don't, and won't start from a USB drive)
    info.insert(QStringLiteral("bootable"),
                head.size() == 512 && quint8(head[510]) == 0x55 && quint8(head[511]) == 0xAA);
    return info;
}

void ImageWriter::setState(const QString &state, const QString &error)
{
    if (m_state == state && m_error == error)
        return;
    m_state = state;
    m_error = error;
    emit stateChanged();
}

void ImageWriter::reset()
{
    if (busy())
        return;
    m_done = m_total = 0;
    m_speed = 0;
    emit progressChanged();
    setState(QStringLiteral("idle"));
}

void ImageWriter::write(const QString &image, const QString &drivePath, bool verify)
{
    if (busy())
        return;
    const QVariantMap info = imageInfo(image);
    if (!info.value(QStringLiteral("ok")).toBool()) {
        setState(QStringLiteral("failed"), QStringLiteral("The image can't be read: %1").arg(info.value(QStringLiteral("error")).toString()));
        return;
    }
    m_image = image;
    m_block = drivePath;
    m_verify = verify;
    m_cancel = false;
    m_done = 0;
    m_total = info.value(QStringLiteral("size")).toLongLong();
    m_speed = 0;
    emit progressChanged();
    setState(QStringLiteral("preparing"));

    // everything mounted from the drive comes off first
    m_toUnmount.clear();
    const Objects objects = managedObjects();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!it->contains(FilesystemIface))
            continue;
        const bool onDrive = it.key() == drivePath
                          || objectPath(it->value(PartitionIface).value(QStringLiteral("Table"))) == drivePath;
        if (onDrive && !mountPoints(it->value(FilesystemIface)).isEmpty())
            m_toUnmount << it.key();
    }
    unmountNext();
}

void ImageWriter::unmountNext()
{
    if (m_cancel) {
        setState(QStringLiteral("cancelled"));
        return;
    }
    if (m_toUnmount.isEmpty()) {
        openDevice();
        return;
    }
    const QString path = m_toUnmount.takeFirst();
    QDBusMessage call = QDBusMessage::createMethodCall(Service, path, FilesystemIface, QStringLiteral("Unmount"));
    call << QVariantMap{};
    call.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(call, PromptTimeout), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        if (watcher->isError()) {
            setState(QStringLiteral("failed"),
                     QStringLiteral("The drive is in use and couldn't be unmounted (%1). Close any programs using it and try again.")
                         .arg(watcher->error().message()));
            return;
        }
        unmountNext();
    });
}

void ImageWriter::openDevice()
{
    // UDisks asks for the administrator password (polkit), then hands us
    // the open drive
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_block, BlockIface, QStringLiteral("OpenDevice"));
    call << QStringLiteral("rw") << QVariantMap{ { QStringLiteral("flags"), int(O_EXCL) } };
    call.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(bus().asyncCall(call, PromptTimeout), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
        if (reply.isError()) {
            const QString name = reply.error().name();
            if (name.endsWith(QLatin1String("NotAuthorizedDismissed")) || name.endsWith(QLatin1String("NotAuthorized"))
                || name.endsWith(QLatin1String("Cancelled")))
                setState(QStringLiteral("cancelled"));
            else
                setState(QStringLiteral("failed"), QStringLiteral("The drive couldn't be opened: %1").arg(reply.error().message()));
            return;
        }
        if (m_cancel) {
            setState(QStringLiteral("cancelled"));
            return;
        }
        // the descriptor in the reply closes with it: keep our own
        const int fd = ::dup(reply.value().fileDescriptor());
        if (fd < 0) {
            setState(QStringLiteral("failed"), QStringLiteral("The drive couldn't be opened: %1").arg(errnoText()));
            return;
        }
        startWorker(fd);
    });
}

void ImageWriter::startWorker(int fd)
{
    m_progress = 0;
    m_phase = 0;
    m_workerError.clear();
    m_samples.clear();
    m_clock.start();
    setState(QStringLiteral("writing"));

    const QString image = m_image;
    const bool verify = m_verify;
    m_worker = QThread::create([this, fd, image, verify] {
        QFile in(image);
        if (!in.open(QIODevice::ReadOnly)) {
            m_workerError = QStringLiteral("The image can't be read: %1").arg(in.errorString());
            ::close(fd);
            return;
        }
        QByteArray buffer(Chunk, Qt::Uninitialized), check(Chunk, Qt::Uninitialized);

        // write
        for (;;) {
            if (m_cancel)
                break;
            const qint64 n = in.read(buffer.data(), Chunk);
            if (n < 0) {
                m_workerError = QStringLiteral("The image can't be read: %1").arg(in.errorString());
                break;
            }
            if (n == 0)
                break;
            if (!writeAll(fd, buffer.constData(), n)) {
                m_workerError = errno == ENOSPC ? QStringLiteral("The image doesn't fit on the drive.")
                                                : QStringLiteral("Writing to the drive failed: %1").arg(errnoText());
                break;
            }
            m_progress += n;
        }

        // the drive catches up with what's still in memory
        if (m_workerError.isEmpty()) {
            m_phase = 1;
            if (::fdatasync(fd) != 0)
                m_workerError = QStringLiteral("Writing to the drive failed: %1").arg(errnoText());
        }

        // read it back from the drive itself (not the copy in memory)
        if (m_workerError.isEmpty() && !m_cancel && verify) {
            m_phase = 2;
            m_progress = 0;
            ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
            if (::lseek(fd, 0, SEEK_SET) != 0 || !in.seek(0)) {
                m_workerError = QStringLiteral("Checking the drive failed: %1").arg(errnoText());
            } else {
                for (;;) {
                    if (m_cancel)
                        break;
                    const qint64 n = in.read(buffer.data(), Chunk);
                    if (n <= 0)
                        break;
                    const qint64 got = readAll(fd, check.data(), n);
                    if (got != n) {
                        m_workerError = QStringLiteral("Reading the drive back failed: %1").arg(got < 0 ? errnoText() : QStringLiteral("it's smaller than the image"));
                        break;
                    }
                    if (std::memcmp(buffer.constData(), check.constData(), size_t(n)) != 0) {
                        m_workerError = QStringLiteral("The drive doesn't hold what was written to it (at %1 MB). "
                                                       "It may be faulty; try another one.")
                                            .arg(m_progress.load() / (1024 * 1024));
                        break;
                    }
                    m_progress += n;
                }
            }
        }
        ::close(fd);
    });
    connect(m_worker, &QThread::finished, this, [this] {
        m_worker->deleteLater();
        m_worker = nullptr;
        m_poll.stop();
        pollProgress();
        if (!m_workerError.isEmpty())
            setState(QStringLiteral("failed"), m_workerError);
        else if (m_cancel)
            setState(QStringLiteral("cancelled"));
        else
            setState(QStringLiteral("done"));
        // the kernel rereads the drive's new partitions
        QDBusMessage rescan = QDBusMessage::createMethodCall(Service, m_block, BlockIface, QStringLiteral("Rescan"));
        rescan << QVariantMap{};
        bus().asyncCall(rescan);
        scheduleRefresh();
    });
    m_worker->start();
    m_poll.start();
}

void ImageWriter::pollProgress()
{
    static const char *states[] = { "writing", "syncing", "verifying" };
    const int phase = m_phase;
    if (busy() && m_state != QLatin1String(states[phase])) {
        m_samples.clear();
        setState(QString::fromLatin1(states[phase]));
    }
    const qint64 done = m_progress;
    // the speed over the last 3 seconds
    const qint64 now = m_clock.elapsed();
    m_samples.append({ now, done });
    while (m_samples.size() > 1 && now - m_samples.constFirst().first > 3000)
        m_samples.removeFirst();
    double speed = 0;
    if (m_samples.size() > 1) {
        const auto &first = m_samples.constFirst();
        const qint64 ms = now - first.first;
        if (ms > 0)
            speed = double(done - first.second) * 1000.0 / ms;
    }
    if (done != m_done || speed != m_speed) {
        m_done = done;
        m_speed = speed;
        emit progressChanged();
    }
}

void ImageWriter::cancel()
{
    if (busy())
        m_cancel = true;
}
