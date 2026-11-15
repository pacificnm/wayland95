#include "accessibility.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QtEndian>
#include <QtMath>

namespace {

bool isModifier(int key)
{
    switch (key) {
    case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Alt: case Qt::Key_Meta:
    case Qt::Key_AltGr: case Qt::Key_Super_L: case Qt::Key_Super_R:
        return true;
    default:
        return false;
    }
}

QString modifierName(int key)
{
    switch (key) {
    case Qt::Key_Shift: return QStringLiteral("Shift");
    case Qt::Key_Control: return QStringLiteral("Ctrl");
    case Qt::Key_Alt: return QStringLiteral("Alt");
    case Qt::Key_AltGr: return QStringLiteral("AltGr");
    default: return QStringLiteral("Windows");
    }
}

// evdev key codes + 8, as XKB (and so Qt's native scan codes) number them
enum : quint32 {
    ScanRightShift = 62, ScanKpAsterisk = 63, ScanKp7 = 79, ScanKp8 = 80, ScanKp9 = 81, ScanKpMinus = 82,
    ScanKp4 = 83, ScanKp5 = 84, ScanKp6 = 85, ScanKpPlus = 86, ScanKp1 = 87, ScanKp2 = 88, ScanKp3 = 89,
    ScanKp0 = 90, ScanKpDot = 91, ScanKpSlash = 106,
};

QPoint direction(quint32 scan)
{
    switch (scan) {
    case ScanKp1: return { -1, 1 };
    case ScanKp2: return { 0, 1 };
    case ScanKp3: return { 1, 1 };
    case ScanKp4: return { -1, 0 };
    case ScanKp6: return { 1, 0 };
    case ScanKp7: return { -1, -1 };
    case ScanKp8: return { 0, -1 };
    case ScanKp9: return { 1, -1 };
    default: return {};
    }
}

bool primarySession()
{
    const QString p = QGuiApplication::platformName();
    return p == QLatin1String("eglfs") || p == QLatin1String("linuxfb");
}

} // namespace

AccessibilityAdaptor::AccessibilityAdaptor(AccessibilityController *controller)
    : QObject(controller)
    , m_controller(controller)
{
}

void AccessibilityAdaptor::SoundPlayed(const QString &event) { m_controller->soundPlayed(event); }

AccessibilityController::AccessibilityController(QObject *parent)
    : QObject(parent)
{
    m_lastInput.start();
    m_sinceRelease.start();
    m_shiftTimer.start();
    m_moveTimer.setInterval(16);
    connect(&m_moveTimer, &QTimer::timeout, this, &AccessibilityController::moveStep);
    m_rightShiftHold.setSingleShot(true);
    m_rightShiftHold.setInterval(8000);
    connect(&m_rightShiftHold, &QTimer::timeout, this, [this] {
        if (m_shortcuts.contains(QLatin1String("filterKeys")))
            emit shortcutPressed(QStringLiteral("filterKeys"));
    });
    m_numLockHold.setSingleShot(true);
    m_numLockHold.setInterval(5000);
    connect(&m_numLockHold, &QTimer::timeout, this, [this] {
        if (m_shortcuts.contains(QLatin1String("toggleKeys")))
            emit shortcutPressed(QStringLiteral("toggleKeys"));
    });
    qApp->installEventFilter(this);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected() && bus.registerService(QStringLiteral("org.wayland95.Accessibility")))
        bus.registerObject(QStringLiteral("/org/wayland95/Accessibility"), new AccessibilityAdaptor(this),
                           QDBusConnection::ExportAllSlots);
}

AccessibilityController::~AccessibilityController()
{
    qApp->removeEventFilter(this);
}

void AccessibilityController::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    m_window = window;
    if (window)
        m_pointer = QPointF(window->width() / 2.0, window->height() / 2.0);
    emit windowChanged();
}

void AccessibilityController::setStickyKeys(bool on)
{
    if (on == m_sticky)
        return;
    m_sticky = on;
    if (!on)
        clearSticky();
    emit settingsChanged();
}

