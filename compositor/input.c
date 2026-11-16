// Input: keyboards, pointers (mice, touchpads, and the virtual ones the
// tests drive), the cursor, gestures, pointer constraints (games), drag and
// drop, and the seat that hands it all to programs.
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/util/log.h>
#include <wlr/util/edges.h>
#include <wlr/util/region.h>

#include "server.h"

static void activity(struct server *server)
{
	// input resets the idle timer (screen saver, display off)
	wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
}

// ------------------------------------------------------------ keyboards

// ---------------------------------------------------- desktop shortcuts
//
// Alt+Tab / Alt+Shift+Tab: step through the windows, most recently used
// first; each step brings the next one to the front, and letting go of Alt
// activates it (Escape goes back). Until the shell draws the Alt+Tab box
// (phase 4, wayland95-shell-v1), the windows themselves show the way.
// Alt+F4 closes the active window.

static void switch_step(struct server *server, int dir)
{
	int n = 0;
	struct view *view;
	wl_list_for_each(view, &server->views, link)
		if (view->mapped)
			n++;
	if (n == 0)
		return;
	struct view *list[n];
	int i = 0, current = 0;
	wl_list_for_each(view, &server->views, link) {
		if (!view->mapped)
			continue;
		if (view == server->switch_current)
			current = i;
		list[i++] = view;
	}
	if (!server->switching) {
		server->switching = true;
		current = 0; // from the active window
	}
	current = ((current + dir) % n + n) % n;
	server->switch_current = list[current];
	if (!list[current]->minimized)
		wlr_scene_node_raise_to_top(&list[current]->scene_tree->node);
}

// the stacking as it was (the list is in order, front first)
static void switch_restore_stacking(struct server *server)
{
	struct view *view;
	wl_list_for_each_reverse(view, &server->views, link)
		if (view->mapped)
			wlr_scene_node_raise_to_top(&view->scene_tree->node);
}

static void switch_end(struct server *server, bool accept)
{
	if (!server->switching)
		return;
	server->switching = false;
	struct view *chosen = server->switch_current;
	server->switch_current = NULL;
	if (accept && chosen) {
		view_focus(chosen);
	} else {
		switch_restore_stacking(server);
	}
	input_refresh_pointer(server);
}

// the program with the keyboard asked to get every key (a remote desktop
// or virtual machine window)
static bool shortcuts_inhibited(struct server *server)
{
	struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
	struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &server->shortcuts_inhibit->inhibitors, link)
		if (inhibitor->surface == focused && inhibitor->seat == server->seat && inhibitor->active)
			return true;
	return false;
}

static bool handle_shortcut(struct server *server, uint32_t modifiers, const xkb_keysym_t *syms, int nsyms)
{
	if (server->locked || server->exclusive_layer)
		return false; // the lock screen or a panel has the keyboard to itself
	bool alt = modifiers & WLR_MODIFIER_ALT;
	for (int i = 0; i < nsyms; ++i) {
		xkb_keysym_t sym = syms[i];
		if (server->switching && sym == XKB_KEY_Escape) {
			switch_end(server, false);
			return true;
		}
		if (!alt || shortcuts_inhibited(server))
			continue;
		if (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) {
			bool back = (modifiers & WLR_MODIFIER_SHIFT) || sym == XKB_KEY_ISO_Left_Tab;
			switch_step(server, back ? -1 : 1);
			return true;
		}
		if (sym == XKB_KEY_F4 && !server->switching) {
			struct view *top = view_top(server);
			if (top && top->frame_active)
				view_close(top);
			return true;
		}
	}
	return false;
}

