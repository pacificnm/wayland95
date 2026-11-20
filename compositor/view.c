// Windows of both kinds (xdg toplevels and X11 windows): focus, stacking,
// moving, resizing, maximizing, minimizing, fullscreen, and the window list
// other programs see (foreign-toplevel: the taskbar).
#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include <xcb/xproto.h>

#include "server.h"

// ------------------------------------------------------------ the basics

struct wlr_surface *view_surface(struct view *view)
{
	if (view->type == VIEW_XDG)
		return view->xdg_toplevel->base->surface;
	return view->xsurface->surface;
}

void view_geometry(struct view *view, struct wlr_box *box)
{
	if (view->type == VIEW_XDG) {
		wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, box);
	} else {
		// (X11's size is the desktop's times xscale)
		*box = (struct wlr_box){0, 0, xscale_from_x(view->server, view->xsurface->width),
				xscale_from_x(view->server, view->xsurface->height)};
	}
}

// the scene node follows view->x/y. For xdg windows the scene helper
// already puts the window geometry's corner at the tree's origin (the
// shadow hangs outside it); X11 windows have no offset.
static void place_node(struct view *view)
{
	wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
}

void view_move(struct view *view, int x, int y)
{
	view->x = x;
	view->y = y;
	place_node(view);
	if (view->type == VIEW_XWAYLAND) {
		// X11 programs place their menus from where X thinks they are
		wlr_xwayland_surface_configure(view->xsurface, xscale_to_x(view->server, x), xscale_to_x(view->server, y),
				view->xsurface->width, view->xsurface->height);
	}
	places_note(view);
}

void view_resize(struct view *view, int width, int height)
{
	if (width < 1 || height < 1)
		return;
	if (view->type == VIEW_XDG)
		wlr_xdg_toplevel_set_size(view->xdg_toplevel, width, height);
	else // (sizes X11 can show pixel for pixel: xscale.c)
		wlr_xwayland_surface_configure(view->xsurface, xscale_to_x(view->server, view->x),
				xscale_to_x(view->server, view->y), xscale_to_x(view->server, xscale_snap(view->server, width)),
				xscale_to_x(view->server, xscale_snap(view->server, height)));
}

static bool resize_outstanding(struct view *view)
{
	// (serials only grow; the difference survives their wrapping round)
	return view->resize_serial
			&& (int32_t)(view->resize_serial - view->xdg_toplevel->base->current.configure_serial) > 0;
}

void view_resize_paced(struct view *view, int width, int height)
{
	if (width < 1 || height < 1)
		return;
	if (view->type != VIEW_XDG) {
		view_resize(view, width, height);
		return;
	}
	if (resize_outstanding(view)) {
		view->resize_waiting = true;
		view->resize_w = width;
		view->resize_h = height;
		return;
	}
	view->resize_waiting = false;
	view->resize_serial = wlr_xdg_toplevel_set_size(view->xdg_toplevel, width, height);
}

void view_resize_drawn(struct view *view)
{
	if (view->type == VIEW_XDG && view->resize_waiting && !resize_outstanding(view)) {
		view->resize_waiting = false;
		view->resize_serial = wlr_xdg_toplevel_set_size(view->xdg_toplevel, view->resize_w, view->resize_h);
	}
}

void view_resize_flush(struct view *view)
{
	if (view->type == VIEW_XDG && view->resize_waiting)
		wlr_xdg_toplevel_set_size(view->xdg_toplevel, view->resize_w, view->resize_h);
	view->resize_waiting = false;
	view->resize_serial = 0;
}

void view_close(struct view *view)
{
	if (view->type == VIEW_XDG)
		wlr_xdg_toplevel_send_close(view->xdg_toplevel);
	else
		wlr_xwayland_surface_close(view->xsurface);
}

static void set_activated(struct view *view, bool active)
{
	view->frame_active = active;
	frame_update(view);
	if (view->type == VIEW_XDG)
		wlr_xdg_toplevel_set_activated(view->xdg_toplevel, active);
	else
		wlr_xwayland_surface_activate(view->xsurface, active);
	if (view->ftl)
		wlr_foreign_toplevel_handle_v1_set_activated(view->ftl, active);
}