void AccessibilityController::setMouseKeys(bool on)
{
    if (on == m_mouseKeys)
        return;
    m_mouseKeys = on;
    if (!on) {
        m_moveTimer.stop();
        m_moveKeys.clear();
        if (m_held)
            mouseButton(QEvent::MouseButtonRelease, Qt::NoButton);
        m_pointerMoved = false;
        emit pointerChanged();
    }
    emit settingsChanged();
}

QStringList AccessibilityController::heldModifiers() const
{
    QStringList out;
    for (auto it = m_modifiers.cbegin(); it != m_modifiers.cend(); ++it) {
        if (it->state == Latched)
            out << modifierName(it.key());
        else if (it->state == Locked)
            out << modifierName(it.key()) + QStringLiteral(" (locked)");
    }
    out.sort();
    return out;
}

AccessibilityController::Key AccessibilityController::keyOf(const QKeyEvent *e)
{
    return { e->key(), e->nativeScanCode(), e->nativeVirtualKey(), e->nativeModifiers(), e->modifiers(), e->text() };
}

void AccessibilityController::deliver(QEvent::Type type, const Key &k)
{
    if (!m_window)
        return;
    QKeyEvent ev(type, k.key, k.mods, k.scan, k.vkey, k.nmods, type == QEvent::KeyPress ? k.text : QString());
    m_synthesizing = true;
    QCoreApplication::sendEvent(m_window, &ev);
    m_synthesizing = false;
}

void AccessibilityController::clearSticky()
{
    for (auto it = m_modifiers.begin(); it != m_modifiers.end(); ++it) {
        if (it->state != Up && !it->down)
            deliver(QEvent::KeyRelease, it->press);
        it->state = Up;
        it->swallowRelease = false;
    }
    emit heldChanged();
}

void AccessibilityController::releaseLatched()
{
    bool any = false;
    for (auto it = m_modifiers.begin(); it != m_modifiers.end(); ++it) {
        if (it->state != Latched)
            continue;
        if (!it->down)
            deliver(QEvent::KeyRelease, it->press);
        it->state = Up;
        any = true;
    }
    if (any)
        emit heldChanged();
}

bool AccessibilityController::sticky(QEvent::Type type, const Key &k, bool autoRepeat)
{
    if (autoRepeat)
        return false;
    if (!isModifier(k.key)) {
        if (type == QEvent::KeyPress) {
            for (Modifier &m : m_modifiers)
                if (m.down)
                    m.usedWithKey = true;
        } else {
            // the key that used the latched modifiers is done: let them go
            // (after its release has reached the program)
            QTimer::singleShot(0, this, &AccessibilityController::releaseLatched);
        }
        return false;
    }

    Modifier &m = m_modifiers[k.key];
    if (type == QEvent::KeyPress) {
        if (m.state == Locked) {
            // third press: unlocks; its release lets the program see it go
            m.state = Up;
            emit heldChanged();
            if (m_stickySound)
                playTone(QStringLiteral("low"));
            return true;
        }
        if (m.state == Latched) {
            // second press: locks it (or, without locking, lets it go)
            m.state = m_stickyLock ? Locked : Up;
            m.swallowRelease = m_stickyLock;
            emit heldChanged();
            if (m_stickySound)
                playTone(m_stickyLock ? QStringLiteral("high") : QStringLiteral("low"));
            return true;
        }
        m.press = k;
        m.down = true;
        m.usedWithKey = false;
        return false;
    }

    // release
    if (m.swallowRelease) {
        m.swallowRelease = false;
        m.down = false;
        return true;
    }
    const bool wasDown = m.down;
    m.down = false;
    if (m.state == Up && wasDown && !m.usedWithKey) {
        // pressed and let go on its own: it stays down for the next key
        m.state = Latched;
        emit heldChanged();
        if (m_stickySound)
            playTone(QStringLiteral("click"));
        return true;
    }
    return false;
}

