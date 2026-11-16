#include "pointerframes.h"

// Pointer frames for other programs (wl_seat version 5).
//
// Qt's compositor offers wl_seat at version 4, which has no wl_pointer.frame
// event. Chromium (Chrome, and Electron programs such as VS Code) adds up
// wheel movement and only scrolls when a frame event arrives, so with
// version 4 the mouse wheel did nothing in them.
//
//  - The version: Qt's is compiled into its library, so, like touchpad.cpp,
//    the compositor provides wl_global_create itself (its symbols are
//    exported and found first) and asks for version 5 for wl_seat. Version 5
//    only adds events (frame, axis_source, axis_stop, axis_discrete), so
//    Qt's handling of the requests is unchanged.
//  - The frames: after each mouse or wheel event has been handed to the
//    programs (InputFixes), sendPointerFrames() ends the group with a frame
//    on every version 5 wl_pointer of the programs involved.

#include <QtGlobal>
#include <QWaylandClient>

#include <cstring>
#include <dlfcn.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace {
constexpr int SeatVersion = 5;
using GlobalCreate = wl_global *(*)(wl_display *, const wl_interface *, int, void *, wl_global_bind_func_t);

wl_iterator_result frameIfPointer(wl_resource *resource, void *)
{
    // by name: Qt's library has its own copy of the interface tables, so
    // their addresses don't match libwayland's
    const char *cls = wl_resource_get_class(resource);
    if (cls && std::strcmp(cls, "wl_pointer") == 0
        && wl_resource_get_version(resource) >= WL_POINTER_FRAME_SINCE_VERSION)
        wl_pointer_send_frame(resource);
    return WL_ITERATOR_CONTINUE;
}
} // namespace

extern "C" Q_DECL_EXPORT wl_global *wl_global_create(wl_display *display, const wl_interface *interface,
                                                      int version, void *data, wl_global_bind_func_t bind)
{
    static const auto real = reinterpret_cast<GlobalCreate>(dlsym(RTLD_NEXT, "wl_global_create"));
    if (interface && interface->name && std::strcmp(interface->name, "wl_seat") == 0 && version < SeatVersion)
        version = SeatVersion;
    return real(display, interface, version, data, bind);
}

void sendPointerFrames(QWaylandClient *client)
{
    if (client && client->client())
        wl_client_for_each_resource(client->client(), frameIfPointer, nullptr);
}