static void keyboard_modifiers(struct wl_listener *listener, void *data)
{
	struct keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	struct server *server = keyboard->server;
	// Alt let go: that's the one. The focus moves first, so the window
	// that had it doesn't see a lone Alt press and release (Firefox would
	// show its menu bar)
	if (server->switching && !(wlr_keyboard_get_modifiers(keyboard->wlr_keyboard) & WLR_MODIFIER_ALT))
		switch_end(server, true);
	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void keyboard_key(struct wl_listener *listener, void *data)
{
	struct keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	activity(server);
	// a key the desktop used doesn't go to the program, nor does its release
	static bool swallowed[KEY_CNT];
	// the shell's keys (volume, brightness: shell.c): which one each held
	// key sent, so its release goes there too
	static uint32_t shell_held[KEY_CNT];
	bool handled = false;
	if (event->keycode < KEY_CNT && event->state != WL_KEYBOARD_KEY_STATE_PRESSED && shell_held[event->keycode]) {
		shell_send_key(server, shell_held[event->keycode], false);
		shell_held[event->keycode] = 0;
		return;
	}
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		const xkb_keysym_t *syms;
		int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, event->keycode + 8, &syms);
		// even over the lock screen or a dialog panel; not while a program
		// asked for every key (a virtual machine's window)
		uint32_t shell_key = 0;
		for (int i = 0; i < nsyms && !shell_key; ++i)
			shell_key = shell_key_for(syms[i], wlr_keyboard_get_modifiers(keyboard->wlr_keyboard));
		if (shell_key && event->keycode < KEY_CNT && !shortcuts_inhibited(server)
				&& shell_send_key(server, shell_key, true)) {
			shell_held[event->keycode] = shell_key;
			return;
		}
		handled = handle_shortcut(server, wlr_keyboard_get_modifiers(keyboard->wlr_keyboard), syms, nsyms);
		if (event->keycode < KEY_CNT)
			swallowed[event->keycode] = handled;
	} else if (event->keycode < KEY_CNT && swallowed[event->keycode]) {
		swallowed[event->keycode] = false;
		handled = true;
	}
	if (handled)
		return;
	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
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

static void physical_keyboard_destroy(struct wl_listener *listener, void *data)
{
	struct physical_keyboard *pk = wl_container_of(listener, pk, destroy);
	wl_list_remove(&pk->destroy.link);
	wl_list_remove(&pk->link);
	free(pk);
}

static void add_physical_keyboard(struct server *server, struct wlr_keyboard *wlr_keyboard)
{
	// the layout and repeat from Control Panel > Keyboard; the group needs
	// its members to share its keymap
	settings_apply_keyboard(server, wlr_keyboard);
	if (!wlr_keyboard_group_add_keyboard(server->kbd_group, wlr_keyboard))
		wlr_log(WLR_ERROR, "keyboard %s couldn't join the keyboard group", wlr_keyboard->base.name);
	struct physical_keyboard *pk = calloc(1, sizeof(*pk));
	pk->wlr_keyboard = wlr_keyboard;
	pk->destroy.notify = physical_keyboard_destroy;
	wl_signal_add(&wlr_keyboard->base.events.destroy, &pk->destroy);
	wl_list_insert(&server->physical_keyboards, &pk->link);
}

static void pointer_device_destroy(struct wl_listener *listener, void *data)
{
	struct pointer_device *pd = wl_container_of(listener, pd, destroy);
	wl_list_remove(&pd->destroy.link);
	wl_list_remove(&pd->link);
	free(pd);
}

static void add_pointer(struct server *server, struct wlr_input_device *device)
{
	// touchpad settings from Control Panel > Mouse
	settings_apply_pointer(server, device);
	wlr_cursor_attach_input_device(server->cursor, device);
	struct pointer_device *pd = calloc(1, sizeof(*pd));
	pd->device = device;
	pd->destroy.notify = pointer_device_destroy;
	wl_signal_add(&device->events.destroy, &pd->destroy);
	wl_list_insert(&server->pointers, &pd->link);
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
		add_pointer(server, device);
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

// --------------------------------------------- keyboard focus, constraints

static void constraint_deactivate(struct server *server)
{
	if (!server->active_constraint)
		return;
	wlr_pointer_constraint_v1_send_deactivated(server->active_constraint);
	wl_list_remove(&server->active_constraint_destroy.link);
	server->active_constraint = NULL;
}

static void active_constraint_destroy(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, active_constraint_destroy);
	wl_list_remove(&server->active_constraint_destroy.link);
	server->active_constraint = NULL;
}

// a lock or confinement is active while its surface has the keyboard
static void constraint_activate_for(struct server *server, struct wlr_surface *surface)
{
	struct wlr_pointer_constraint_v1 *constraint = surface
			? wlr_pointer_constraints_v1_constraint_for_surface(server->constraints, surface, server->seat)
			: NULL;
	if (constraint == server->active_constraint)
		return;
	constraint_deactivate(server);
	if (!constraint)
		return;
	server->active_constraint = constraint;
	server->active_constraint_destroy.notify = active_constraint_destroy;
	wl_signal_add(&constraint->events.destroy, &server->active_constraint_destroy);
	wlr_pointer_constraint_v1_send_activated(constraint);
}

static void new_constraint(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_constraint);
	struct wlr_pointer_constraint_v1 *constraint = data;
	if (constraint->surface == server->seat->keyboard_state.focused_surface)
		constraint_activate_for(server, constraint->surface);
}