void AccessibilityController::shortcutKeys(QEvent::Type type, const Key &k, bool autoRepeat)
{
    if (autoRepeat)
        return;
    if (type == QEvent::KeyPress) {
        // Shift five times: StickyKeys
        if (k.key == Qt::Key_Shift) {
            if (m_shiftTimer.elapsed() > 1500)
                m_shiftPresses = 0;
            m_shiftTimer.restart();
            if (++m_shiftPresses == 5) {
                m_shiftPresses = 0;
                if (m_shortcuts.contains(QLatin1String("stickyKeys")))
                    emit shortcutPressed(QStringLiteral("stickyKeys"));
            }
            // right Shift held for eight seconds: FilterKeys
            if (k.scan == ScanRightShift)
                m_rightShiftHold.start();
        } else {
            m_shiftPresses = 0;
            m_rightShiftHold.stop();
        }
        const bool altShift = (k.mods & Qt::AltModifier) && (k.mods & Qt::ShiftModifier);
        if (k.key == Qt::Key_NumLock) {
            // Left Alt + Left Shift + Num Lock: MouseKeys; Num Lock held for
            // five seconds: ToggleKeys
            if (altShift) {
                if (m_shortcuts.contains(QLatin1String("mouseKeys")))
                    emit shortcutPressed(QStringLiteral("mouseKeys"));
            } else {
                m_numLockHold.start();
            }
        } else {
            m_numLockHold.stop();
        }
        // Left Alt + Left Shift + Print Screen: High Contrast
        if (k.key == Qt::Key_Print && altShift && m_shortcuts.contains(QLatin1String("highContrast")))
            emit shortcutPressed(QStringLiteral("highContrast"));
    } else {
        if (k.scan == ScanRightShift)
            m_rightShiftHold.stop();
        if (k.key == Qt::Key_NumLock)
            m_numLockHold.stop();
    }
}

bool AccessibilityController::mouseKey(QEvent::Type type, const Key &k, bool autoRepeat)
{
    if (!m_mouseKeys || !m_window || !(k.mods & Qt::KeypadModifier))
        return false;
    const quint32 scan = k.scan;
    const bool digitKeys = (k.key >= Qt::Key_0 && k.key <= Qt::Key_9) || k.key == Qt::Key_Period || k.key == Qt::Key_Comma;
    const bool numberPadKey = !direction(scan).isNull() || scan == ScanKp5 || scan == ScanKp0 || scan == ScanKpDot;
    // the digit keys move the pointer only with Num Lock as chosen; the
    // operator keys always choose buttons
    if (numberPadKey && digitKeys != m_mouseNumLockOn)
        return false;
    const bool press = type == QEvent::KeyPress;

    if (!direction(scan).isNull()) {
        if (press && !autoRepeat) {
            m_moveKeys.insert(scan);
            if (!m_moveTimer.isActive()) {
                m_moveSince.restart();
                m_moveStep.restart();
                moveStep();
                m_moveTimer.start();
            }
        } else if (!press && !autoRepeat) {
            m_moveKeys.remove(scan);
            if (m_moveKeys.isEmpty())
                m_moveTimer.stop();
        }
        return true;
    }

    if (press && !autoRepeat) {
        switch (scan) {
        case ScanKp5: // click
            mouseButton(QEvent::MouseButtonPress, m_mouseButton);
            mouseButton(QEvent::MouseButtonRelease, m_mouseButton);
            break;
        case ScanKpPlus: // double-click
            for (int i = 0; i < 2; ++i) {
                mouseButton(QEvent::MouseButtonPress, m_mouseButton);
                mouseButton(QEvent::MouseButtonRelease, m_mouseButton);
            }
            break;
        case ScanKpSlash: m_mouseButton = Qt::LeftButton; break;
        case ScanKpAsterisk: m_mouseButton = Qt::MiddleButton; break;
        case ScanKpMinus: m_mouseButton = Qt::RightButton; break;
        case ScanKp0: // hold the button down (dragging)
            if (!(m_held & m_mouseButton))
                mouseButton(QEvent::MouseButtonPress, m_mouseButton);
            break;
        case ScanKpDot: // let it go
            if (m_held)
                mouseButton(QEvent::MouseButtonRelease, Qt::NoButton);
            break;
        default:
            return false;
        }
        return true;
    }
    switch (scan) {
    case ScanKp5: case ScanKpPlus: case ScanKpSlash: case ScanKpAsterisk: case ScanKpMinus: case ScanKp0: case ScanKpDot:
        return true; // their releases and repeats
    default:
        return false;
    }
}

