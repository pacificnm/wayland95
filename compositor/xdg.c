// xdg-shell: windows (toplevels) and popups (menus, drop-down lists,
// tooltips), for windows and for panels (layer-shell). Popups are placed by
// wlroots from the program's positioner and kept on the screen.
#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "server.h"

// ------------------------------------------------------------- toplevels

static void toplevel_map(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, map);
	view_mapped(view);
}

static void toplevel_unmap(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, unmap);
	view_unmapped(view);
}

static void toplevel_commit(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, commit);
	struct wlr_xdg_toplevel *toplevel = view->xdg_toplevel;
	if (toplevel->base->initial_commit) {
		// Win95 frames (server side) come in phase 3; until then programs
		// draw their own
		if (view->decoration)
			wlr_xdg_toplevel_decoration_v1_set_mode(view->decoration,
					WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
		// the program chooses its size, unless it asked to start
		// maximized or fullscreen
		if (toplevel->requested.fullscreen)
			view_set_fullscreen(view, true);
		else if (toplevel->requested.maximized)
			view_set_maximized(view, true);
		else
			wlr_xdg_toplevel_set_size(toplevel, 0, 0);
		return;
	}
	if (!view->mapped)
		return;
	// (the scene helper keeps the window geometry's corner at the tree's
	// origin when the shadow changes)
	struct wlr_box geo;
	view_geometry(view, &geo);
	// resizing from the left or top edge: the right/bottom edge stays put
	struct server *server = view->server;
	if (server->grabbed_view == view && server->cursor_mode == CURSOR_RESIZE) {
		int nx = view->x, ny = view->y;
		if (server->resize_edges & WLR_EDGE_LEFT)
			nx = server->grab_geobox.x + server->grab_geobox.width - geo.width;
		if (server->resize_edges & WLR_EDGE_TOP)
			ny = server->grab_geobox.y + server->grab_geobox.height - geo.height;
		if (nx != view->x || ny != view->y)
			view_move(view, nx, ny);
	}
}

static void toplevel_destroy(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, destroy);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_minimize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->set_app_id.link);
	if (view->decoration) {
		wl_list_remove(&view->decoration_mode.link);
		wl_list_remove(&view->decoration_destroy.link);
	}
	free(view);
}

// A move or resize the program asks for (dragging its own title bar or
// edge) only starts if that button is still down: one that arrives after the
// release would otherwise hijack the next drag.
static bool button_still_down(struct view *view, uint32_t serial)
{
	return wlr_seat_validate_pointer_grab_serial(view->server->seat, view_surface(view), serial);
}

static void toplevel_request_move(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_move);
	struct wlr_xdg_toplevel_move_event *event = data;
	if (button_still_down(view, event->serial))
		view_begin_interactive(view, CURSOR_MOVE, 0);
}

static void toplevel_request_resize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xdg_toplevel_resize_event *event = data;
	if (button_still_down(view, event->serial))
		view_begin_interactive(view, CURSOR_RESIZE, event->edges);
}

static void toplevel_request_maximize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_maximize);
	if (!view->xdg_toplevel->base->initialized)
		return; // handled at the initial commit
	view_set_maximized(view, view->xdg_toplevel->requested.maximized);
	// xdg-shell needs a configure for every request
	wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void toplevel_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_fullscreen);
	if (!view->xdg_toplevel->base->initialized)
		return;
	view_set_fullscreen(view, view->xdg_toplevel->requested.fullscreen);
	wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void toplevel_request_minimize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_minimize);
	if (view->mapped)
		view_set_minimized(view, true);
}

static void toplevel_set_title(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_title);
	view_update_title(view);
}

static void toplevel_set_app_id(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_app_id);
	view_update_title(view);
}

static void new_xdg_toplevel(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct view *view = calloc(1, sizeof(*view));
	view_init_common(view, server, VIEW_XDG);
	view->xdg_toplevel = xdg_toplevel;
	view->scene_tree = wlr_scene_xdg_surface_create(server->layer_windows, xdg_toplevel->base);
	view->scene_tree->node.data = &view->tag;
	xdg_toplevel->base->data = view->scene_tree; // popups attach under it

	view->map.notify = toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &view->map);
	view->unmap.notify = toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &view->unmap);
	view->commit.notify = toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &view->commit);
	view->destroy.notify = toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &view->destroy);
	view->request_move.notify = toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &view->request_move);
	view->request_resize.notify = toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &view->request_resize);
	view->request_maximize.notify = toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &view->request_maximize);
	view->request_minimize.notify = toplevel_request_minimize;
	wl_signal_add(&xdg_toplevel->events.request_minimize, &view->request_minimize);
	view->request_fullscreen.notify = toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &view->request_fullscreen);
	view->set_title.notify = toplevel_set_title;
	wl_signal_add(&xdg_toplevel->events.set_title, &view->set_title);
	view->set_app_id.notify = toplevel_set_app_id;
	wl_signal_add(&xdg_toplevel->events.set_app_id, &view->set_app_id);
}

// ------------------------------------------------------------ decorations

static void decoration_request_mode(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, decoration_mode);
	if (view->xdg_toplevel->base->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(view->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
}

static void decoration_destroy(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, decoration_destroy);
	wl_list_remove(&view->decoration_mode.link);
	wl_list_remove(&view->decoration_destroy.link);
	view->decoration = NULL;
}