struct view *view_top(struct server *server)
{
	struct view *view;
	wl_list_for_each(view, &server->views, link)
		if (view->mapped && !view->minimized)
			return view;
	return NULL;
}

struct view *view_active(struct server *server)
{
	struct view *view;
	wl_list_for_each(view, &server->views, link)
		if (view->mapped && !view->minimized && view->frame_active)
			return view;
	return NULL;
}

// A full screen window covers the panels too (the taskbar) while it's the
// one in front: it moves to a layer above them, and back among the windows
// when another comes forward.
static void fullscreen_restack(struct server *server)
{
	struct view *top = view_top(server), *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->scene_tree)
			continue;
		struct wlr_scene_tree *want = view->fullscreen && view == top
				? server->layer_fullscreen : server->layer_windows;
		if (view->scene_tree->node.parent != want)
			wlr_scene_node_reparent(&view->scene_tree->node, want);
	}
}

// ----------------------------------------------------------------- focus

// a window's dialogs: the windows it's the parent of (or theirs)
static bool view_owns(struct view *owner, struct view *view)
{
	if (owner->type != VIEW_XDG || view->type != VIEW_XDG)
		return false;
	for (struct wlr_xdg_toplevel *p = view->xdg_toplevel->parent; p; p = p->parent)
		if (p == owner->xdg_toplevel)
			return true;
	return false;
}

// A window's dialogs stay in front of it, as in Windows 95 (in the order
// they were in): bringing it forward brings them too.
static void raise_dialogs(struct view *owner)
{
	struct server *server = owner->server;
	struct view *found[32], *view;
	int n = 0;
	wl_list_for_each_reverse(view, &server->views, link) // back to front
		if (n < 32 && view != owner && view->mapped && !view->minimized && view_owns(owner, view))
			found[n++] = view;
	for (int i = 0; i < n; ++i) {
		wl_list_remove(&found[i]->link);
		wl_list_insert(&server->views, &found[i]->link);
		wlr_scene_node_raise_to_top(&found[i]->scene_tree->node);
	}
}

// the front one of a window's modal dialogs, if it's waiting on one
static struct view *modal_dialog(struct view *owner)
{
	struct view *view;
	wl_list_for_each(view, &owner->server->views, link)
		if (view->modal && view->mapped && !view->minimized && view_owns(owner, view))
			return view;
	return NULL;
}

void view_focus(struct view *view)
{
	if (!view || !view->mapped)
		return;
	struct server *server = view->server;
	if (server->locked || server->exclusive_layer)
		return; // the lock screen or an exclusive panel keeps the keyboard
	if (view->minimized)
		view_set_minimized(view, false);

	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);
	fullscreen_restack(server); // (before raising: one that leaves its layer lands on top)
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	if (view->type == VIEW_XWAYLAND)
		wlr_xwayland_surface_restack(view->xsurface, NULL, XCB_STACK_MODE_ABOVE);
	raise_dialogs(view);
	// a window waiting on a modal dialog: the dialog takes the keyboard
	struct view *modal = modal_dialog(view);
	if (modal)
		view = modal;

	struct view *other;
	wl_list_for_each(other, &server->views, link)
		if (other != view && other->mapped)
			set_activated(other, false);
	set_activated(view, true);
	input_focus_surface(server, view_surface(view));
}

// the keyboard to whatever should have it now that something went away
static void focus_next(struct server *server)
{
	struct view *top = view_top(server);
	if (top)
		view_focus(top);
	else
		input_focus_surface(server, NULL);
}

// --------------------------------------------- minimize, maximize, fullscreen

void view_set_minimized(struct view *view, bool minimized)
{
	if (view->minimized == minimized)
		return;
	view->minimized = minimized;
	wlr_scene_node_set_enabled(&view->scene_tree->node, !minimized);
	if (view->type == VIEW_XWAYLAND)
		wlr_xwayland_surface_set_minimized(view->xsurface, minimized);
	if (view->ftl)
		wlr_foreign_toplevel_handle_v1_set_minimized(view->ftl, minimized);
	if (minimized) {
		set_activated(view, false);
		if (wlr_seat_get_keyboard(view->server->seat) &&
				view->server->seat->keyboard_state.focused_surface == view_surface(view))
			focus_next(view->server);
	}
	fullscreen_restack(view->server);
	input_refresh_pointer(view->server);
}

