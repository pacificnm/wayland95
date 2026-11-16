// Input: keyboards, pointers (mice, touchpads, and the virtual ones the
// tests drive), the cursor, and the seat that hands events to programs.
#include <stdlib.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/util/log.h>

#include "server.h"

// ------------------------------------------------------------ keyboards

static void keyboard_modifiers(struct wl_listener *listener, void *data)
{
	struct keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void keyboard_key(struct wl_listener *listener, void *data)
{
	struct keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct wlr_keyboard_key_event *event = data;
	// desktop shortcuts (Alt+Tab...) come in phase 3; everything goes to
	// the focused program
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_key(keyboard->server->seat, event->time_msec, event->keycode, event->state);
}

static void keyboard_destroy(struct wl_listener *listener, void *data)
{
	struct keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
	struct server *server = keyboard->server;
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	// a virtual keyboard went away: the physical ones are the seat's again
	if (wlr_seat_get_keyboard(server->seat) == keyboard->wlr_keyboard)
		wlr_seat_set_keyboard(server->seat, &server->kbd_group->keyboard);
	free(keyboard);
}

static void update_capabilities(struct server *server)
{
	// a desktop always has a keyboard (the group, with its keymap)
#ifdef WAYLAND95_TEST_BREAK_FOCUS // tests/selftest.sh: the bug before the group, keys lost
	wlr_seat_set_capabilities(server->seat, WL_SEAT_CAPABILITY_POINTER
			| (wl_list_empty(&server->keyboards) ? 0 : WL_SEAT_CAPABILITY_KEYBOARD));
	return;
#endif
	wlr_seat_set_capabilities(server->seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
}

static struct keyboard *listen_keyboard(struct server *server, struct wlr_keyboard *wlr_keyboard, bool can_go)
{
	struct keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;
	keyboard->modifiers.notify = keyboard_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	wl_list_init(&keyboard->destroy.link);
	wl_list_init(&keyboard->link);
	if (can_go) {
		keyboard->destroy.notify = keyboard_destroy;
		wl_list_remove(&keyboard->destroy.link);
		wl_signal_add(&wlr_keyboard->base.events.destroy, &keyboard->destroy);
		wl_list_insert(&server->keyboards, &keyboard->link);
	}
	return keyboard;
}

// the default keymap (Control Panel > Keyboard's layout comes in phase 3)
static void set_default_keymap(struct wlr_keyboard *wlr_keyboard)
{
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
}

static void add_physical_keyboard(struct server *server, struct wlr_keyboard *wlr_keyboard)
{
	// the group needs its members to share its keymap
	wlr_keyboard_set_keymap(wlr_keyboard, server->kbd_group->keyboard.keymap);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
	if (!wlr_keyboard_group_add_keyboard(server->kbd_group, wlr_keyboard))
		wlr_log(WLR_ERROR, "keyboard %s couldn't join the keyboard group", wlr_keyboard->base.name);
}

static void new_input(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		add_physical_keyboard(server, wlr_keyboard_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_POINTER:
		wlr_cursor_attach_input_device(server->cursor, device);
		break;
	default:
		break;
	}
	update_capabilities(server);
}

static void new_virtual_keyboard(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *vk = data;
	// its keymap comes from the client; it's the seat's keyboard when it types
	listen_keyboard(server, &vk->keyboard, true);
	update_capabilities(server);
}

static void new_virtual_pointer(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_virtual_pointer);
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	wlr_cursor_attach_input_device(server->cursor, &event->new_pointer->pointer.base);
}

// ------------------------------------------------------- cursor, pointer

void input_reset_cursor_mode(struct server *server)
{
	server->cursor_mode = CURSOR_PASSTHROUGH;
	server->grabbed_view = NULL;
}

static void process_move(struct server *server)
{
	struct view *view = server->grabbed_view;
	wlr_scene_node_set_position(&view->scene_tree->node,
			server->cursor->x - server->grab_x, server->cursor->y - server->grab_y);
}

static void process_resize(struct server *server)
{
	struct view *view = server->grabbed_view;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int left = server->grab_geobox.x, right = server->grab_geobox.x + server->grab_geobox.width;
	int top = server->grab_geobox.y, bottom = server->grab_geobox.y + server->grab_geobox.height;
	if (server->resize_edges & WLR_EDGE_TOP) {
		top = border_y;
		if (top >= bottom)
			top = bottom - 1;
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		bottom = border_y;
		if (bottom <= top)
			bottom = top + 1;
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		left = border_x;
		if (left >= right)
			left = right - 1;
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		right = border_x;
		if (right <= left)
			right = left + 1;
	}
	struct wlr_box geo_box;
		wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo_box);
		struct wlr_box *geo = &geo_box;
	wlr_scene_node_set_position(&view->scene_tree->node, left - geo->x, top - geo->y);
	wlr_xdg_toplevel_set_size(view->xdg_toplevel, right - left, bottom - top);
}

