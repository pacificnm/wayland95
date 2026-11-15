#include "xdgactivation.h"

#include <QUuid>
#include <QWaylandCompositor>
#include <QWaylandSeat>

namespace {

// one token being made: the program may describe it (serial, app ID,
// surface), then commits it and gets the token string back
class ActivationToken : public QtWaylandServer::xdg_activation_token_v1
{
public:
    ActivationToken(XdgActivation *activation, wl_client *client, uint32_t id, int version)
        : QtWaylandServer::xdg_activation_token_v1(client, id, version)
        , m_activation(activation)
    {
    }

protected:
    void xdg_activation_token_v1_commit(Resource *resource) override
    {
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (m_activation)
            m_activation->issue(token);
        send_done(resource->handle, token);
    }
    void xdg_activation_token_v1_destroy(Resource *resource) override { wl_resource_destroy(resource->handle); }
    void xdg_activation_token_v1_destroy_resource(Resource *) override { delete this; }

private:
    QPointer<XdgActivation> m_activation;
};

} // namespace

XdgActivation::XdgActivation()
    : QWaylandCompositorExtensionTemplate<XdgActivation>()
{
}

void XdgActivation::initialize()
{
    QWaylandCompositorExtensionTemplate::initialize();
    auto *compositor = static_cast<QWaylandCompositor *>(extensionContainer());
    if (compositor)
        init(compositor->display(), 1);
}

void XdgActivation::componentComplete()
{
    if (!extensionContainer())
        setExtensionContainer(qobject_cast<QWaylandObject *>(parent()));
    if (!isInitialized())
        initialize();
}

void XdgActivation::xdg_activation_v1_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void XdgActivation::xdg_activation_v1_get_activation_token(Resource *resource, uint32_t id)
{
    new ActivationToken(this, resource->client(), id, resource->version());
}

void XdgActivation::xdg_activation_v1_activate(Resource *, const QString &token, struct ::wl_resource *surface)
{
    // only tokens we gave out, each once
    if (!m_tokens.remove(token))
        return;
    QWaylandSurface *s = QWaylandSurface::fromResource(surface);
    if (!s)
        return;
    // Programs activate themselves and then wait for the keyboard to arrive
    // (Firefox sets up its own focus only then). If it's already there,
    // take it away and give it back, so they see it come.
    auto *compositor = static_cast<QWaylandCompositor *>(extensionContainer());
    if (QWaylandSeat *seat = compositor ? compositor->defaultSeat() : nullptr) {
        if (seat->keyboardFocus() == s) {
            seat->setKeyboardFocus(nullptr);
            seat->setKeyboardFocus(s);
        }
    }
    emit activateRequested(s);
}