void AccessibilityController::moveStep()
{
    if (!m_window)
        return;
    QPoint d;
    for (quint32 scan : std::as_const(m_moveKeys))
        d += direction(scan);
    d.setX(qBound(-1, d.x(), 1));
    d.setY(qBound(-1, d.y(), 1));
    if (d.isNull())
        return;
    // speeds up to the top speed; acceleration sets how quickly
    const double top = 60 + m_mouseSpeed * 90;              // pixels a second
    const double ramp = (11 - qBound(1, m_mouseAccel, 10)) * 0.25; // seconds to get there
    const double t = m_moveSince.elapsed() / 1000.0;
    const double dt = qMax(0.001, m_moveStep.restart() / 1000.0);
    const double v = top * qMin(1.0, 0.08 + t / ramp);
    double step = m_moveTimer.isActive() ? v * dt : 1; // the first press nudges by a pixel
    if (d.x() && d.y())
        step /= M_SQRT2;
    m_pointer.setX(qBound(0.0, m_pointer.x() + d.x() * step, double(m_window->width() - 1)));
    m_pointer.setY(qBound(0.0, m_pointer.y() + d.y() * step, double(m_window->height() - 1)));
    m_pointerMoved = true;
    emit pointerChanged();

    const QPointF global = m_window->mapToGlobal(m_pointer);
    if (primarySession())
        QCursor::setPos(m_window->screen(), global.toPoint());
    QMouseEvent ev(QEvent::MouseMove, m_pointer, m_pointer, global, Qt::NoButton, m_held, QGuiApplication::keyboardModifiers());
    m_synthesizing = true;
    QCoreApplication::sendEvent(m_window, &ev);
    m_synthesizing = false;
}

void AccessibilityController::mouseButton(QEvent::Type type, Qt::MouseButton button)
{
    if (!m_window)
        return;
    // NoButton on release: let go of whatever is held
    const QList<Qt::MouseButton> buttons = button != Qt::NoButton ? QList<Qt::MouseButton>{ button }
        : QList<Qt::MouseButton>{ Qt::LeftButton, Qt::MiddleButton, Qt::RightButton };
    for (Qt::MouseButton b : buttons) {
        if (type == QEvent::MouseButtonRelease && button == Qt::NoButton && !(m_held & b))
            continue;
        if (type == QEvent::MouseButtonPress)
            m_held |= b;
        else
            m_held &= ~b;
        const QPointF global = m_window->mapToGlobal(m_pointer);
        QMouseEvent ev(type, m_pointer, m_pointer, global, b, m_held, QGuiApplication::keyboardModifiers());
        m_synthesizing = true;
        QCoreApplication::sendEvent(m_window, &ev);
        m_synthesizing = false;
    }
}

