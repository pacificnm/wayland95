// X11 programs, through Xwayland (started when the first X11 program
// connects; wlroots is its window manager). Ordinary windows are views like
// any other; menus, tooltips and other override-redirect windows are placed
// where the program puts them, above the windows. On scaled displays X11's
// coordinates are the desktop's times server->xscale (xscale.c): every
// number to or from X11 goes through xscale_to_x / xscale_from_x.
#include <stdlib.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include <xcb/xproto.h>

#include "server.h"

// --------------------------------------------------------------- windows

// (after the scene helper's own: it sets the buffer's size, this the
// size it's shown at)
static void xview_scale_commit(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, scale_commit);
	xscale_apply(view->server, view->content);
}

static void xview_map(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, map);
	struct wlr_xwayland_surface *xs = view->xsurface;
	if (!view->scene_tree) {
		view->scene_tree = wlr_scene_tree_create(view->server->layer_windows);
		view->scene_tree->node.data = &view->tag;
		view->content = wlr_scene_subsurface_tree_create(view->scene_tree, xs->surface);
		view->scale_commit.notify = xview_scale_commit;
		wl_signal_add(&xs->surface->events.commit, &view->scale_commit);
	}
	xscale_apply(view->server, view->content);
	// the Win95 frame, unless the program asked for none (_MOTIF_WM_HINTS:
	// programs that draw their own title bar, splash screens)
	view->ssd = xs->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
	// a window that asked for a position keeps it (X11 programs remember
	// theirs); others are cascaded like Wayland windows
	if (!view->placed && (xs->x != 0 || xs->y != 0)) {
		view->x = xscale_from_x(view->server, xs->x);
		view->y = xscale_from_x(view->server, xs->y);
		view->placed = true;
	}
	view_mapped(view);
	// one that asked to start maximized did so before it was known to get a
	// frame: make room for the title bar now
	if (view->maximized && view->ssd)
		view_arrange_all(view->server);
}

static void xview_unmap(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, unmap);
	view_unmapped(view);
}

static void xview_commit(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, commit);
	if (view->mapped)
		frame_update(view); // a new size, if it changed
}

static void xview_set_decorations(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, decoration_mode);
	bool ssd = view->xsurface->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
	if (view->mapped && ssd != view->ssd) {
		view->ssd = ssd;
		frame_update(view);
	}
}

static void xview_associate(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, associate);
	view->map.notify = xview_map;
	wl_signal_add(&view->xsurface->surface->events.map, &view->map);
	view->unmap.notify = xview_unmap;
	wl_signal_add(&view->xsurface->surface->events.unmap, &view->unmap);
	view->commit.notify = xview_commit;
	wl_signal_add(&view->xsurface->surface->events.commit, &view->commit);
}

static void xview_dissociate(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, dissociate);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->commit.link);
	if (view->scene_tree) {
		wl_list_remove(&view->scale_commit.link);
		frame_destroy(view);
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
		view->content = NULL;
	}
}

static void xview_destroy(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, destroy);
	wl_list_remove(&view->associate.link);
	wl_list_remove(&view->dissociate.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_configure.link);
	wl_list_remove(&view->request_activate.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_minimize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->set_app_id.link);
	wl_list_remove(&view->decoration_mode.link);
	free(view->frame_title);
	free(view);
}

static void xview_request_configure(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	struct server *server = view->server;
	// (sizes X11 can show pixel for pixel: xscale.c)
	int width = xscale_to_x(server, xscale_snap(server, xscale_from_x(server, event->width)));
	int height = xscale_to_x(server, xscale_snap(server, xscale_from_x(server, event->height)));
	if (!view->mapped) {
		// before it's shown it may put itself where it likes
		wlr_xwayland_surface_configure(view->xsurface, event->x, event->y, width, height);
		return;
	}
	if (view->maximized || view->fullscreen || view->snap_edge) {
		// keep it where it is: tell it its real geometry back
		wlr_xwayland_surface_configure(view->xsurface, xscale_to_x(view->server, view->x),
				xscale_to_x(view->server, view->y), view->xsurface->width, view->xsurface->height);
		return;
	}
	// move (which re-sends the current size), then the new size
	view_move(view, xscale_from_x(server, event->x), xscale_from_x(server, event->y));
	wlr_xwayland_surface_configure(view->xsurface, xscale_to_x(server, view->x), xscale_to_x(server, view->y),
			width, height);
}

static void xview_request_activate(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_activate);
	view_focus(view);
}

