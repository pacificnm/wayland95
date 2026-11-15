#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QtQmlIntegration/qqmlintegration.h>

// Alt+Tab's keys, caught before any program sees them: Alt+Tab starts
// (Alt+Shift+Tab backwards), Tab steps while Alt is held, letting go of Alt
// switches, Esc cancels. TaskSwitcher.qml shows the box.
//
// Instantiated in Main.qml.
class TaskSwitchKeys : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickWindow *window MEMBER m_window NOTIFY windowChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit TaskSwitchKeys(QObject *parent = nullptr);
    ~TaskSwitchKeys() override;
    bool active() const { return m_active; }

signals:
    void windowChanged();
    void activeChanged();
    void started(bool backwards);
    void stepped(bool backwards);
    void finished();
    void cancelled();
    // Ctrl+Shift+5: the screen capture tool. Caught here by the physical
    // key, since with Shift held the 5 key says "%" (or something else on
    // other layouts), which a QML Shortcut "Ctrl+Shift+5" never matches.
    void captureRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setActive(bool on);

    QPointer<QQuickWindow> m_window;
    bool m_active = false;
    bool m_eatCaptureRelease = false;
};
