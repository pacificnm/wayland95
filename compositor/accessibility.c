// Control Panel > Accessibility Options, done where every key arrives first
// (the Qt compositor did the same in src/accessibility.cpp):
//  - StickyKeys: Shift, Ctrl, Alt or the Windows key, pressed and let go,
//    stays down for the next key or click; pressed twice it's locked down
//    until pressed again
//  - FilterKeys: ignores a key pressed again too soon after it was let go
//    (bounce keys), and keys not held long enough (slow keys)
//  - ToggleKeys: a high tone when Caps/Num/Scroll Lock turns on, low off
//  - MouseKeys: the number pad moves the pointer and clicks
//  - the Win95 shortcuts that turn them on and off
// Programs get the result as ordinary input. The shell (wayland95-shell-v1)
// makes the sounds, shows which keys StickyKeys holds, and turns features
// on and off when their shortcut is used (writing settings.ini, which
// settings.c follows: a11y_settings_changed).
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

#include "server.h"
#include "wayland95-shell-v1-protocol.h"

enum sticky_state { STICKY_UP, STICKY_LATCHED, STICKY_LOCKED };
enum { MOD_SHIFT, MOD_CTRL, MOD_ALT, MOD_LOGO, MOD_N };
static const char *const mod_names[MOD_N] = { "Shift", "Ctrl", "Alt", "Windows" };

struct sticky_mod {
	struct keyboard *keyboard; // where its press came from (its release goes there)
	uint32_t keycode;
	bool down, used_with_key, swallow_release;
	enum sticky_state state;
};

struct slow_key {
	bool pending;
	struct keyboard *keyboard;
	uint32_t keycode;
	struct wl_event_source *timer;
};

static struct {
	struct server *server;
	bool owns; // StickyKeys or FilterKeys: modifiers come from what gets through
	// StickyKeys
	struct sticky_mod mods[MOD_N];
	// FilterKeys
	uint32_t last_release_key, last_release_ms;
	bool bounced[KEY_CNT];
	bool slow_accepted[KEY_CNT];
	struct slow_key slow[8];
	// ToggleKeys
	bool scroll_lock;
	// MouseKeys
	unsigned move_keys; // bit per direction key held
	struct wl_event_source *move_timer;
	uint32_t move_start_ms, move_last_ms;
	double move_rest_x, move_rest_y;
	uint32_t mouse_button;   // the one 5, + and 0 use (/, * and - choose)
	bool button_held;        // 0 holds it down (dragging), . lets go
	// the shortcuts
	int shift_presses;
	uint32_t last_shift_ms;
	struct wl_event_source *right_shift_timer, *num_lock_timer;
} a;

static uint32_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int modifier_of(uint32_t keycode)
{
	switch (keycode) {
	case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: return MOD_SHIFT;
	case KEY_LEFTCTRL: case KEY_RIGHTCTRL: return MOD_CTRL;
	case KEY_LEFTALT: case KEY_RIGHTALT: return MOD_ALT;
	case KEY_LEFTMETA: case KEY_RIGHTMETA: return MOD_LOGO;
	default: return -1;
	}
}

bool a11y_owns_modifiers(struct server *server)
{
	return a.owns;
}

// the raw keyboard's own modifiers (what's physically down)
static bool raw_mod(struct keyboard *keyboard, const char *name, enum xkb_state_component which)
{
	struct xkb_state *state = keyboard->wlr_keyboard->xkb_state;
	return state && xkb_state_mod_name_is_active(state, name, which) > 0;
}

// ------------------------------------------------------------- StickyKeys

static void announce_sticky(void)
{
	char held[128] = "";
	for (int i = 0; i < MOD_N; ++i) {
		if (a.mods[i].state == STICKY_UP)
			continue;
		size_t len = strlen(held);
		snprintf(held + len, sizeof held - len, "%s%s%s", len ? " + " : "", mod_names[i],
				a.mods[i].state == STICKY_LOCKED ? " (locked)" : "");
	}
	shell_send_sticky(a.server, held);
}

