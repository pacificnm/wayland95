#include "screencopy.h"

#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QScreen>
#include <QStandardPaths>
#include <QFile>
#include <QThreadPool>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandintegration_p.h>
#include <QtWaylandClient/private/qwaylandshm_p.h>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

#include "desktopkeys.h"

using namespace QtWaylandClient;

// --------------------------------------------------------- one capture

class CaptureFrame : public QObject, public QtWayland::zwlr_screencopy_frame_v1
{
public:
    CaptureFrame(ScreenCopy *owner, int id, const QString &folder, ::zwlr_screencopy_frame_v1 *frame, uint version)
        : QtWayland::zwlr_screencopy_frame_v1(frame), m_owner(owner), m_id(id), m_folder(folder), m_version(version) {}
    ~CaptureFrame() override
    {
        if (m_buffer)
            wl_buffer_destroy(m_buffer);
        if (m_data)
            munmap(m_data, m_size);
        destroy();
    }

protected:
    void zwlr_screencopy_frame_v1_buffer(uint32_t format, uint32_t width, uint32_t height, uint32_t stride) override
    {
        if (format != WL_SHM_FORMAT_ARGB8888 && format != WL_SHM_FORMAT_XRGB8888)
            return; // (another offer may suit)
        m_format = format;
        m_width = int(width);
        m_height = int(height);
        m_stride = int(stride);
        if (m_version < 3)
            copyNow();
    }
    void zwlr_screencopy_frame_v1_buffer_done() override { copyNow(); }
    void zwlr_screencopy_frame_v1_flags(uint32_t flags) override { m_yInvert = flags & flags_y_invert; }
    void zwlr_screencopy_frame_v1_ready(uint32_t, uint32_t, uint32_t) override
    {
        QImage image(static_cast<const uchar *>(m_data), m_width, m_height, m_stride,
                     m_format == WL_SHM_FORMAT_ARGB8888 ? QImage::Format_ARGB32 : QImage::Format_RGB32);
        QImage copy = image.copy();
        if (m_yInvert)
            copy = copy.mirrored(false, true);
        done(copy);
    }
    void zwlr_screencopy_frame_v1_failed() override { done(QImage()); }

private:
    void copyNow()
    {
        if (m_buffer || m_width <= 0) {
            if (m_width <= 0)
                done(QImage());
            return;
        }
        m_size = size_t(m_stride) * size_t(m_height);
        const int fd = memfd_create("wayland95-screencopy", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, off_t(m_size)) < 0) {
            if (fd >= 0)
                close(fd);
            done(QImage());
            return;
        }
        m_data = mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        auto *shm = QWaylandIntegration::instance()->display()->shm();
        ::wl_shm_pool *pool = wl_shm_create_pool(shm->object(), fd, int32_t(m_size));
        m_buffer = wl_shm_pool_create_buffer(pool, 0, m_width, m_height, m_stride, m_format);
        wl_shm_pool_destroy(pool);
        close(fd);
        if (m_data == MAP_FAILED) {
            m_data = nullptr;
            done(QImage());
            return;
        }
        copy(m_buffer);
    }
    void done(const QImage &image)
    {
        if (m_finished)
            return;
        m_finished = true;
        m_owner->finished(m_id, image, m_folder);
        deleteLater();
    }

    ScreenCopy *m_owner;
    int m_id;
    QString m_folder;
    uint m_version;
    uint32_t m_format = 0;
    int m_width = 0, m_height = 0, m_stride = 0;
    bool m_yInvert = false, m_finished = false;
    ::wl_buffer *m_buffer = nullptr;
    void *m_data = nullptr;
    size_t m_size = 0;
};

// ------------------------------------------------------ the window list

class WindowList : public QObject, public QtWayland::wayland95_window_list_v1
{
public:
    WindowList(ScreenCopy *owner, const QString &tag, ::wayland95_window_list_v1 *list)
        : QtWayland::wayland95_window_list_v1(list), m_owner(owner), m_tag(tag) {}
    ~WindowList() override { destroy(); }

protected:
    void wayland95_window_list_v1_window(int32_t x, int32_t y, int32_t width, int32_t height) override
    {
        m_windows << QVariantMap{ { QStringLiteral("x"), x }, { QStringLiteral("y"), y },
                                  { QStringLiteral("width"), width }, { QStringLiteral("height"), height } };
    }
    void wayland95_window_list_v1_done() override
    {
        emit m_owner->windowsListed(m_tag, m_windows);
        deleteLater();
    }

private:
    ScreenCopy *m_owner;
    QString m_tag;
    QVariantList m_windows;
};

