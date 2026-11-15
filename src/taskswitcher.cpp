#include "taskswitcher.h"

#include <QCoreApplication>
#include <QKeyEvent>

TaskSwitchKeys::TaskSwitchKeys(QObject *parent)
    : QObject(parent)
{
    qApp->installEventFilter(this);
}

TaskSwitchKeys::~TaskSwitchKeys()
{
    qApp->removeEventFilter(this);
}

void TaskSwitchKeys::setActive(bool on)
{
    if (on == m_active)
        return;
    m_active = on;
    emit activeChanged();
}

bool TaskSwitchKeys::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window)
        return false;
    const QEvent::Type type = event->type();

    if (m_active && (type == QEvent::MouseButtonPress || type == QEvent::FocusOut)) {
        setActive(false);
        emit cancelled();
        return false;
    }
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease)
        return false;

    const auto *ke = static_cast<QKeyEvent *>(event);
    const int key = ke->key();
    const bool tab = key == Qt::Key_Tab || key == Qt::Key_Backtab;
    const bool alt = ke->modifiers() & Qt::AltModifier;

    // Ctrl+Shift+5 (the key, whatever it prints; evdev KEY_5 is 6, and xkb
    // keycodes are evdev + 8)
    const quint32 scan = ke->nativeScanCode();
    const bool fiveKey = key == Qt::Key_5 || key == Qt::Key_Percent || scan == 14;
    const Qt::KeyboardModifiers mods = ke->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    if (fiveKey && type == QEvent::KeyPress && mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
        if (!ke->isAutoRepeat())
            emit captureRequested();
        m_eatCaptureRelease = true;
        return true;
    }
    if (fiveKey && type == QEvent::KeyRelease && m_eatCaptureRelease) {
        m_eatCaptureRelease = false;
        return true;
    }

    if (type == QEvent::KeyPress) {
        if (tab && alt) {
            const bool backwards = key == Qt::Key_Backtab || (ke->modifiers() & Qt::ShiftModifier);
            if (!m_active) {
                setActive(true);
                emit started(backwards);
            } else {
                emit stepped(backwards);
            }
            return true; // Tab is ours while switching
        }
        if (m_active) {
            if (key == Qt::Key_Escape) {
                setActive(false);
                emit cancelled();
                return true;
            }
            // Alt was let go without us seeing it (another key came)
            if (!alt && key != Qt::Key_Alt) {
                setActive(false);
                emit finished();
                return false;
            }
            if (key != Qt::Key_Alt && key != Qt::Key_Shift)
                return true; // other keys do nothing while switching
        }
        return false;
    }

    // release
    if (m_active && tab)
        return true;
    if (m_active && key == Qt::Key_Alt) {
        setActive(false);
        emit finished();
        // the program saw Alt go down; let it see it come up
        return false;
    }
    return false;
}