static void tone(uint32_t which)
{
	shell_send_tone(a.server, which);
}

// the latched modifiers have been used: let them go (locked ones stay)
static void release_latched(uint32_t time)
{
	bool any = false;
	for (int i = 0; i < MOD_N; ++i) {
		struct sticky_mod *m = &a.mods[i];
		if (m->state != STICKY_LATCHED)
			continue;
		m->state = STICKY_UP;
		any = true;
		if (!m->down && m->keyboard)
			input_key(a.server, m->keyboard, time, m->keycode, false);
	}
	if (any)
		announce_sticky();
}

// all of them (StickyKeys turned off)
static void clear_sticky(void)
{
	bool any = false;
	for (int i = 0; i < MOD_N; ++i) {
		struct sticky_mod *m = &a.mods[i];
		if (m->state != STICKY_UP && !m->down && m->keyboard)
			input_key(a.server, m->keyboard, now_ms(), m->keycode, false);
		any = any || m->state != STICKY_UP;
		m->state = STICKY_UP;
		m->swallow_release = false;
	}
	if (any)
		announce_sticky();
}

// true: the key is used up here
static bool sticky(struct keyboard *keyboard, uint32_t time, uint32_t keycode, bool pressed)
{
	const struct settings *s = &a.server->settings;
	int which = modifier_of(keycode);
	if (which < 0) {
		if (pressed)
			for (int i = 0; i < MOD_N; ++i)
				if (a.mods[i].down)
					a.mods[i].used_with_key = true;
		return false; // (its release lets the latched ones go: a11y_key)
	}
	struct sticky_mod *m = &a.mods[which];
	if (pressed) {
		if (m->state == STICKY_LOCKED) {
			// third press: unlocked; its release lets programs see it go
			m->state = STICKY_UP;
			m->down = true;
			m->used_with_key = true;
			announce_sticky();
			if (s->a11y.sticky_sound)
				tone(WAYLAND95_SHELL_V1_TONE_LOW);
			return true;
		}
		if (m->state == STICKY_LATCHED) {
			// second press: locked (or, without locking, let go)
			m->state = s->a11y.sticky_lock ? STICKY_LOCKED : STICKY_UP;
			m->swallow_release = s->a11y.sticky_lock;
			m->down = true;
			m->used_with_key = true;
			announce_sticky();
			if (s->a11y.sticky_sound)
				tone(s->a11y.sticky_lock ? WAYLAND95_SHELL_V1_TONE_HIGH : WAYLAND95_SHELL_V1_TONE_LOW);
			return true;
		}
		m->keyboard = keyboard;
		m->keycode = keycode;
		m->down = true;
		m->used_with_key = false;
		return false;
	}
	// released
	m->down = false;
	if (m->swallow_release) {
		m->swallow_release = false;
		return true;
	}
#ifdef WAYLAND95_TEST_BREAK_STICKY // tests/selftest.sh: StickyKeys never holds a key
	return false;
#endif
	if (m->state == STICKY_UP && !m->used_with_key && m->keycode == keycode) {
		// pressed and let go on its own: it stays down for the next key
		m->state = STICKY_LATCHED;
		announce_sticky();
		if (s->a11y.sticky_sound)
			tone(WAYLAND95_SHELL_V1_TONE_CLICK);
		return true;
	}
	return false;
}

void a11y_pointer_released(struct server *server)
{
	if (server->settings.a11y.sticky_keys)
		release_latched(now_ms());
}

// ------------------------------------------------------------- MouseKeys

static void direction(uint32_t keycode, int *dx, int *dy)
{
	*dx = *dy = 0;
	switch (keycode) {
	case KEY_KP1: *dx = -1; *dy = 1; break;
	case KEY_KP2: *dy = 1; break;
	case KEY_KP3: *dx = 1; *dy = 1; break;
	case KEY_KP4: *dx = -1; break;
	case KEY_KP6: *dx = 1; break;
	case KEY_KP7: *dx = -1; *dy = -1; break;
	case KEY_KP8: *dy = -1; break;
	case KEY_KP9: *dx = 1; *dy = -1; break;
	}
}