static void xview_request_move(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_move);
	// X11 has no serial: only while a button is down
	if (view->server->seat->pointer_state.button_count > 0)
		view_begin_interactive(view, CURSOR_MOVE, 0);
}

static void xview_request_resize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xwayland_resize_event *event = data;
	if (view->server->seat->pointer_state.button_count > 0)
		view_begin_interactive(view, CURSOR_RESIZE, event->edges);
}

static void xview_request_maximize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_maximize);
	struct wlr_xwayland_surface *xs = view->xsurface;
	view_set_maximized(view, xs->maximized_horz || xs->maximized_vert);
}

static void xview_request_minimize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_minimize);
	struct wlr_xwayland_minimize_event *event = data;
	view_set_minimized(view, event->minimize);
}

static void xview_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_fullscreen);
	view_set_fullscreen(view, view->xsurface->fullscreen);
}

static void xview_set_title(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_title);
	view_update_title(view);
}

// (its own function: the view is found from the listener, and this one is
// set_app_id, not set_title - LibreOffice sets its class after its title)
static void xview_set_class(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_app_id);
	view_update_title(view);
}

static void new_managed(struct server *server, struct wlr_xwayland_surface *xs)
{
	struct view *view = calloc(1, sizeof(*view));
	view_init_common(view, server, VIEW_XWAYLAND);
	view->xsurface = xs;
	xs->data = view;

	view->associate.notify = xview_associate;
	wl_signal_add(&xs->events.associate, &view->associate);
	view->dissociate.notify = xview_dissociate;
	wl_signal_add(&xs->events.dissociate, &view->dissociate);
	view->destroy.notify = xview_destroy;
	wl_signal_add(&xs->events.destroy, &view->destroy);
	view->request_configure.notify = xview_request_configure;
	wl_signal_add(&xs->events.request_configure, &view->request_configure);
	view->request_activate.notify = xview_request_activate;
	wl_signal_add(&xs->events.request_activate, &view->request_activate);
	view->request_move.notify = xview_request_move;
	wl_signal_add(&xs->events.request_move, &view->request_move);
	view->request_resize.notify = xview_request_resize;
	wl_signal_add(&xs->events.request_resize, &view->request_resize);
	view->request_maximize.notify = xview_request_maximize;
	wl_signal_add(&xs->events.request_maximize, &view->request_maximize);
	view->request_minimize.notify = xview_request_minimize;
	wl_signal_add(&xs->events.request_minimize, &view->request_minimize);
	view->request_fullscreen.notify = xview_request_fullscreen;
	wl_signal_add(&xs->events.request_fullscreen, &view->request_fullscreen);
	view->set_title.notify = xview_set_title;
	wl_signal_add(&xs->events.set_title, &view->set_title);
	view->set_app_id.notify = xview_set_class; // the class is the app id
	wl_signal_add(&xs->events.set_class, &view->set_app_id);
	view->decoration_mode.notify = xview_set_decorations;
	wl_signal_add(&xs->events.set_decorations, &view->decoration_mode);
}

// -------------------------------------------- menus, tooltips (unmanaged)

static void unmanaged_scale_commit(struct wl_listener *listener, void *data)
{
	struct unmanaged *u = wl_container_of(listener, u, scale_commit);
	xscale_apply(u->server, u->scene_tree);
}

static void unmanaged_map(struct wl_listener *listener, void *data)
{
	struct unmanaged *u = wl_container_of(listener, u, map);
	struct server *server = u->server;
	u->scene_tree = wlr_scene_tree_create(server->layer_unmanaged);
	u->scene_tree->node.data = &u->tag;
	wlr_scene_subsurface_tree_create(u->scene_tree, u->xsurface->surface);
	u->scale_commit.notify = unmanaged_scale_commit;
	wl_signal_add(&u->xsurface->surface->events.commit, &u->scale_commit);
	xscale_apply(server, u->scene_tree);
	wlr_scene_node_set_position(&u->scene_tree->node, xscale_from_x(server, u->xsurface->x),
			xscale_from_x(server, u->xsurface->y));
	// a menu that wants the keyboard (keyboard navigation) gets it
	if (wlr_xwayland_or_surface_wants_focus(u->xsurface))
		input_focus_surface(u->server, u->xsurface->surface);
	input_refresh_pointer(u->server);
}

