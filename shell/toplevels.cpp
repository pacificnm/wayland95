#include "toplevels.h"

#include <QGuiApplication>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandinputdevice_p.h>
#include <QtWaylandClient/private/qwaylandintegration_p.h>

using namespace QtWaylandClient;

static ::wl_seat *seat()
{
    auto *integration = QWaylandIntegration::instance();
    if (!integration || !integration->display())
        return nullptr;
    QWaylandInputDevice *device = integration->display()->defaultInputDevice();
    return device ? device->wl_seat() : nullptr;
}

// ---------------------------------------------------------------- Toplevel

Toplevel::Toplevel(::zwlr_foreign_toplevel_handle_v1 *handle)
    : QtWayland::zwlr_foreign_toplevel_handle_v1(handle)
{
}

Toplevel::~Toplevel()
{
    destroy();
}

void Toplevel::zwlr_foreign_toplevel_handle_v1_state(wl_array *state)
{
    m_pending.minimized = m_pending.maximized = m_pending.fullscreen = m_pending.active = false;
    const auto *s = static_cast<const uint32_t *>(state->data);
    for (size_t i = 0; i < state->size / sizeof(uint32_t); ++i) {
        switch (s[i]) {
        case state_minimized: m_pending.minimized = true; break;
        case state_maximized: m_pending.maximized = true; break;
        case state_fullscreen: m_pending.fullscreen = true; break;
        case state_activated: m_pending.active = true; break;
        }
    }
}

void Toplevel::zwlr_foreign_toplevel_handle_v1_done()
{
    // (title and app id arrive only when they change; state with every done)
    if (!m_pending.title.isNull())
        m_title = m_pending.title;
    if (!m_pending.appId.isNull())
        m_appId = m_pending.appId;
    m_pending.title = m_pending.appId = QString();
    m_minimized = m_pending.minimized;
    m_maximized = m_pending.maximized;
    m_fullscreen = m_pending.fullscreen;
    m_active = m_pending.active;
    Q_EMIT changed();
}

void Toplevel::requestActivate()
{
    if (auto *s = seat())
        activate(s);
}

void Toplevel::toggleMaximize()
{
    if (m_maximized)
        unset_maximized();
    else
        set_maximized();
}

void Toplevel::setMinimizedState(bool minimized)
{
    if (minimized)
        set_minimized();
    else
        unset_minimized();
}

void Toplevel::closeWindow()
{
    close();
}

void Toplevel::forceClose()
{
    close();
}

// ----------------------------------------------------------- WindowManager

WindowManager::WindowManager(QObject *parent)
    : QAbstractListModel(parent)
    , m_manager(new ToplevelManager)
{
    m_manager->setParent(this);
    connect(m_manager, &ToplevelManager::toplevelAdded, this, &WindowManager::add);
    if (!m_manager->isActive())
        qWarning("WindowManager: the compositor has no zwlr_foreign_toplevel_manager_v1; the taskbar stays empty");
}

void WindowManager::add(Toplevel *toplevel)
{
    toplevel->setParent(this);
    // it shows on the taskbar once its first state is in (title and all)
    connect(toplevel, &Toplevel::changed, this, [this, toplevel] { changed(toplevel); });
    connect(toplevel, &Toplevel::gone, this, [this, toplevel] { remove(toplevel); });
}

void WindowManager::changed(Toplevel *toplevel)
{
    int row = int(m_toplevels.indexOf(toplevel));
    if (row < 0) {
        row = int(m_toplevels.size());
        beginInsertRows(QModelIndex(), row, row);
        m_toplevels.append(toplevel);
        endInsertRows();
        Q_EMIT countChanged();
    } else {
        Q_EMIT dataChanged(index(row), index(row));
    }
    if (toplevel->active()) {
        m_recent.removeAll(toplevel);
        m_recent.prepend(toplevel);
    } else if (!m_recent.contains(toplevel)) {
        m_recent.append(toplevel);
    }
}

void WindowManager::remove(Toplevel *toplevel)
{
    const int row = int(m_toplevels.indexOf(toplevel));
    if (row >= 0) {
        beginRemoveRows(QModelIndex(), row, row);
        m_toplevels.removeAt(row);
        endRemoveRows();
        Q_EMIT countChanged();
    }
    m_recent.removeAll(toplevel);
    toplevel->deleteLater();
}

int WindowManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_toplevels.size());
}

QVariant WindowManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_toplevels.size())
        return {};
    Toplevel *t = m_toplevels.at(index.row());
    switch (role) {
    case TitleRole: return t->title();
    case MinimizedRole: return t->minimized();
    case ActiveRole: return t->active();
    case FrameRole: return QVariant::fromValue(static_cast<QObject *>(t));
    case AppIdRole: return t->appId();
    case MaximizedRole: return t->maximized();
    }
    return {};
}

QHash<int, QByteArray> WindowManager::roleNames() const
{
    return {
        {TitleRole, "title"},
        {MinimizedRole, "minimized"},
        {ActiveRole, "active"},
        {FrameRole, "frame"},
        {AppIdRole, "appId"},
        {MaximizedRole, "maximized"},
    };
}

void WindowManager::setMinimized(QObject *frame, bool minimized)
{
    if (auto *t = qobject_cast<Toplevel *>(frame)) {
        t->setMinimizedState(minimized);
        if (!minimized)
            t->requestActivate();
    }
}

void WindowManager::toggleFromTaskbar(int index)
{
    if (index < 0 || index >= m_toplevels.size())
        return;
    Toplevel *t = m_toplevels.at(index);
    // the active window's own button minimizes it; others come forward
    if (t->active() && !t->minimized()) {
        t->setMinimizedState(true);
        return;
    }
    if (t->minimized())
        t->setMinimizedState(false);
    t->requestActivate();
}

QObject *WindowManager::activeFrame() const
{
    for (Toplevel *t : m_toplevels)
        if (t->active())
            return t;
    return nullptr;
}

QVariantList WindowManager::switchOrder() const
{
    QVariantList list;
    for (const QPointer<Toplevel> &t : m_recent) {
        if (!t)
            continue;
        list.append(QVariantMap{
            {QStringLiteral("frame"), QVariant::fromValue(static_cast<QObject *>(t.data()))},
            {QStringLiteral("title"), t->title()},
            {QStringLiteral("minimized"), t->minimized()},
        });
    }
    return list;
}