void input_focus_surface(struct server *server, struct wlr_surface *surface)
{
	struct wlr_seat *seat = server->seat;
	if (!surface) {
		wlr_seat_keyboard_notify_clear_focus(seat);
		constraint_deactivate(server);
		return;
	}
	// keyboard focus even before a keyboard is plugged in (the tests'
	// virtual one, a USB keyboard later): the program is ready for keys
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard)
		wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
				&keyboard->modifiers);
	else
		wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
	constraint_activate_for(server, surface);
}

// ------------------------------------------------------- cursor, pointer

void input_reset_cursor_mode(struct server *server)
{
	server->cursor_mode = CURSOR_PASSTHROUGH;
	server->grabbed_view = NULL;
}

// (positions are rounded: the pointer is at fractions of a pixel, and
// truncating 339.9999 left windows a pixel short half the time)
static void process_move(struct server *server)
{
	view_move(server->grabbed_view, (int)lround(server->cursor->x - server->grab_x),
			(int)lround(server->cursor->y - server->grab_y));
}

static void process_resize(struct server *server)
{
	struct view *view = server->grabbed_view;
	int border_x = (int)lround(server->cursor->x - server->grab_x);
	int border_y = (int)lround(server->cursor->y - server->grab_y);
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
	// X11 windows move now; xdg windows when they've drawn the new size
	// (toplevel_commit keeps the opposite edge still)
	if (view->type == VIEW_XWAYLAND)
		view_move(view, left, top);
	view_resize(view, right - left, bottom - top);
}

// pick the surface under the pointer and tell it where the pointer is
static void pointer_focus(struct server *server, uint32_t time)
{
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
	struct view *view = view_from_surface_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy, NULL);
	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		// the desktop, or a window frame: its edges show the resize arrows
		uint32_t edges = WLR_EDGE_NONE;
		enum frame_part part = view ? frame_part_at(view, server->cursor->x, server->cursor->y, &edges) : PART_NONE;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, frame_cursor(part, edges));
		wlr_seat_pointer_clear_focus(server->seat);
	}
}

static uint32_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// something appeared or went away under a still pointer
void input_refresh_pointer(struct server *server)
{
	if (server->cursor_mode == CURSOR_PASSTHROUGH) {
		pointer_focus(server, now_ms());
		wlr_seat_pointer_notify_frame(server->seat);
	}
}

static void process_motion(struct server *server, uint32_t time)
{
	activity(server);
	if (server->frame_press_view) {
		// a title button held: it shows pressed only while the pointer is
		// over it (and acts on release only then)
		struct view *view = server->frame_press_view;
		uint32_t edges;
		enum frame_part part = frame_part_at(view, server->cursor->x, server->cursor->y, &edges);
		frame_set_pressed(view, part == server->frame_press_part ? part : PART_NONE);
		return;
	}
	if (server->cursor_mode == CURSOR_MOVE) {
		process_move(server);
		return;
	} else if (server->cursor_mode == CURSOR_RESIZE) {
		process_resize(server);
		return;
	}
	pointer_focus(server, time);
}

static void cursor_motion(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	double dx = event->delta_x, dy = event->delta_y;
	// games and remote desktops get the raw movement too
	wlr_relative_pointer_manager_v1_send_relative_motion(server->relative_pointers, server->seat,
			(uint64_t)event->time_msec * 1000, dx, dy, event->unaccel_dx, event->unaccel_dy);
	struct wlr_pointer_constraint_v1 *c = server->active_constraint;
	if (c && server->seat->pointer_state.focused_surface == c->surface) {
		if (c->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
			activity(server);
			return; // the pointer stays put
		}
		// confined: stop at the region's edge
		double sx = server->seat->pointer_state.sx, sy = server->seat->pointer_state.sy;
		double cx, cy;
		if (wlr_region_confine(&c->region, sx, sy, sx + dx, sy + dy, &cx, &cy)) {
			dx = cx - sx;
			dy = cy - sy;
		}
	}
	wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
	process_motion(server, event->time_msec);
}

static void cursor_motion_absolute(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_motion(server, event->time_msec);
}

// ------------------------------------------------- window frames' clicks