static const uint32_t direction_keys[] = { KEY_KP1, KEY_KP2, KEY_KP3, KEY_KP4, KEY_KP6, KEY_KP7, KEY_KP8, KEY_KP9 };

static void move_step(bool first)
{
	int dx = 0, dy = 0;
	for (size_t i = 0; i < sizeof direction_keys / sizeof direction_keys[0]; ++i) {
		if (!(a.move_keys & (1u << i)))
			continue;
		int x, y;
		direction(direction_keys[i], &x, &y);
		dx += x;
		dy += y;
	}
	dx = dx < -1 ? -1 : dx > 1 ? 1 : dx;
	dy = dy < -1 ? -1 : dy > 1 ? 1 : dy;
	if (!dx && !dy)
		return;
	const struct settings *s = &a.server->settings;
	const uint32_t now = now_ms();
	double step;
	if (first) {
		step = 1; // the first press nudges by a pixel
	} else {
		// speeds up to the top speed; acceleration sets how quickly
		const double top = 60 + s->a11y.mouse_keys_speed * 90;               // pixels a second
		const double ramp = (11 - s->a11y.mouse_keys_acceleration) * 0.25;   // seconds to get there
		const double t = (now - a.move_start_ms) / 1000.0;
		const double dt = fmax(0.001, (now - a.move_last_ms) / 1000.0);
		const double v = top * fmin(1.0, 0.08 + t / ramp);
		step = v * dt;
	}
	a.move_last_ms = now;
	if (dx && dy)
		step /= M_SQRT2;
	double mx = dx * step + a.move_rest_x, my = dy * step + a.move_rest_y;
	double ix = trunc(mx), iy = trunc(my);
	a.move_rest_x = mx - ix;
	a.move_rest_y = my - iy;
	if (ix || iy)
		input_pointer_move(a.server, now, ix, iy);
}

static int move_tick(void *data)
{
	move_step(false);
	if (a.move_keys)
		wl_event_source_timer_update(a.move_timer, 16);
	return 0;
}

static void click(uint32_t time, int times)
{
	for (int i = 0; i < times; ++i) {
		input_pointer_button(a.server, time, a.mouse_button, true);
		input_pointer_button(a.server, time, a.mouse_button, false);
	}
}

// true: a number pad key MouseKeys used
static bool mouse_key(struct keyboard *keyboard, uint32_t time, uint32_t keycode, bool pressed)
{
	const struct settings *s = &a.server->settings;
	if (!s->a11y.mouse_keys)
		return false;
	bool digit = false;
	switch (keycode) {
	case KEY_KP0: case KEY_KP1: case KEY_KP2: case KEY_KP3: case KEY_KP4: case KEY_KP5:
	case KEY_KP6: case KEY_KP7: case KEY_KP8: case KEY_KP9: case KEY_KPDOT:
		digit = true;
		break;
	case KEY_KPSLASH: case KEY_KPASTERISK: case KEY_KPMINUS: case KEY_KPPLUS:
		break;
	default:
		return false;
	}
	// the digits move the pointer only with Num Lock as chosen; the
	// operator keys always choose buttons
	if (digit && raw_mod(keyboard, XKB_MOD_NAME_NUM, XKB_STATE_MODS_LOCKED) != s->a11y.mouse_keys_numlock_on)
		return false;

	for (size_t i = 0; i < sizeof direction_keys / sizeof direction_keys[0]; ++i) {
		if (direction_keys[i] != keycode)
			continue;
		if (pressed) {
			const bool moving = a.move_keys != 0;
			a.move_keys |= 1u << i;
			if (!moving) {
				a.move_start_ms = a.move_last_ms = now_ms();
				a.move_rest_x = a.move_rest_y = 0;
				move_step(true);
				wl_event_source_timer_update(a.move_timer, 16);
			}
		} else {
			a.move_keys &= ~(1u << i);
			if (!a.move_keys)
				wl_event_source_timer_update(a.move_timer, 0);
		}
		return true;
	}
	if (!pressed)
		return true; // (their releases)
	switch (keycode) {
	case KEY_KP5: click(time, 1); break;            // click
	case KEY_KPPLUS: click(time, 2); break;         // double-click
	case KEY_KPSLASH: a.mouse_button = BTN_LEFT; break;
	case KEY_KPASTERISK: a.mouse_button = BTN_MIDDLE; break;
	case KEY_KPMINUS: a.mouse_button = BTN_RIGHT; break;
	case KEY_KP0: // hold the button down (dragging)
		if (!a.button_held) {
			a.button_held = true;
			input_pointer_button(a.server, time, a.mouse_button, true);
		}
		break;
	case KEY_KPDOT: // let it go
		if (a.button_held) {
			a.button_held = false;
			input_pointer_button(a.server, time, a.mouse_button, false);
		}
		break;
	}
	return true;
}

