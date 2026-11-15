#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

// WindowManager is the C++ backbone of the shell: it is the single source of
// truth for "what windows currently exist", in the order the taskbar should
// show them. The compositor's QML side (WindowFrame.qml) registers itself
// here when a client surface is mapped, and keeps this model's title /
// minimized / active flags in sync. The taskbar (Taskbar.qml) is just a
// Repeater bound to this model - it never talks to the compositor directly.
//
// Exposed to QML as the singleton `WindowManager`.
class WindowManager : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        MinimizedRole,
        ActiveRole,
        FrameRole,
    };

    explicit WindowManager(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Called by WindowFrame.qml when a new client window is mapped.
    Q_INVOKABLE void addWindow(QObject *frame, const QString &title);
    // Called by WindowFrame.qml's Component.onDestruction.
    Q_INVOKABLE void removeWindow(QObject *frame);

    // Called whenever the underlying toplevel's title changes.
    Q_INVOKABLE void setTitle(QObject *frame, const QString &title);
    // Minimizing/restoring from either the taskbar or the window's own
    // minimize button both go through here, so the model and the actual
    // QML item's `minimized` property never disagree.
    Q_INVOKABLE void setMinimized(QObject *frame, bool minimized);
    // Marks `frame` as the active window and clears the flag on every other
    // entry - there is only ever one active window.
    Q_INVOKABLE void setActive(QObject *frame, bool active);

    // Convenience for taskbar clicks: restore+activate (or minimize, if it
    // was already the active window) the entry at `index`.
    Q_INVOKABLE void toggleFromTaskbar(int index);

    // Monotonically increasing stacking counter so "activate" can always
    // raise a window above every other one.
    Q_INVOKABLE int nextStackingZ();
    // the active window's frame, or null
    Q_INVOKABLE QObject *activeFrame() const;
    // Alt+Tab's order: [{ frame, title, minimized }], the most recently
    // active first
    Q_INVOKABLE QVariantList switchOrder() const;

private:
    struct Entry {
        QObject *frame = nullptr;
        QString title;
        bool minimized = false;
        bool active = false;
    };

    int indexOfFrame(QObject *frame) const;

    QList<Entry> m_entries;
    QList<QObject *> m_recent; // frames, most recently active first
    int m_zCounter = 10;
};
