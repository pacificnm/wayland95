// xdg-shell: windows (toplevels) and their popups (menus, drop-down lists,
// tooltips). Popups are placed by wlroots from the program's positioner and
// kept on the screen (wlr_xdg_popup_unconstrain_from_box).
#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "server.h"

// ---------------------------------------------------------------- focus

void view_focus(struct view *view, struct wlr_surface *surface)
{
	if (!view)
		return;
	struct server *server = view->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev = seat->keyboard_state.focused_surface;
	struct wlr_surface *target = view->xdg_toplevel->base->surface;
	if (prev != target) {
		// the previous window is told it isn't active any more
		struct wlr_xdg_toplevel *prev_toplevel = prev ? wlr_xdg_toplevel_try_from_wlr_surface(prev) : NULL;
		if (prev_toplevel)
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
	}
	// to the top, front of the list
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);
	wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);

	// keyboard focus even before a keyboard is plugged in (the tests' virtual
	// one, a USB keyboard later): the program is ready when keys arrive
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard)
		wlr_seat_keyboard_notify_enter(seat, target, keyboard->keycodes, keyboard->num_keycodes,
				&keyboard->modifiers);
	else
		wlr_seat_keyboard_notify_enter(seat, target, NULL, 0, NULL);
	(void)surface;
}

// the window (and the exact surface: sub-surface or popup) at a layout point
struct view *view_at(struct server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy)
{
	struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER)
		return NULL;
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface)
		return NULL;
	*surface = scene_surface->surface;
	// up the tree to the window's own node, which carries the view
	struct wlr_scene_tree *tree = node->parent;
	while (tree && !tree->node.data)
		tree = tree->node.parent;
	return tree ? tree->node.data : NULL;
}

// ----------------------------------------------------- moving, resizing

void view_begin_interactive(struct view *view, enum cursor_mode mode, uint32_t edges)
{
	struct server *server = view->server;
	server->grabbed_view = view;
	server->cursor_mode = mode;
	if (mode == CURSOR_MOVE) {
		server->grab_x = server->cursor->x - view->scene_tree->node.x;
		server->grab_y = server->cursor->y - view->scene_tree->node.y;
	} else {
		struct wlr_box geo_box;
		wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo_box);
		struct wlr_box *geo = &geo_box;
		double border_x = (view->scene_tree->node.x + geo->x) + ((edges & WLR_EDGE_RIGHT) ? geo->width : 0);
		double border_y = (view->scene_tree->node.y + geo->y) + ((edges & WLR_EDGE_BOTTOM) ? geo->height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;
		server->grab_geobox = *geo;
		server->grab_geobox.x += view->scene_tree->node.x;
		server->grab_geobox.y += view->scene_tree->node.y;
		server->resize_edges = edges;
	}
}

// ------------------------------------------------------------- toplevels

// the usable area of the output the window is on (whole layout for now;
// the shell's taskbar reserves its edge in phase 4)
static void usable_area(struct server *server, struct wlr_box *box)
{
	wlr_output_layout_get_box(server->output_layout, NULL, box);
}

static void toplevel_map(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, map);
	if (!view->placed) {
		// cascade new windows from the top left of the screen
		struct wlr_box area;
		usable_area(view->server, &area);
		int n = wl_list_length(&view->server->views) % 8;
		struct wlr_box geo_box;
		wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo_box);
		struct wlr_box *geo = &geo_box;
		wlr_scene_node_set_position(&view->scene_tree->node,
				area.x + 40 + n * 30 - geo->x, area.y + 30 + n * 30 - geo->y);
		view->placed = true;
	}
	wl_list_insert(&view->server->views, &view->link);
	view_focus(view, view->xdg_toplevel->base->surface);
}

static void toplevel_unmap(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, unmap);
	if (view == view->server->grabbed_view)
		input_reset_cursor_mode(view->server);
	wl_list_remove(&view->link);
	// the keyboard goes to the window that's now on top
	if (!wl_list_empty(&view->server->views)) {
		struct view *next = wl_container_of(view->server->views.next, next, link);
		view_focus(next, next->xdg_toplevel->base->surface);
	}
}

static void toplevel_commit(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, commit);
	if (view->xdg_toplevel->base->initial_commit) {
		// let the program pick its size; Win95 frames come in phase 3
		wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
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
	wl_list_remove(&view->request_fullscreen.link);
	free(view);
}

// A move or resize the program asks for (dragging its own title bar or
// edge) only starts if that button is still down: one that arrives after the
// release would otherwise hijack the next drag.
static bool button_still_down(struct view *view, uint32_t serial)
{
	return wlr_seat_validate_pointer_grab_serial(view->server->seat, view->xdg_toplevel->base->surface, serial);
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
	// phase 3; xdg-shell needs an answer to every request
	struct view *view = wl_container_of(listener, view, request_maximize);
	if (view->xdg_toplevel->base->initialized)
		wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void toplevel_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_fullscreen);
	if (view->xdg_toplevel->base->initialized)
		wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void new_xdg_toplevel(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct view *view = calloc(1, sizeof(*view));
	view->server = server;
	view->xdg_toplevel = xdg_toplevel;
	view->scene_tree = wlr_scene_xdg_surface_create(server->layer_windows, xdg_toplevel->base);
	view->scene_tree->node.data = view;
	xdg_toplevel->base->data = view->scene_tree; // popups attach under it
	wl_list_init(&view->link);

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
	view->request_fullscreen.notify = toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &view->request_fullscreen);
}

// ---------------------------------------------------------------- popups

static void popup_commit(struct wl_listener *listener, void *data)
{
	struct popup *popup = wl_container_of(listener, popup, commit);
	if (!popup->xdg_popup->base->initial_commit)
		return;
	// keep it on the screen: the box is the output area in the coordinates
	// of the popup's toplevel
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->xdg_popup->parent);
	struct wlr_xdg_surface *top = parent;
	while (top && top->role == WLR_XDG_SURFACE_ROLE_POPUP)
		top = wlr_xdg_surface_try_from_wlr_surface(top->popup->parent);
	if (top && top->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL && top->data) {
		struct wlr_scene_tree *tree = top->data;
		int lx, ly;
		wlr_scene_node_coords(&tree->node, &lx, &ly);
		struct wlr_box area;
		wlr_output_layout_get_box(((struct view *)tree->node.data)->server->output_layout, NULL, &area);
		area.x -= lx;
		area.y -= ly;
		wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &area);
	}
	wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
}

static void popup_destroy(struct wl_listener *listener, void *data)
{
	struct popup *popup = wl_container_of(listener, popup, destroy);
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static void new_xdg_popup(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_popup *xdg_popup = data;
	struct popup *popup = calloc(1, sizeof(*popup));
	popup->xdg_popup = xdg_popup;

	// in the scene under its parent, so it moves and stacks with it
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	if (!parent || !parent->data) {
		wlr_log(WLR_ERROR, "popup without a known parent");
		free(popup);
		return;
	}
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
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
}