static void stop_mouse_keys(void)
{
	a.move_keys = 0;
	if (a.move_timer)
		wl_event_source_timer_update(a.move_timer, 0);
	if (a.button_held) {
		a.button_held = false;
		input_pointer_button(a.server, now_ms(), a.mouse_button, false);
	}
}

// --------------------------------------------------- after FilterKeys

static void pass_on(struct keyboard *keyboard, uint32_t time, uint32_t keycode, bool pressed)
{
	const struct settings *s = &a.server->settings;
	if (mouse_key(keyboard, time, keycode, pressed))
		return;
	// ToggleKeys: the lock's state before this press is in the keyboard's
	// (wlroots updates it after telling us of the key)
	if (s->a11y.toggle_keys && pressed) {
		int on = -1;
		if (keycode == KEY_CAPSLOCK)
			on = !raw_mod(keyboard, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED);
		else if (keycode == KEY_NUMLOCK)
			on = !raw_mod(keyboard, XKB_MOD_NAME_NUM, XKB_STATE_MODS_LOCKED);
		else if (keycode == KEY_SCROLLLOCK)
			on = a.scroll_lock = !a.scroll_lock;
		if (on >= 0)
			tone(on ? WAYLAND95_SHELL_V1_TONE_HIGH : WAYLAND95_SHELL_V1_TONE_LOW);
	}
	if (s->a11y.sticky_keys && sticky(keyboard, time, keycode, pressed))
		return;
	input_key(a.server, keyboard, time, keycode, pressed);
	// the key that used the latched modifiers is done: let them go
	if (s->a11y.sticky_keys && !pressed && modifier_of(keycode) < 0)
		release_latched(time);
}

// ------------------------------------------------------------ FilterKeys

static int slow_key_held(void *data)
{
	struct slow_key *k = data;
	if (!k->pending)
		return 0;
	k->pending = false;
	if (k->keycode < KEY_CNT)
		a.slow_accepted[k->keycode] = true;
	tone(WAYLAND95_SHELL_V1_TONE_CLICK);
	pass_on(k->keyboard, now_ms(), k->keycode, true);
	return 0;
}

static struct slow_key *slow_pending(uint32_t keycode)
{
	for (size_t i = 0; i < sizeof a.slow / sizeof a.slow[0]; ++i)
		if (a.slow[i].pending && a.slow[i].keycode == keycode)
			return &a.slow[i];
	return NULL;
}

static void cancel_slow(struct slow_key *k)
{
	k->pending = false;
	if (k->timer)
		wl_event_source_timer_update(k->timer, 0);
}