static void unmanaged_unmap(struct wl_listener *listener, void *data)
{
	struct unmanaged *u = wl_container_of(listener, u, unmap);
	struct server *server = u->server;
	if (u->scene_tree) {
		wl_list_remove(&u->scale_commit.link);
		wlr_scene_node_destroy(&u->scene_tree->node);
		u->scene_tree = NULL;
	}
	// the keyboard back to the window under it
	if (server->seat->keyboard_state.focused_surface == u->xsurface->surface) {
		struct view *top = view_top(server);
		if (top)
			view_focus(top);
	}
	input_refresh_pointer(server);
}

static void unmanaged_associate(struct wl_listener *listener, void *data)
{
	struct unmanaged *u = wl_container_of(listener, u, associate);
	u->map.notify = unmanaged_map;
	wl_signal_add(&u->xsurface->surface->events.map, &u->map);
	u->unmap.notify = unmanaged_unmap;
	wl_signal_add(&u->xsurface->surface->events.unmap, &u->unmap);
}

static void unmanaged_dissociate(struct wl_listener *listener, void *data)
{
	struct unmanaged *u = wl_container_of(listener, u, dissociate);
	wl_list_remove(&u->map.link);
	wl_list_remove(&u->unmap.link);
}

static void unmanaged_request_configure(struct wl_listener *listener, void *data)
{
	struct unmanaged *u = wl_container_of(listener, u, request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	wlr_xwayland_surface_configure(u->xsurface, event->x, event->y, event->width, event->height);
}

static void unmanaged_set_geometry(struct wl_listener *listener, void *data)
{
	struct unmanaged *u = wl_container_of(listener, u, set_geometry);
	if (u->scene_tree)
		wlr_scene_node_set_position(&u->scene_tree->node, xscale_from_x(u->server, u->xsurface->x),
				xscale_from_x(u->server, u->xsurface->y));
}

static void unmanaged_destroy(struct wl_listener *listener, void *data)
{
	struct unmanaged *u = wl_container_of(listener, u, destroy);
	wl_list_remove(&u->associate.link);
	wl_list_remove(&u->dissociate.link);
	wl_list_remove(&u->destroy.link);
	wl_list_remove(&u->request_configure.link);
	wl_list_remove(&u->set_geometry.link);
	free(u);
}

static void new_unmanaged(struct server *server, struct wlr_xwayland_surface *xs)
{
	struct unmanaged *u = calloc(1, sizeof(*u));
	u->tag.kind = NODE_UNMANAGED;
	u->server = server;
	u->xsurface = xs;
	u->associate.notify = unmanaged_associate;
	wl_signal_add(&xs->events.associate, &u->associate);
	u->dissociate.notify = unmanaged_dissociate;
	wl_signal_add(&xs->events.dissociate, &u->dissociate);
	u->destroy.notify = unmanaged_destroy;
	wl_signal_add(&xs->events.destroy, &u->destroy);
	u->request_configure.notify = unmanaged_request_configure;
	wl_signal_add(&xs->events.request_configure, &u->request_configure);
	u->set_geometry.notify = unmanaged_set_geometry;
	wl_signal_add(&xs->events.set_geometry, &u->set_geometry);
}

// ------------------------------------------------------------- the server

static void new_xwayland_surface(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *xs = data;
	if (xs->override_redirect)
		new_unmanaged(server, xs);
	else
		new_managed(server, xs);
}

static void xwayland_ready(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, xwayland_ready);
	wlr_xwayland_set_seat(server->xwayland, server->seat);
	// the default cursor, so X11 windows don't show an empty one
	struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", 1);
	if (xcursor) {
		struct wlr_xcursor_image *image = xcursor->images[0];
		wlr_xwayland_set_cursor(server->xwayland, image->buffer, image->width * 4,
				image->width, image->height, image->hotspot_x, image->hotspot_y);
	}
	wlr_log(WLR_INFO, "Xwayland ready on DISPLAY=%s", server->xwayland->display_name);
	xscale_xwayland_ready(server); // (its DPI)
}

void xwayland_init(struct server *server)
{
	// lazy: Xwayland starts when the first X11 program connects
	server->xwayland = wlr_xwayland_create(server->display, server->compositor, true);
	if (!server->xwayland) {
		wlr_log(WLR_ERROR, "no Xwayland: X11 programs won't run");
		return;
	}
	server->xwayland_ready.notify = xwayland_ready;
	wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);
	server->new_xwayland_surface.notify = new_xwayland_surface;
	wl_signal_add(&server->xwayland->events.new_surface, &server->new_xwayland_surface);
	setenv("DISPLAY", server->xwayland->display_name, true);
}
