#include "windowmanager.h"

#include <QMetaObject>

WindowManager::WindowManager(QObject *parent)
    : QAbstractListModel(parent)
{
}

int WindowManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant WindowManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case TitleRole:
        return entry.title.isEmpty() ? QStringLiteral("Untitled") : entry.title;
    case MinimizedRole:
        return entry.minimized;
    case ActiveRole:
        return entry.active;
    case FrameRole:
        return QVariant::fromValue(entry.frame);
    default:
        return {};
    }
}

QHash<int, QByteArray> WindowManager::roleNames() const
{
    return {
        { TitleRole, "title" },
        { MinimizedRole, "minimized" },
        { ActiveRole, "active" },
        { FrameRole, "frame" },
    };
}

int WindowManager::indexOfFrame(QObject *frame) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).frame == frame)
            return i;
    }
    return -1;
}

void WindowManager::addWindow(QObject *frame, const QString &title)
{
    if (!frame || indexOfFrame(frame) != -1)
        return;

    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size());
    Entry entry;
    entry.frame = frame;
    entry.title = title;
    m_entries.append(entry);
    endInsertRows();
}

void WindowManager::removeWindow(QObject *frame)
{
    m_recent.removeAll(frame);
    const int row = indexOfFrame(frame);
    if (row == -1)
        return;

    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();
}

void WindowManager::setTitle(QObject *frame, const QString &title)
{
    const int row = indexOfFrame(frame);
    if (row == -1)
        return;

    m_entries[row].title = title;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { TitleRole });
}

void WindowManager::setMinimized(QObject *frame, bool minimized)
{
    const int row = indexOfFrame(frame);
    if (row == -1)
        return;

    m_entries[row].minimized = minimized;
    frame->setProperty("minimized", minimized);

    if (minimized && m_entries[row].active) {
        m_entries[row].active = false;
        frame->setProperty("isActive", false);
    }

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { MinimizedRole, ActiveRole });
}

void WindowManager::setActive(QObject *frame, bool active)
{
    const int row = indexOfFrame(frame);
    if (row == -1)
        return;

    if (active) {
        m_recent.removeAll(frame);
        m_recent.prepend(frame);
    }
    for (int i = 0; i < m_entries.size(); ++i) {
        const bool shouldBeActive = active && (m_entries[i].frame == frame);
        if (m_entries[i].active != shouldBeActive) {
            m_entries[i].active = shouldBeActive;
            m_entries[i].frame->setProperty("isActive", shouldBeActive);
        }
    }

    emit dataChanged(index(0), index(m_entries.size() - 1), { ActiveRole });
}

void WindowManager::toggleFromTaskbar(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;

    Entry &entry = m_entries[index];
    QObject *frame = entry.frame;

    if (entry.active && !entry.minimized) {
        // Clicking the active window's own taskbar button minimizes it.
        setMinimized(frame, true);
        return;
    }

    if (entry.minimized)
        setMinimized(frame, false);

    setActive(frame, true);
    QMetaObject::invokeMethod(frame, "requestActivate");
}

int WindowManager::nextStackingZ()
{
    return ++m_zCounter;
}

QObject *WindowManager::activeFrame() const
{
    for (const Entry &e : m_entries)
        if (e.active && !e.minimized)
            return e.frame;
    return nullptr;
}

QVariantList WindowManager::switchOrder() const
{
    QList<QObject *> order;
    for (QObject *f : m_recent)
        if (indexOfFrame(f) != -1 && !order.contains(f))
            order << f;
    for (const Entry &e : m_entries)
        if (!order.contains(e.frame))
            order << e.frame;
    QVariantList out;
    for (QObject *f : std::as_const(order)) {
        const Entry &e = m_entries.at(indexOfFrame(f));
        out << QVariantMap{
            { QStringLiteral("frame"), QVariant::fromValue(f) },
            { QStringLiteral("title"), e.title.isEmpty() ? QStringLiteral("Untitled") : e.title },
            { QStringLiteral("minimized"), e.minimized },
        };
    }
    return out;
}