bool AccessibilityController::eventFilter(QObject *watched, QEvent *event)
{
    if (m_synthesizing || watched != m_window)
        return false;

    switch (event->type()) {
    case QEvent::MouseMove: {
        m_lastInput.restart();
        // the real mouse moved: MouseKeys carries on from where it is
        m_pointer = static_cast<QMouseEvent *>(event)->scenePosition();
        if (m_pointerMoved) {
            m_pointerMoved = false;
            emit pointerChanged();
        }
        return false;
    }
    case QEvent::MouseButtonPress:
        m_lastInput.restart();
        m_pointer = static_cast<QMouseEvent *>(event)->scenePosition();
        return false;
    case QEvent::MouseButtonRelease:
        m_lastInput.restart();
        // a Shift-click with a latched Shift: let it go afterwards
        if (m_sticky)
            QTimer::singleShot(0, this, &AccessibilityController::releaseLatched);
        return false;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        break;
    default:
        return false;
    }

    m_lastInput.restart();
    const auto *ke = static_cast<QKeyEvent *>(event);
    const Key k = keyOf(ke);
    const bool rep = ke->isAutoRepeat();
    const QEvent::Type type = event->type();
    shortcutKeys(type, k, rep);

    // FilterKeys first: what it lets through goes on to the rest
    if (m_filter) {
        if (type == QEvent::KeyPress) {
            if (!rep && m_bounceMs > 0 && k.scan == quint32(m_lastReleasedScan) && m_sinceRelease.elapsed() < m_bounceMs) {
                m_bounced.insert(k.scan);
                return true;
            }
            if (m_bounced.contains(k.scan) || m_slowPending.contains(k.scan))
                return true;
            if (m_slowMs > 0 && !m_slowAccepted.contains(k.scan)) {
                // slow keys: only once it's been held long enough
                m_slowPending.insert(k.scan, k);
                auto *timer = new QTimer(this);
                timer->setSingleShot(true);
                m_slowTimers.insert(k.scan, timer);
                const quint32 scan = k.scan;
                connect(timer, &QTimer::timeout, this, [this, scan, timer] {
                    timer->deleteLater();
                    m_slowTimers.remove(scan);
                    const Key held = m_slowPending.take(scan);
                    m_slowAccepted.insert(scan);
                    playTone(QStringLiteral("click"));
                    if (!mouseKey(QEvent::KeyPress, held, false) && !(m_sticky && sticky(QEvent::KeyPress, held, false)))
                        deliver(QEvent::KeyPress, held);
                });
                timer->start(m_slowMs);
                return true;
            }
        } else {
            if (m_bounced.remove(k.scan))
                return true;
            if (m_slowPending.remove(k.scan)) {
                if (QTimer *t = m_slowTimers.take(k.scan))
                    t->deleteLater();
                return true;
            }
            if (!rep) {
                m_slowAccepted.remove(k.scan);
                m_lastReleasedScan = int(k.scan);
                m_sinceRelease.restart();
            }
        }
    }

    if (mouseKey(type, k, rep))
        return true;

    // ToggleKeys: the lock state before this press is in the native modifiers
    if (m_toggle && type == QEvent::KeyPress && !rep) {
        if (k.key == Qt::Key_CapsLock)
            emit lockToggled(!(k.nmods & 0x02));
        else if (k.key == Qt::Key_NumLock)
            emit lockToggled(!(k.nmods & 0x10));
        else if (k.key == Qt::Key_ScrollLock)
            emit lockToggled(m_scrollLock = !m_scrollLock);
    }

    return m_sticky && sticky(type, k, rep);
}

void AccessibilityController::playTone(const QString &tone)
{
    // tiny WAV files, made once
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wayland95/tones");
    const QString path = dir + QLatin1Char('/') + tone + QStringLiteral(".wav");
    if (!QFile::exists(path)) {
        QList<std::pair<double, int>> notes; // frequency, milliseconds
        if (tone == QLatin1String("on")) notes = { { 600, 70 }, { 900, 90 } };
        else if (tone == QLatin1String("off")) notes = { { 900, 70 }, { 600, 90 } };
        else if (tone == QLatin1String("high")) notes = { { 880, 110 } };
        else if (tone == QLatin1String("low")) notes = { { 440, 110 } };
        else notes = { { 1400, 12 } };
        const int rate = 22050;
        QByteArray pcm;
        for (const auto &[freq, ms] : notes) {
            const int n = rate * ms / 1000;
            for (int i = 0; i < n; ++i) {
                // a short fade in and out, so it doesn't click
                const double env = qMin(1.0, qMin(i, n - i) / (rate * 0.004));
                const qint16 v = qint16(9000 * env * std::sin(2 * M_PI * freq * i / rate));
                pcm.append(char(v & 0xff)).append(char((v >> 8) & 0xff));
            }
        }
        QByteArray wav;
        auto u32 = [&wav](quint32 v) { for (int i = 0; i < 4; ++i) wav.append(char((v >> (8 * i)) & 0xff)); };
        auto u16 = [&wav](quint16 v) { wav.append(char(v & 0xff)).append(char(v >> 8)); };
        wav += "RIFF"; u32(36 + pcm.size()); wav += "WAVEfmt ";
        u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
        wav += "data"; u32(pcm.size()); wav += pcm;
        QDir().mkpath(dir);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write(wav);
    }
    emit toneReady(path);
}