static void output_box_for(struct view *view, struct wlr_box *box)
{
	struct output *output = output_for_view(view);
	wlr_output_layout_get_box(view->server->output_layout, output ? output->wlr_output : NULL, box);
}

static void apply_state(struct view *view)
{
	if (view->type == VIEW_XDG) {
		wlr_xdg_toplevel_set_maximized(view->xdg_toplevel, view->maximized);
		wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel, view->fullscreen);
		// a snapped window touches the screen's edges on three sides (GTK
		// then draws no shadow or rounded corners there)
		if (wl_resource_get_version(view->xdg_toplevel->resource) >= XDG_TOPLEVEL_STATE_TILED_LEFT_SINCE_VERSION)
			wlr_xdg_toplevel_set_tiled(view->xdg_toplevel, view->snap_edge
					? view->snap_edge | WLR_EDGE_TOP | WLR_EDGE_BOTTOM : WLR_EDGE_NONE);
	} else {
		wlr_xwayland_surface_set_maximized(view->xsurface, view->maximized);
		wlr_xwayland_surface_set_fullscreen(view->xsurface, view->fullscreen);
	}
	if (view->ftl) {
		wlr_foreign_toplevel_handle_v1_set_maximized(view->ftl, view->maximized);
		wlr_foreign_toplevel_handle_v1_set_fullscreen(view->ftl, view->fullscreen);
	}
}

// where a maximized window's content goes: the usable area, less the title
// bar; the frame's borders hang just outside the area (Win95 does the same)
static void maximized_box(struct view *view, struct wlr_box *box)
{
	// (on the display it's on)
	struct output *output = output_for_view(view);
	*box = output ? output->usable : (struct wlr_box){0, 0, 640, 480};
	if (view->ssd) {
		box->y += FRAME_TOP - FRAME_BORDER;
		box->height -= FRAME_TOP - FRAME_BORDER;
	}
}

// a snapped window: its display's usable area, the left or right half,
// with the frame inside it (FRAME is the frame's outer box, CONTENT the
// window's own)
void view_snap_box(struct view *view, uint32_t edge, struct output *output, struct wlr_box *frame, struct wlr_box *content)
{
	if (!output)
		output = output_for_view(view);
	struct wlr_box area = output ? output->usable : (struct wlr_box){0, 0, 640, 480};
	int half = area.width / 2;
	if (edge == WLR_EDGE_RIGHT)
		*frame = (struct wlr_box){area.x + half, area.y, area.width - half, area.height};
	else
		*frame = (struct wlr_box){area.x, area.y, half, area.height};
	int left, top, right, bottom;
	view_frame_insets(view, &left, &top, &right, &bottom);
	*content = (struct wlr_box){frame->x + left, frame->y + top,
			frame->width - left - right, frame->height - top - bottom};
}

// whether the program lets its window be resized (a dialog of one fixed
// size doesn't, and doesn't snap)
bool view_resizable(struct view *view)
{
	if (view->type == VIEW_XDG) {
		const struct wlr_xdg_toplevel_state *s = &view->xdg_toplevel->current;
		return !(s->max_width > 0 && s->min_width == s->max_width
				&& s->max_height > 0 && s->min_height == s->max_height);
	}
	const xcb_size_hints_t *h = view->xsurface->size_hints;
	return !(h && (h->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) && (h->flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE)
			&& h->max_width > 0 && h->min_width == h->max_width
			&& h->max_height > 0 && h->min_height == h->max_height);
}

// where a window goes in its state, if that decides it (else false: where
// it was put)
static bool state_box(struct view *view, struct wlr_box *box)
{
	if (view->fullscreen)
		output_box_for(view, box);
	else if (view->maximized)
		maximized_box(view, box);
	else if (view->snap_edge) {
		struct wlr_box frame;
		view_snap_box(view, view->snap_edge, NULL, &frame, box);
	} else
		return false;
	return true;
}

static void remember_geometry(struct view *view)
{
	if (view->maximized || view->fullscreen || view->snap_edge)
		return; // already remembered
	struct wlr_box geo;
	view_geometry(view, &geo);
	view->saved = (struct wlr_box){view->x, view->y, geo.width, geo.height};
}