// a press on a window's frame (decoration.c): the title bar moves it (two
// quick clicks maximize or restore it), the edges resize it, the buttons
// act when released over them
static void frame_pressed(struct server *server, struct view *view, uint32_t button, uint32_t time)
{
	if (button != BTN_LEFT)
		return; // (the window menu on the right button: phase 4)
	uint32_t edges;
	enum frame_part part = frame_part_at(view, server->cursor->x, server->cursor->y, &edges);
	switch (part) {
	case PART_TITLE:
		if (server->last_title_click_view == view
				&& time - server->last_title_click_ms <= (uint32_t)server->settings.double_click_ms) {
			server->last_title_click_view = NULL;
			view_set_maximized(view, !view->maximized);
			return;
		}
		server->last_title_click_view = view;
		server->last_title_click_ms = time;
		if (!view->maximized) // Win95 doesn't move maximized windows
			view_begin_interactive(view, CURSOR_MOVE, 0);
		break;
	case PART_EDGE:
		view_begin_interactive(view, CURSOR_RESIZE, edges);
		break;
	case PART_MINIMIZE:
	case PART_MAXIMIZE:
	case PART_CLOSE:
		server->frame_press_view = view;
		server->frame_press_part = part;
		frame_set_pressed(view, part);
		break;
	default:
		break;
	}
}

static void frame_button_released(struct server *server)
{
	struct view *view = server->frame_press_view;
	enum frame_part pressed = server->frame_press_part;
	server->frame_press_view = NULL;
	frame_set_pressed(view, PART_NONE);
	uint32_t edges;
	if (frame_part_at(view, server->cursor->x, server->cursor->y, &edges) != pressed)
		return; // let go somewhere else: nothing happens
	switch (pressed) {
	case PART_MINIMIZE:
		view_set_minimized(view, true);
		break;
	case PART_MAXIMIZE:
		view_set_maximized(view, !view->maximized);
		break;
	case PART_CLOSE:
		view_close(view);
		break;
	default:
		break;
	}
}

static void cursor_button(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	activity(server);
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		input_reset_cursor_mode(server);
		if (server->frame_press_view && event->button == BTN_LEFT)
			frame_button_released(server);
		if (server->seat->pointer_state.button_count == 0) {
			// the grab ends: the pointer goes to whatever is under it now
			server->press_surface = NULL;
			pointer_focus(server, event->time_msec);
		}
		return;
	}
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct node_tag *owner = NULL;
	struct view *view = view_from_surface_at(server, server->cursor->x, server->cursor->y,
			&surface, &sx, &sy, &owner);
	if (surface && server->seat->pointer_state.button_count == 1) {
		// the first button down starts the implicit grab
		server->press_surface = surface;
		server->press_ox = server->cursor->x - sx;
		server->press_oy = server->cursor->y - sy;
	}
	// a press raises and focuses what's under it
	if (view) {
		if (server->switching)
			switch_end(server, false);
		view_focus(view);
		if (!surface)
			frame_pressed(server, view, event->button, event->time_msec);
	} else if (owner && owner->kind == NODE_LAYER) {
		struct layer_surface *ls = (struct layer_surface *)owner;
		if (ls->layer_surface->current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
			input_focus_surface(server, ls->layer_surface->surface);
	} else if (owner && owner->kind == NODE_LOCK && surface) {
		input_focus_surface(server, surface);
	}
}

static void cursor_axis(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	activity(server);
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

// touchpad gestures, passed to the program under the pointer
#define GESTURE(name, type, call) \
	static void name(struct wl_listener *listener, void *data) \
	{ \
		struct server *server = wl_container_of(listener, server, name); \
		type *e = data; \
		activity(server); \
		call; \
	}
GESTURE(swipe_begin, struct wlr_pointer_swipe_begin_event,
	wlr_pointer_gestures_v1_send_swipe_begin(server->gestures, server->seat, e->time_msec, e->fingers))
GESTURE(swipe_update, struct wlr_pointer_swipe_update_event,
	wlr_pointer_gestures_v1_send_swipe_update(server->gestures, server->seat, e->time_msec, e->dx, e->dy))
GESTURE(swipe_end, struct wlr_pointer_swipe_end_event,
	wlr_pointer_gestures_v1_send_swipe_end(server->gestures, server->seat, e->time_msec, e->cancelled))
GESTURE(pinch_begin, struct wlr_pointer_pinch_begin_event,
	wlr_pointer_gestures_v1_send_pinch_begin(server->gestures, server->seat, e->time_msec, e->fingers))
GESTURE(pinch_update, struct wlr_pointer_pinch_update_event,
	wlr_pointer_gestures_v1_send_pinch_update(server->gestures, server->seat, e->time_msec, e->dx, e->dy,
		e->scale, e->rotation))
GESTURE(pinch_end, struct wlr_pointer_pinch_end_event,
	wlr_pointer_gestures_v1_send_pinch_end(server->gestures, server->seat, e->time_msec, e->cancelled))
GESTURE(hold_begin, struct wlr_pointer_hold_begin_event,
	wlr_pointer_gestures_v1_send_hold_begin(server->gestures, server->seat, e->time_msec, e->fingers))
GESTURE(hold_end, struct wlr_pointer_hold_end_event,
	wlr_pointer_gestures_v1_send_hold_end(server->gestures, server->seat, e->time_msec, e->cancelled))

// ------------------------------------------------------------- the seat

static void request_set_cursor(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, request_set_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	// only the program the pointer is over may set it
	if (server->seat->pointer_state.focused_client == event->seat_client)
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

static void request_set_shape(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, request_set_shape);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (server->seat->pointer_state.focused_client == event->seat_client)
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, wlr_cursor_shape_v1_name(event->shape));
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

// drag and drop: allowed while the button that started it is down
static void request_start_drag(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, request_start_drag);
	struct wlr_seat_request_start_drag_event *event = data;
	if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial))
		wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

