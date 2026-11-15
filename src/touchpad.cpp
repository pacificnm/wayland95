#include "touchpad.h"

#include <QCursor>
#include <QGuiApplication>
#include <QMutex>
#include <QPointer>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

#include <cmath>
#include <dlfcn.h>
#include <libinput.h>

namespace {

struct Settings {
    bool tapToClick = true;
    QString scrollMethod = QStringLiteral("twofinger");
    bool naturalScroll = false;
    double scrollSpeed = 1.0;
    bool disableWhileTyping = true;
    QString clickMethod = QStringLiteral("default");
    double speed = 0;
    bool acceleration = true;
    bool disableWithMouse = false;
    bool middleEmulation = false;
};

// shared between Qt's input handling (where libinput events come through)
// and the settings (from QML); both run on the main thread, the mutex is
// for safety
QMutex mutex;
Settings current;
QList<libinput_device *> touchpads;
QList<QPointer<TouchpadControl>> controls;
double scrollRest[2] = { 0, 0 }; // fractions of an angle step carried over
double pixelRest[2] = { 0, 0 };

bool isTouchpad(libinput_device *d)
{
    return libinput_device_has_capability(d, LIBINPUT_DEVICE_CAP_POINTER)
           && libinput_device_config_tap_get_finger_count(d) > 0;
}

void apply(libinput_device *d, const Settings &s)
{
    if (libinput_device_config_tap_get_finger_count(d) > 0)
        libinput_device_config_tap_set_enabled(d, s.tapToClick ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);

    const uint32_t methods = libinput_device_config_scroll_get_methods(d);
    libinput_config_scroll_method method = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
    if (s.scrollMethod == QLatin1String("twofinger") && (methods & LIBINPUT_CONFIG_SCROLL_2FG))
        method = LIBINPUT_CONFIG_SCROLL_2FG;
    else if (s.scrollMethod == QLatin1String("edge") && (methods & LIBINPUT_CONFIG_SCROLL_EDGE))
        method = LIBINPUT_CONFIG_SCROLL_EDGE;
    else if (s.scrollMethod != QLatin1String("none"))
        method = libinput_device_config_scroll_get_default_method(d);
    libinput_device_config_scroll_set_method(d, method);

    if (libinput_device_config_scroll_has_natural_scroll(d))
        libinput_device_config_scroll_set_natural_scroll_enabled(d, s.naturalScroll);
    if (libinput_device_config_dwt_is_available(d))
        libinput_device_config_dwt_set_enabled(d, s.disableWhileTyping ? LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED);

    const uint32_t clicks = libinput_device_config_click_get_methods(d);
    if (s.clickMethod == QLatin1String("areas") && (clicks & LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS))
        libinput_device_config_click_set_method(d, LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS);
    else if (s.clickMethod == QLatin1String("fingers") && (clicks & LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER))
        libinput_device_config_click_set_method(d, LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
    else if (clicks)
        libinput_device_config_click_set_method(d, libinput_device_config_click_get_default_method(d));

    if (libinput_device_config_accel_is_available(d)) {
        libinput_device_config_accel_set_speed(d, qBound(-1.0, s.speed, 1.0));
        libinput_device_config_accel_set_profile(d, s.acceleration ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
                                                                   : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
    }
    const uint32_t modes = libinput_device_config_send_events_get_modes(d);
    if (s.disableWithMouse && (modes & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE))
        libinput_device_config_send_events_set_mode(d, LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE);
    else
        libinput_device_config_send_events_set_mode(d, LIBINPUT_CONFIG_SEND_EVENTS_ENABLED);
    if (libinput_device_config_middle_emulation_is_available(d))
        libinput_device_config_middle_emulation_set_enabled(d, s.middleEmulation ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED
                                                                                  : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
}

void devicesChanged()
{
    for (const QPointer<TouchpadControl> &c : std::as_const(controls))
        if (c)
            QMetaObject::invokeMethod(c, "devicesChanged", Qt::QueuedConnection);
}

// finger (and continuous) scrolling: smooth scroll events, in both Qt's
// units (an "angle" of 120 per wheel notch) and pixels
void scroll(libinput_event_pointer *p)
{
    QPointF angle, pixels;
    const libinput_pointer_axis axes[2] = { LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL };
    double speed;
    {
        QMutexLocker lock(&mutex);
        speed = current.scrollSpeed;
    }
    bool any = false;
    for (int i = 0; i < 2; ++i) {
        if (!libinput_event_pointer_has_axis(p, axes[i]))
            continue;
        // libinput: positive scrolls down/right; one wheel notch is 15
        const double v = libinput_event_pointer_get_scroll_value(p, axes[i]) * speed;
        if (v == 0) { // fingers lifted: start afresh
            scrollRest[i] = pixelRest[i] = 0;
            continue;
        }
        scrollRest[i] += -v * 8;
        pixelRest[i] += -v;
        const double a = std::trunc(scrollRest[i]);
        const double px = std::trunc(pixelRest[i]);
        scrollRest[i] -= a;
        pixelRest[i] -= px;
        (i == 0 ? angle.rx() : angle.ry()) = a;
        (i == 0 ? pixels.rx() : pixels.ry()) = px;
        any = any || a != 0 || px != 0;
    }
    if (!any)
        return;
    const QPoint global = QCursor::pos();
    QWindow *window = nullptr;
    for (QWindow *w : QGuiApplication::topLevelWindows())
        if (w->isVisible() && w->geometry().contains(global))
            window = w;
    if (!window)
        return;
    QWindowSystemInterface::handleWheelEvent(window, QPointF(global - window->position()), QPointF(global),
                                             pixels.toPoint(), angle.toPoint(), QGuiApplication::keyboardModifiers());
}

// true: the event was used up here
bool filter(libinput_event *ev)
{
    switch (libinput_event_get_type(ev)) {
    case LIBINPUT_EVENT_DEVICE_ADDED: {
        libinput_device *d = libinput_event_get_device(ev);
        if (isTouchpad(d)) {
            QMutexLocker lock(&mutex);
            touchpads << libinput_device_ref(d);
            apply(d, current);
            qInfo().noquote() << "Touchpad:" << libinput_device_get_name(d);
            devicesChanged();
        }
        return false; // Qt sets up the device too
    }
    case LIBINPUT_EVENT_DEVICE_REMOVED: {
        libinput_device *d = libinput_event_get_device(ev);
        QMutexLocker lock(&mutex);
        if (touchpads.removeAll(d)) {
            libinput_device_unref(d);
            devicesChanged();
        }
        return false;
    }
    case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
    case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS:
        scroll(libinput_event_get_pointer_event(ev));
        return true;
    default:
        return false;
    }
}

} // namespace

// Qt's libinput handling calls this; the program's own definition wins over
// libinput's (the compositor is linked with its symbols exported).
extern "C" Q_DECL_EXPORT libinput_event *libinput_get_event(libinput *li)
{
    using GetEvent = libinput_event *(*)(libinput *);
    static const auto real = reinterpret_cast<GetEvent>(dlsym(RTLD_NEXT, "libinput_get_event"));
    if (!real)
        return nullptr;
    for (;;) {
        libinput_event *ev = real(li);
        if (!ev || !filter(ev))
            return ev;
        libinput_event_destroy(ev);
    }
}

// ---------------------------------------------------------------------------

TouchpadControl::TouchpadControl(QObject *parent)
    : QObject(parent)
{
    QMutexLocker lock(&mutex);
    controls << this;
}

TouchpadControl::~TouchpadControl()
{
    QMutexLocker lock(&mutex);
    controls.removeAll(this);
}

QVariantMap TouchpadControl::settings() const
{
    QMutexLocker lock(&mutex);
    return {
        { QStringLiteral("tapToClick"), current.tapToClick },
        { QStringLiteral("scrollMethod"), current.scrollMethod },
        { QStringLiteral("naturalScroll"), current.naturalScroll },
        { QStringLiteral("scrollSpeed"), qRound(current.scrollSpeed * 10) },
        { QStringLiteral("disableWhileTyping"), current.disableWhileTyping },
        { QStringLiteral("clickMethod"), current.clickMethod },
        { QStringLiteral("speed"), qRound(current.speed * 10) },
        { QStringLiteral("acceleration"), current.acceleration },
        { QStringLiteral("disableWithMouse"), current.disableWithMouse },
        { QStringLiteral("middleEmulation"), current.middleEmulation },
    };
}

void TouchpadControl::setSettings(const QVariantMap &m)
{
    {
        QMutexLocker lock(&mutex);
        current.tapToClick = m.value(QStringLiteral("tapToClick"), true).toBool();
        current.scrollMethod = m.value(QStringLiteral("scrollMethod"), QStringLiteral("twofinger")).toString();
        current.naturalScroll = m.value(QStringLiteral("naturalScroll")).toBool();
        current.scrollSpeed = qBound(1, m.value(QStringLiteral("scrollSpeed"), 10).toInt(), 20) / 10.0;
        current.disableWhileTyping = m.value(QStringLiteral("disableWhileTyping"), true).toBool();
        current.clickMethod = m.value(QStringLiteral("clickMethod"), QStringLiteral("default")).toString();
        current.speed = qBound(-10, m.value(QStringLiteral("speed")).toInt(), 10) / 10.0;
        current.acceleration = m.value(QStringLiteral("acceleration"), true).toBool();
        current.disableWithMouse = m.value(QStringLiteral("disableWithMouse")).toBool();
        current.middleEmulation = m.value(QStringLiteral("middleEmulation")).toBool();
        for (libinput_device *d : std::as_const(touchpads))
            apply(d, current);
    }
    emit settingsChanged();
}

QStringList TouchpadControl::devices() const
{
    QMutexLocker lock(&mutex);
    QStringList names;
    for (libinput_device *d : touchpads)
        names << QString::fromUtf8(libinput_device_get_name(d));
    return names;
}