static void new_xdg_decoration(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
	struct wlr_scene_tree *tree = decoration->toplevel->base->data;
	if (!tree || !tree->node.data)
		return;
	struct view *view = (struct view *)tree->node.data;
	view->decoration = decoration;
	view->decoration_mode.notify = decoration_request_mode;
	wl_signal_add(&decoration->events.request_mode, &view->decoration_mode);
	view->decoration_destroy.notify = decoration_destroy;
	wl_signal_add(&decoration->events.destroy, &view->decoration_destroy);
	decoration_request_mode(&view->decoration_mode, NULL);
}

// ---------------------------------------------------------------- popups

// Keep a popup on screen: the box is the output's area, in the coordinates
// of the popup's root (a window's geometry, or a panel).
static void popup_unconstrain(struct server *server, struct wlr_xdg_popup *popup)
{
	struct wlr_surface *root = popup->parent;
	struct wlr_xdg_surface *xdg;
	while ((xdg = wlr_xdg_surface_try_from_wlr_surface(root)) && xdg->role == WLR_XDG_SURFACE_ROLE_POPUP)
		root = xdg->popup->parent;
	struct wlr_scene_tree *tree = NULL;
	if (xdg && xdg->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		tree = xdg->data; // its origin is the window geometry's corner
	} else {
		struct wlr_layer_surface_v1 *ls = wlr_layer_surface_v1_try_from_wlr_surface(root);
		if (ls && ls->data)
			tree = ((struct layer_surface *)ls->data)->scene->tree;
	}
	if (!tree)
		return;
	int lx, ly;
	wlr_scene_node_coords(&tree->node, &lx, &ly);
	int ox = lx, oy = ly; // the root's corner in the layout
	struct wlr_output *output = wlr_output_layout_output_at(server->output_layout,
			ox + popup->scheduled.geometry.x, oy + popup->scheduled.geometry.y);
	if (!output)
		output = wlr_output_layout_output_at(server->output_layout, ox, oy);
	struct wlr_box area;
	wlr_output_layout_get_box(server->output_layout, output, &area);
	area.x -= ox;
	area.y -= oy;
	wlr_xdg_popup_unconstrain_from_box(popup, &area);
}

static void popup_commit(struct wl_listener *listener, void *data)
{
	struct popup *popup = wl_container_of(listener, popup, commit);
	if (!popup->xdg_popup->base->initial_commit)
		return;
	popup_unconstrain(popup->server, popup->xdg_popup);
	// the popup's first configure (it doesn't appear without one)
	wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
}

// A menu that grabs (keyboard navigation) gets the keyboard while it's
// open: xdg-shell says the topmost grabbing popup always has keyboard focus,
// as GNOME's and KDE's compositors do. wlroots routes keys during the grab
// but leaves moving the focus to us; its grab ignores focus changes, so the
// focus is set directly. (GTK 3, Chromium and Firefox menus also work
// without it, as tested; it's here for the protocol's sake and for programs
// that rely on it.)
static void enter_keyboard(struct wlr_seat *seat, struct wlr_surface *surface)
{
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard)
		wlr_seat_keyboard_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
				&keyboard->modifiers);
	else
		wlr_seat_keyboard_enter(seat, surface, NULL, 0, NULL);
}

static void popup_map(struct wl_listener *listener, void *data)
{
	struct popup *popup = wl_container_of(listener, popup, map);
	if (popup->xdg_popup->seat)
		enter_keyboard(popup->xdg_popup->seat, popup->xdg_popup->base->surface);
	input_refresh_pointer(popup->server);
}

static void popup_unmap(struct wl_listener *listener, void *data)
{
	struct popup *popup = wl_container_of(listener, popup, unmap);
	struct wlr_seat *seat = popup->server->seat;
	// the keyboard back to what opened it (a parent menu, or the window)
	if (seat->keyboard_state.focused_surface == popup->xdg_popup->base->surface && popup->xdg_popup->parent)
		enter_keyboard(seat, popup->xdg_popup->parent);
	input_refresh_pointer(popup->server);
}

static void popup_destroy(struct wl_listener *listener, void *data)
{
	struct popup *popup = wl_container_of(listener, popup, destroy);
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->map.link);
	wl_list_remove(&popup->unmap.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static void new_xdg_popup(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_xdg_popup);
	struct wlr_xdg_popup *xdg_popup = data;

	// under its parent in the scene, so it moves and stacks with it
	struct wlr_scene_tree *parent_tree = NULL;
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	if (parent) {
		parent_tree = parent->data;
	} else {
		struct wlr_layer_surface_v1 *ls = wlr_layer_surface_v1_try_from_wlr_surface(xdg_popup->parent);
		if (ls && ls->data)
			parent_tree = ((struct layer_surface *)ls->data)->scene->tree;
	}
	if (!parent_tree) {
		wlr_log(WLR_ERROR, "popup without a known parent");
		return;
	}
	struct popup *popup = calloc(1, sizeof(*popup));
	popup->server = server;
	popup->xdg_popup = xdg_popup;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
	popup->map.notify = popup_map;
	wl_signal_add(&xdg_popup->base->surface->events.map, &popup->map);
	popup->unmap.notify = popup_unmap;
	wl_signal_add(&xdg_popup->base->surface->events.unmap, &popup->unmap);
	popup->destroy.notify = popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void xdg_init(struct server *server)
{
	wl_list_init(&server->views);
	server->xdg_shell = wlr_xdg_shell_create(server->display, 6);
	server->new_xdg_toplevel.notify = new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
	server->new_xdg_popup.notify = new_xdg_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);

	struct wlr_xdg_decoration_manager_v1 *decorations = wlr_xdg_decoration_manager_v1_create(server->display);
	server->new_xdg_decoration.notify = new_xdg_decoration;
	wl_signal_add(&decorations->events.new_toplevel_decoration, &server->new_xdg_decoration);
}