struct drag_icon {
	struct wlr_scene_tree *tree;
	struct server *server;
	struct wl_listener motion, destroy;
};

static void drag_icon_follow(struct drag_icon *icon)
{
	wlr_scene_node_set_position(&icon->tree->node, icon->server->cursor->x, icon->server->cursor->y);
}

static void drag_icon_motion(struct wl_listener *listener, void *data)
{
	struct drag_icon *icon = wl_container_of(listener, icon, motion);
	drag_icon_follow(icon);
}

static void drag_icon_destroy(struct wl_listener *listener, void *data)
{
	struct drag_icon *icon = wl_container_of(listener, icon, destroy);
	wl_list_remove(&icon->motion.link);
	wl_list_remove(&icon->destroy.link);
	free(icon);
}

static void start_drag(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, start_drag);
	struct wlr_drag *drag = data;
	if (!drag->icon)
		return;
	// the dragged thing's picture follows the pointer
	struct drag_icon *icon = calloc(1, sizeof(*icon));
	icon->server = server;
	icon->tree = wlr_scene_drag_icon_create(server->drag_icons, drag->icon);
	drag_icon_follow(icon);
	icon->motion.notify = drag_icon_motion;
	wl_signal_add(&server->cursor->events.motion, &icon->motion);
	icon->destroy.notify = drag_icon_destroy;
	wl_signal_add(&drag->icon->events.destroy, &icon->destroy);
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
#define LISTEN(name) server->name.notify = name; wl_signal_add(&server->cursor->events.name, &server->name)
	LISTEN(swipe_begin); LISTEN(swipe_update); LISTEN(swipe_end);
	LISTEN(pinch_begin); LISTEN(pinch_update); LISTEN(pinch_end);
	LISTEN(hold_begin); LISTEN(hold_end);
#undef LISTEN

	server->new_input.notify = new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);

	struct wlr_virtual_pointer_manager_v1 *vp = wlr_virtual_pointer_manager_v1_create(server->display);
	server->new_virtual_pointer.notify = new_virtual_pointer;
	wl_signal_add(&vp->events.new_virtual_pointer, &server->new_virtual_pointer);
	struct wlr_virtual_keyboard_manager_v1 *vk = wlr_virtual_keyboard_manager_v1_create(server->display);
	server->new_virtual_keyboard.notify = new_virtual_keyboard;
	wl_signal_add(&vk->events.new_virtual_keyboard, &server->new_virtual_keyboard);

	server->relative_pointers = wlr_relative_pointer_manager_v1_create(server->display);
	server->gestures = wlr_pointer_gestures_v1_create(server->display);
	server->constraints = wlr_pointer_constraints_v1_create(server->display);
	server->new_constraint.notify = new_constraint;
	wl_signal_add(&server->constraints->events.new_constraint, &server->new_constraint);

	server->seat = wlr_seat_create(server->display, "seat0");
	server->kbd_group = wlr_keyboard_group_create();
	settings_apply_keyboard(server, &server->kbd_group->keyboard);
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
	server->request_start_drag.notify = request_start_drag;
	wl_signal_add(&server->seat->events.request_start_drag, &server->request_start_drag);
	server->start_drag.notify = start_drag;
	wl_signal_add(&server->seat->events.start_drag, &server->start_drag);

	struct wlr_cursor_shape_manager_v1 *shapes = wlr_cursor_shape_manager_v1_create(server->display, 1);
	server->request_set_shape.notify = request_set_shape;
	wl_signal_add(&shapes->events.request_set_shape, &server->request_set_shape);
	update_capabilities(server);
}