// true: held back (or dropped)
static bool filter(struct keyboard *keyboard, uint32_t time, uint32_t keycode, bool pressed)
{
	const struct settings *s = &a.server->settings;
	if (keycode >= KEY_CNT)
		return false;
	if (pressed) {
		// bounce keys: pressed again too soon after it was let go
		if (s->a11y.filter_bounce_ms > 0 && keycode == a.last_release_key
				&& time - a.last_release_ms < (uint32_t)s->a11y.filter_bounce_ms) {
			a.bounced[keycode] = true;
			return true;
		}
		if (a.bounced[keycode] || slow_pending(keycode))
			return true;
		// slow keys: only once it's been held long enough
		if (s->a11y.filter_slow_ms > 0 && !a.slow_accepted[keycode]) {
			for (size_t i = 0; i < sizeof a.slow / sizeof a.slow[0]; ++i) {
				struct slow_key *k = &a.slow[i];
				if (k->pending)
					continue;
				k->pending = true;
				k->keyboard = keyboard;
				k->keycode = keycode;
				if (!k->timer)
					k->timer = wl_event_loop_add_timer(a.server->event_loop, slow_key_held, k);
				wl_event_source_timer_update(k->timer, s->a11y.filter_slow_ms);
				return true;
			}
			return true; // (eight keys held at once, not yet accepted: drop it)
		}
		return false;
	}
	if (a.bounced[keycode]) {
		a.bounced[keycode] = false;
		return true;
	}
	struct slow_key *k = slow_pending(keycode);
	if (k) {
		cancel_slow(k); // let go too soon: never happened
		return true;
	}
	a.slow_accepted[keycode] = false;
	a.last_release_key = keycode;
	a.last_release_ms = time;
	return false;
}

static void clear_filter(void)
{
	for (size_t i = 0; i < sizeof a.slow / sizeof a.slow[0]; ++i)
		cancel_slow(&a.slow[i]);
	memset(a.bounced, 0, sizeof a.bounced);
	memset(a.slow_accepted, 0, sizeof a.slow_accepted);
}

// --------------------------------------------------------- the shortcuts

static int right_shift_held(void *data)
{
	if (a.server->settings.a11y.filter_shortcut)
		shell_send_shortcut(a.server, WAYLAND95_SHELL_V1_FEATURE_FILTER_KEYS);
	return 0;
}

static int num_lock_held(void *data)
{
	if (a.server->settings.a11y.toggle_shortcut)
		shell_send_shortcut(a.server, WAYLAND95_SHELL_V1_FEATURE_TOGGLE_KEYS);
	return 0;
}

static void shortcut_keys(struct keyboard *keyboard, uint32_t time, uint32_t keycode, bool pressed)
{
	const struct settings *s = &a.server->settings;
	if (!pressed) {
		if (keycode == KEY_RIGHTSHIFT)
			wl_event_source_timer_update(a.right_shift_timer, 0);
		if (keycode == KEY_NUMLOCK)
			wl_event_source_timer_update(a.num_lock_timer, 0);
		return;
	}
	const uint32_t now = now_ms();
	if (keycode == KEY_LEFTSHIFT || keycode == KEY_RIGHTSHIFT) {
		// Shift five times: StickyKeys
		if (now - a.last_shift_ms > 1500)
			a.shift_presses = 0;
		a.last_shift_ms = now;
		if (++a.shift_presses == 5) {
			a.shift_presses = 0;
			if (s->a11y.sticky_shortcut)
				shell_send_shortcut(a.server, WAYLAND95_SHELL_V1_FEATURE_STICKY_KEYS);
		}
		// right Shift held for eight seconds: FilterKeys
		if (keycode == KEY_RIGHTSHIFT)
			wl_event_source_timer_update(a.right_shift_timer, 8000);
	} else {
		a.shift_presses = 0;
		wl_event_source_timer_update(a.right_shift_timer, 0);
	}
	const bool alt_shift = raw_mod(keyboard, XKB_MOD_NAME_ALT, XKB_STATE_MODS_DEPRESSED)
			&& raw_mod(keyboard, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_DEPRESSED);
	if (keycode == KEY_NUMLOCK) {
		// Left Alt + Left Shift + Num Lock: MouseKeys; Num Lock held for
		// five seconds: ToggleKeys
		if (alt_shift) {
			if (s->a11y.mouse_keys_shortcut)
				shell_send_shortcut(a.server, WAYLAND95_SHELL_V1_FEATURE_MOUSE_KEYS);
		} else {
			wl_event_source_timer_update(a.num_lock_timer, 5000);
		}
	} else {
		wl_event_source_timer_update(a.num_lock_timer, 0);
	}
	// Left Alt + Left Shift + Print Screen: High Contrast
	if (keycode == KEY_SYSRQ && alt_shift && s->a11y.high_contrast_shortcut)
		shell_send_shortcut(a.server, WAYLAND95_SHELL_V1_FEATURE_HIGH_CONTRAST);
}