void view_set_maximized(struct view *view, bool maximized)
{
	if (view->maximized == maximized && view->mapped)
		return;
	if (maximized) {
		remember_geometry(view);
		view->maximized = true;
		struct wlr_box area;
		maximized_box(view, &area);
		view_move(view, area.x, area.y);
		view_resize(view, area.width, area.height);
	} else {
		// (a snapped window goes back to its half)
		view->maximized = false;
		struct wlr_box area;
		if (state_box(view, &area)) {
			view_move(view, area.x, area.y);
			view_resize(view, area.width, area.height);
		} else if (view->saved.width > 0) {
			view_move(view, view->saved.x, view->saved.y);
			view_resize(view, view->saved.width, view->saved.height);
		} else if (view->type == VIEW_XDG) {
			// maximized before it ever had a size of its own: it chooses
			wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
		}
	}
	apply_state(view);
	frame_update(view);
	places_note(view);
}

void view_set_snapped(struct view *view, uint32_t edge, struct output *output)
{
	if (view->fullscreen || view->snap_edge == edge)
		return;
	if (edge != WLR_EDGE_NONE) {
		remember_geometry(view);
		view->maximized = false;
		view->snap_edge = edge;
		struct wlr_box frame, box;
		view_snap_box(view, edge, output, &frame, &box);
		if (box.width <= 0 || box.height <= 0)
			return;
		view_move(view, box.x, box.y);
		view_resize(view, box.width, box.height);
	} else {
		view->snap_edge = WLR_EDGE_NONE;
		if (!view->maximized && view->saved.width > 0) {
			view_move(view, view->saved.x, view->saved.y);
			view_resize(view, view->saved.width, view->saved.height);
		}
	}
	apply_state(view);
	frame_update(view);
	places_note(view);
}

void view_set_fullscreen(struct view *view, bool fullscreen)
{
	if (view->fullscreen == fullscreen)
		return;
	if (fullscreen) {
		remember_geometry(view);
		view->fullscreen = true;
		struct wlr_box box;
		output_box_for(view, &box);
		view_move(view, box.x, box.y);
		view_resize(view, box.width, box.height);
		wlr_scene_node_raise_to_top(&view->scene_tree->node);
	} else {
		view->fullscreen = false;
		struct wlr_box area;
		if (state_box(view, &area)) { // maximized or snapped
			view_move(view, area.x, area.y);
			view_resize(view, area.width, area.height);
		} else if (view->saved.width > 0) {
			view_move(view, view->saved.x, view->saved.y);
			view_resize(view, view->saved.width, view->saved.height);
		}
	}
	apply_state(view);
	frame_update(view);
	fullscreen_restack(view->server);
	input_refresh_pointer(view->server);
	places_note(view);
}

// panels or displays came, went or changed: maximized, snapped and full
// screen windows follow their display
void view_arrange_all(struct server *server)
{
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->mapped)
			continue;
		struct wlr_box area;
		if (!state_box(view, &area))
			continue;
		if (area.width <= 0 || area.height <= 0)
			continue;
		view_move(view, area.x, area.y);
		view_resize(view, area.width, area.height);
	}
}

// ----------------------------------------------- the taskbar's window list

static void ftl_activate(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, ftl_activate);
	view_focus(view);
}

static void ftl_close(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, ftl_close);
	view_close(view);
}

static void ftl_minimize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, ftl_minimize);
	struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
	view_set_minimized(view, event->minimized);
	if (!event->minimized)
		view_focus(view);
}

static void ftl_maximize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, ftl_maximize);
	struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
	view_set_maximized(view, event->maximized);
}

static void ftl_fullscreen(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, ftl_fullscreen);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
	view_set_fullscreen(view, event->fullscreen);
}

const char *view_title(struct view *view)
{
	const char *t = view->type == VIEW_XDG ? view->xdg_toplevel->title : view->xsurface->title;
	return t ? t : "";
}

static const char *view_app_id(struct view *view)
{
	const char *a = view->type == VIEW_XDG ? view->xdg_toplevel->app_id : view->xsurface->class;
	return a ? a : "";
}

