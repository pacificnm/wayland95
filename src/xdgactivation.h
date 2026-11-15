#pragma once

#include <QQmlParserStatus>
#include <QSet>
#include <QWaylandCompositorExtensionTemplate>
#include <QWaylandSurface>
#include <QtQmlIntegration/qqmlintegration.h>

#include "qwayland-server-xdg-activation-v1.h"

// xdg-activation-v1: how a program asks for one of its windows to be
// activated (given the keyboard and raised). Firefox and Electron apps (VS
// Code) need it even to focus their own window: without it, Firefox never
// takes typing. A program asks for a token, then activates a surface with
// it; tokens are only good once, and only ones handed out here count.
//
// Declared in Main.qml's WaylandCompositor, which raises the window on
// activateRequested.
class XdgActivation : public QWaylandCompositorExtensionTemplate<XdgActivation>,
                      public QtWaylandServer::xdg_activation_v1,
                      public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

public:
    XdgActivation();
    void initialize() override;
    // declared in QML: start once the compositor has taken us in, like
    // Qt's own extensions do
    void classBegin() override {}
    void componentComplete() override;

    void issue(const QString &token) { m_tokens.insert(token); }

signals:
    void activateRequested(QWaylandSurface *surface);

protected:
    void xdg_activation_v1_destroy(Resource *resource) override;
    void xdg_activation_v1_get_activation_token(Resource *resource, uint32_t id) override;
    void xdg_activation_v1_activate(Resource *resource, const QString &token, struct ::wl_resource *surface) override;

private:
    QSet<QString> m_tokens;
};
