// Input: keyboards, pointers (mice, touchpads, and the virtual ones the
// tests drive), the cursor, gestures, pointer constraints (games), drag and
// drop, and the seat that hands it all to programs.
#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>

#include "server.h"

static void activity(struct server *server)
{
	// input resets the idle timer (screen saver, display off)
	wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
}

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
	activity(keyboard->server);
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

static void process_move(struct server *server)
{
	view_move(server->grabbed_view, server->cursor->x - server->grab_x, server->cursor->y - server->grab_y);
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
	view_from_surface_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy, NULL);
	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
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

static void cursor_button(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	activity(server);
	wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		input_reset_cursor_mode(server);
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
		view_focus(view);
	} else if (owner && owner->kind == NODE_LAYER) {
		struct layer_surface *ls = (struct layer_surface *)owner;
		if (ls->layer_surface->current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
			input_focus_surface(server, ls->layer_surface->surface);
	} else if (owner && owner->kind == NODE_LOCK) {
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
	server->request_start_drag.notify = request_start_drag;
	wl_signal_add(&server->seat->events.request_start_drag, &server->request_start_drag);
	server->start_drag.notify = start_drag;
	wl_signal_add(&server->seat->events.start_drag, &server->start_drag);

	struct wlr_cursor_shape_manager_v1 *shapes = wlr_cursor_shape_manager_v1_create(server->display, 1);
	server->request_set_shape.notify = request_set_shape;
	wl_signal_add(&shapes->events.request_set_shape, &server->request_set_shape);
	update_capabilities(server);
}