// -------------------------------------------------------------- ScreenCopy

ScreenCopy::ScreenCopy(QObject *parent)
    : QObject(parent)
    , m_manager(new ScreencopyManager)
{
    m_manager->setParent(this);
}

int ScreenCopy::take(const QRect &rect, const QString &folder)
{
    const int id = m_nextId++;
    // the main display (where the capture tool shows)
    QScreen *screen = QGuiApplication::primaryScreen();
    const QString mainName = ShellExtension::instance()->primaryOutput();
    for (QScreen *s : QGuiApplication::screens())
        if (!mainName.isEmpty() && s->name() == mainName)
            screen = s;
    auto *native = screen ? screen->nativeInterface<QNativeInterface::QWaylandScreen>() : nullptr;
    if (!m_manager->isActive() || !native || !native->output()) {
        std::fprintf(stderr, "win95shell: no way to take a picture of the screen (screencopy)\n");
        QMetaObject::invokeMethod(this, [this, id] { emit taken(id, QString()); }, Qt::QueuedConnection);
        return id;
    }
    const QRect all(QPoint(0, 0), screen->geometry().size());
    const QRect r = rect.isEmpty() ? all : rect.intersected(all);
    auto *frame = m_manager->capture_output_region(0, native->output(), r.x(), r.y(), r.width(), r.height());
    auto *capture = new CaptureFrame(this, id, folder, frame, uint(m_manager->QtWayland::zwlr_screencopy_manager_v1::version()));
    capture->setParent(this);
    return id;
}

void ScreenCopy::finished(int id, const QImage &image, const QString &folder)
{
    if (image.isNull()) {
        std::fprintf(stderr, "win95shell: the screen picture failed\n");
        emit taken(id, QString());
        return;
    }
    // compressing a screenful to PNG takes a moment: not on the desktop's
    // own thread (the taskbar and menus would stand still meanwhile)
    QThreadPool::globalInstance()->start([this, id, image, folder] {
        if (folder == QLatin1String("Clipboard")) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            // for the thumbnail
            const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wayland95");
            QDir().mkpath(dir);
            const QString path = dir + QStringLiteral("/clipboard-screenshot.png");
            QFile file(path);
            if (file.open(QIODevice::WriteOnly))
                file.write(png);
            file.close();
            // (the clipboard is the main thread's)
            QMetaObject::invokeMethod(this, [this, id, png, path, size = image.size()] {
                auto *data = new QMimeData;
                data->setData(QStringLiteral("image/png"), png);
                QGuiApplication::clipboard()->setMimeData(data);
                std::fprintf(stderr, "win95shell: screen picture %dx%d on the clipboard\n", size.width(), size.height());
                emit taken(id, path);
            }, Qt::QueuedConnection);
            return;
        }
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd 'at' hh.mm.ss"));
        QString path = folder + QStringLiteral("/Screenshot ") + stamp + QStringLiteral(".png");
        for (int n = 2; QFileInfo::exists(path); ++n)
            path = folder + QStringLiteral("/Screenshot ") + stamp + QStringLiteral(" (%1).png").arg(n);
        const bool ok = image.save(path, "PNG");
        std::fprintf(stderr, "win95shell: screen picture %dx%d %s %s\n", image.width(), image.height(),
                     ok ? "saved as" : "couldn't be saved as", qPrintable(path));
        QMetaObject::invokeMethod(this, [this, id, ok, path] { emit taken(id, ok ? path : QString()); },
                                  Qt::QueuedConnection);
    });
}

QString ScreenCopy::folder(const QString &name) const
{
    QStandardPaths::StandardLocation where = QStandardPaths::PicturesLocation;
    if (name == QLatin1String("Desktop"))
        where = QStandardPaths::DesktopLocation;
    else if (name == QLatin1String("Documents"))
        where = QStandardPaths::DocumentsLocation;
    QString dir = QStandardPaths::writableLocation(where);
    if (dir.isEmpty())
        dir = QDir::homePath();
    QDir().mkpath(dir);
    return dir;
}

QString ScreenCopy::printFolder() const
{
    const QString pictures = QDir::homePath() + QStringLiteral("/Pictures");
    return QFileInfo(pictures).isDir() ? pictures : QDir::homePath();
}

void ScreenCopy::listWindows(const QString &tag)
{
    ShellExtension *shell = ShellExtension::instance();
    if (!shell->isActive()) {
        QMetaObject::invokeMethod(this, [this, tag] { emit windowsListed(tag, {}); }, Qt::QueuedConnection);
        return;
    }
    new WindowList(this, tag, shell->list_windows());
}