// --------------------------------------------------------------- the way in

static void init(struct server *server)
{
	if (a.server)
		return;
	a.server = server;
	a.mouse_button = BTN_LEFT;
	a.move_timer = wl_event_loop_add_timer(server->event_loop, move_tick, NULL);
	a.right_shift_timer = wl_event_loop_add_timer(server->event_loop, right_shift_held, NULL);
	a.num_lock_timer = wl_event_loop_add_timer(server->event_loop, num_lock_held, NULL);
}

void a11y_key(struct server *server, struct keyboard *keyboard, uint32_t time, uint32_t keycode, bool pressed)
{
	init(server);
	shortcut_keys(keyboard, time, keycode, pressed);
	const struct settings *s = &server->settings;
	if (s->a11y.filter_keys && filter(keyboard, time, keycode, pressed))
		return;
	pass_on(keyboard, time, keycode, pressed);
}

void a11y_settings_changed(struct server *server)
{
	init(server);
	const struct settings *s = &server->settings;
	if (!s->a11y.sticky_keys)
		clear_sticky();
	if (!s->a11y.filter_keys)
		clear_filter();
	if (!s->a11y.mouse_keys)
		stop_mouse_keys();
	const bool owns = s->a11y.sticky_keys || s->a11y.filter_keys;
	if (owns == a.owns)
		return;
	a.owns = owns;
	wlr_log(WLR_INFO, "accessibility: %s", owns ? "keys pass StickyKeys/FilterKeys" : "keys go straight through");
	struct wlr_keyboard *kb = server->seat ? wlr_seat_get_keyboard(server->seat) : NULL;
	if (!owns && kb) // programs back in step with the keyboard itself
		wlr_seat_keyboard_notify_modifiers(server->seat, &kb->modifiers);
}

void a11y_keyboard_gone(struct server *server, struct keyboard *keyboard)
{
	// (what it held goes with it; the shell stops showing it)
	bool changed = false;
	for (int i = 0; i < MOD_N; ++i)
		if (a.mods[i].keyboard == keyboard) {
			changed = changed || a.mods[i].state != STICKY_UP;
			a.mods[i].keyboard = NULL;
			a.mods[i].state = STICKY_UP;
			a.mods[i].down = false;
		}
	if (changed)
		announce_sticky();
	for (size_t i = 0; i < sizeof a.slow / sizeof a.slow[0]; ++i)
		if (a.slow[i].pending && a.slow[i].keyboard == keyboard)
			cancel_slow(&a.slow[i]);
}

void a11y_finish(struct server *server)
{
	struct wl_event_source **sources[] = { &a.move_timer, &a.right_shift_timer, &a.num_lock_timer };
	for (size_t i = 0; i < sizeof sources / sizeof sources[0]; ++i)
		if (*sources[i]) {
			wl_event_source_remove(*sources[i]);
			*sources[i] = NULL;
		}
	for (size_t i = 0; i < sizeof a.slow / sizeof a.slow[0]; ++i)
		if (a.slow[i].timer) {
			wl_event_source_remove(a.slow[i].timer);
			a.slow[i].timer = NULL;
		}
}