void view_update_title(struct view *view)
{
	frame_update(view);
	if (view->ftl) {
		wlr_foreign_toplevel_handle_v1_set_title(view->ftl, view_title(view));
		wlr_foreign_toplevel_handle_v1_set_app_id(view->ftl, view_app_id(view));
	}
	if (view->ext_ftl) {
		struct wlr_ext_foreign_toplevel_handle_v1_state state = {
			.title = view_title(view), .app_id = view_app_id(view),
		};
		wlr_ext_foreign_toplevel_handle_v1_update_state(view->ext_ftl, &state);
	}
}

// --------------------------------------------------------- map and unmap

struct view *view_by_id(struct server *server, uint32_t id)
{
	struct view *view;
	wl_list_for_each(view, &server->views, link)
		if (view->id == id)
			return view;
	return NULL;
}

void view_init_common(struct view *view, struct server *server, enum view_type type)
{
	static uint32_t next_id = 1;
	view->id = next_id++;
	view->tag.kind = NODE_VIEW;
	view->server = server;
	view->type = type;
	wl_list_init(&view->link);
}

void view_mapped(struct view *view)
{
	struct server *server = view->server;
	view->mapped = true;
	wlr_scene_node_set_enabled(&view->scene_tree->node, true);
	frame_update(view);
	int left, top, right, bottom;
	view_frame_insets(view, &left, &top, &right, &bottom);
	// new windows open on the display the pointer is on
	struct output *output = view->placed ? output_for_view(view)
			: output_at(server, server->cursor->x, server->cursor->y);
	struct wlr_box area = output ? output->usable : (struct wlr_box){0, 0, 640, 480};
	struct wlr_box state;
	if (!view->placed && (view->maximized || view->fullscreen || view->snap_edge) && state_box(view, &state)) {
		// it asked to be maximized (or fullscreen) before it showed (Firefox
		// does, between its first commit and its first drawing), or was
		// maximized or snapped when it was last open (places.c): there
		view_move(view, state.x, state.y);
		view_resize(view, state.width, state.height);
		view->placed = true;
	} else if (view->place_restored && !view->maximized && !view->fullscreen && !view->snap_edge) {
		// back where it was last time: all of it on a display, now that
		// its size is known
		places_clamp(view);
	} else if (!view->placed) {
		// cascade new windows (their frames) from the top left of the
		// usable area
		int n = wl_list_length(&server->views) % 8;
		view_move(view, area.x + 40 + n * 30 + left, area.y + 30 + n * 30 + top);
		view->placed = true;
	} else if (!view->maximized && !view->fullscreen && view->y - top < area.y) {
		// a window that placed itself keeps its place, but not with its
		// title bar off the top of the screen
		view_move(view, view->x, area.y + top);
	} else {
		place_node(view);
	}
	wl_list_insert(&server->views, &view->link);

	view->ftl = wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevels);
	view->ftl_activate.notify = ftl_activate;
	wl_signal_add(&view->ftl->events.request_activate, &view->ftl_activate);
	view->ftl_close.notify = ftl_close;
	wl_signal_add(&view->ftl->events.request_close, &view->ftl_close);
	view->ftl_minimize.notify = ftl_minimize;
	wl_signal_add(&view->ftl->events.request_minimize, &view->ftl_minimize);
	view->ftl_maximize.notify = ftl_maximize;
	wl_signal_add(&view->ftl->events.request_maximize, &view->ftl_maximize);
	view->ftl_fullscreen.notify = ftl_fullscreen;
	wl_signal_add(&view->ftl->events.request_fullscreen, &view->ftl_fullscreen);
	struct wlr_ext_foreign_toplevel_handle_v1_state ext_state = {
		.title = view_title(view), .app_id = view_app_id(view),
	};
	view->ext_ftl = wlr_ext_foreign_toplevel_handle_v1_create(server->ext_toplevels, &ext_state);
	view_update_title(view);
	if (view->maximized || view->fullscreen)
		apply_state(view); // (the taskbar's window list hears of it too)

	view_focus(view);
	input_refresh_pointer(server);
	places_note(view);
}