static void process_motion(struct server *server, uint32_t time)
{
	if (server->cursor_mode == CURSOR_MOVE) {
		process_move(server);
		return;
	} else if (server->cursor_mode == CURSOR_RESIZE) {
		process_resize(server);
		return;
	}
	// a button is held: everything goes to the surface it was pressed on,
	// even outside it (dragging a scroll bar, selecting text)
	if (server->press_surface && server->seat->pointer_state.button_count > 0
			&& server->seat->pointer_state.focused_surface == server->press_surface) {
		wlr_seat_pointer_notify_motion(server->seat, time,
				server->cursor->x - server->press_ox, server->cursor->y - server->press_oy);
		return;
	}
	server->press_surface = NULL;

	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct view *view = view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!view)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(server->seat);
	}
}

static void cursor_motion(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_motion(server, event->time_msec);
}

static void cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_motion(server, event->time_msec);
}

static void cursor_button(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		input_reset_cursor_mode(server);
		if (server->seat->pointer_state.button_count == 0) {
			// the grab ends: the pointer goes to whatever is under it now
			server->press_surface = NULL;
			process_motion(server, event->time_msec);
		}
		return;
	}
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct view *view = view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (surface && server->seat->pointer_state.button_count == 1) {
		// the first button down starts the implicit grab
		server->press_surface = surface;
		server->press_ox = server->cursor->x - sx;
		server->press_oy = server->cursor->y - sy;
	}
	// a press raises and focuses the window under it
	if (view)
		view_focus(view, surface);
}

static void cursor_axis(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

static void cursor_frame(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, cursor_frame);
#ifndef WAYLAND95_TEST_BREAK_FRAMES // tests/selftest.sh: proves the wheel test catches this
	wlr_seat_pointer_notify_frame(server->seat);
#endif
}

// ------------------------------------------------------------- the seat

static void request_set_cursor(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, request_set_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	// only the program the pointer is over may set it
	if (server->seat->pointer_state.focused_client == event->seat_client)
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

static void request_set_selection(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void request_set_primary_selection(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

void input_init(struct server *server)
{
	wl_list_init(&server->keyboards);

	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
	server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	server->cursor_mode = CURSOR_PASSTHROUGH;

	server->cursor_motion.notify = cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
	server->cursor_button.notify = cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	server->new_input.notify = new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);

	struct wlr_virtual_pointer_manager_v1 *vp = wlr_virtual_pointer_manager_v1_create(server->display);
	server->new_virtual_pointer.notify = new_virtual_pointer;
	wl_signal_add(&vp->events.new_virtual_pointer, &server->new_virtual_pointer);
	struct wlr_virtual_keyboard_manager_v1 *vk = wlr_virtual_keyboard_manager_v1_create(server->display);
	server->new_virtual_keyboard.notify = new_virtual_keyboard;
	wl_signal_add(&vk->events.new_virtual_keyboard, &server->new_virtual_keyboard);

	server->seat = wlr_seat_create(server->display, "seat0");
	server->kbd_group = wlr_keyboard_group_create();
	set_default_keymap(&server->kbd_group->keyboard);
	server->group_kbd = listen_keyboard(server, &server->kbd_group->keyboard, false);
#ifndef WAYLAND95_TEST_BREAK_FOCUS
	wlr_seat_set_keyboard(server->seat, &server->kbd_group->keyboard);
#endif
	server->request_set_cursor.notify = request_set_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->request_set_cursor);
	server->request_set_selection.notify = request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);
	server->request_set_primary_selection.notify = request_set_primary_selection;
	wl_signal_add(&server->seat->events.request_set_primary_selection, &server->request_set_primary_selection);
	update_capabilities(server);
}