void view_unmapped(struct view *view)
{
	struct server *server = view->server;
	view->resize_waiting = false; // (nothing more to ask of a window that's going)
	view->resize_serial = 0;
	if (view == server->grabbed_view) {
		input_cancel_snap(server);
		input_reset_cursor_mode(server);
	}
	view->mapped = false;
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);
	if (server->frame_press_view == view)
		server->frame_press_view = NULL;
	if (server->last_title_click_view == view)
		server->last_title_click_view = NULL;
	if (server->switch_current == view)
		server->switch_current = NULL;
	view->frame_pressed = PART_NONE;
	wl_list_remove(&view->link);
	wl_list_init(&view->link);
	if (view->ftl) {
		wl_list_remove(&view->ftl_activate.link);
		wl_list_remove(&view->ftl_close.link);
		wl_list_remove(&view->ftl_minimize.link);
		wl_list_remove(&view->ftl_maximize.link);
		wl_list_remove(&view->ftl_fullscreen.link);
		wlr_foreign_toplevel_handle_v1_destroy(view->ftl);
		view->ftl = NULL;
	}
	if (view->ext_ftl) {
		wlr_ext_foreign_toplevel_handle_v1_destroy(view->ext_ftl);
		view->ext_ftl = NULL;
	}
	if (server->seat->keyboard_state.focused_surface == view_surface(view))
		focus_next(server);
	input_refresh_pointer(server);
}

// ------------------------------------------------ what's under the pointer

struct view *view_from_surface_at(struct server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy, struct node_tag **owner)
{
	*surface = NULL;
	if (owner)
		*owner = NULL;
	// while locked only the lock screen exists for input
	struct wlr_scene_node *root = server->locked ? &server->layer_lock->node : &server->scene->tree.node;
	struct wlr_scene_node *node = wlr_scene_node_at(root, lx, ly, sx, sy);
	if (!node)
		return NULL;
	// a program's surface, or something of ours: a window frame (its
	// picture or the resize zone around it), the lock screen's cover
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_surface *scene_surface =
				wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
		if (scene_surface)
			*surface = scene_surface->surface;
	}
	// up the tree to the node that says what it belongs to
	struct wlr_scene_tree *tree = node->parent;
	while (tree && !tree->node.data)
		tree = tree->node.parent;
	if (!tree)
		return NULL;
	struct node_tag *tag = tree->node.data;
	if (owner)
		*owner = tag;
	return tag->kind == NODE_VIEW ? (struct view *)tag : NULL;
}

// ----------------------------------------------------- moving, resizing

void view_begin_interactive(struct view *view, enum cursor_mode mode, uint32_t edges)
{
	struct server *server = view->server;
	if (view->fullscreen)
		return;
	if (view->maximized && mode == CURSOR_MOVE) {
		// dragging a maximized window (by its own title bar) restores it
		// under the pointer, at the same place across its width (to its
		// size from before it was snapped, if it was)
		struct wlr_box geo;
		view_geometry(view, &geo);
		double fraction = geo.width > 0 ? (server->cursor->x - view->x) / geo.width : 0.5;
		double dy = server->cursor->y - view->y;
		view->snap_edge = WLR_EDGE_NONE;
		view_set_maximized(view, false);
		if (view->saved.width > 0)
			view_move(view, server->cursor->x - fraction * view->saved.width, server->cursor->y - dy);
	} else if (view->maximized) {
		return; // a maximized window has no edges to resize
	} else if (view->snap_edge && mode == CURSOR_RESIZE) {
		// resizing a snapped window: it keeps the new size, as any other
		view->snap_edge = WLR_EDGE_NONE;
		apply_state(view);
	}
	// (a snapped window being moved is restored once the pointer has
	// moved a little: input.c)
	server->grabbed_view = view;
	server->cursor_mode = mode;
	server->grab_start_x = server->cursor->x;
	server->grab_start_y = server->cursor->y;
	server->snap_edge = WLR_EDGE_NONE;
	struct wlr_box geo;
	view_geometry(view, &geo);
	if (mode == CURSOR_MOVE) {
		server->grab_x = server->cursor->x - view->x;
		server->grab_y = server->cursor->y - view->y;
	} else {
		double border_x = view->x + ((edges & WLR_EDGE_RIGHT) ? geo.width : 0);
		double border_y = view->y + ((edges & WLR_EDGE_BOTTOM) ? geo.height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;
		server->grab_geobox = (struct wlr_box){view->x, view->y, geo.width, geo.height};
		server->resize_edges = edges;
	}
}
