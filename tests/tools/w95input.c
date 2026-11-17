// w95input: drives the pointer and keyboard of a wlroots compositor for the
// tests, through the virtual-pointer and virtual-keyboard protocols (the same
// path as a real mouse and keyboard inside the compositor). Keys use the
// standard keymap with real keycodes and timestamps, like a physical
// keyboard (wtype sends its own keymap with every key, and time 0).
//
//   w95input ACTION...
//
//   move:X,Y            move to X,Y (layout pixels; the pixel's centre, so
//                       programs that round down get exactly X,Y)
//   down:BUTTON         press left, right or middle
//   up:BUTTON           release it
//   click:X,Y[,BUTTON]  move there, press, release (default left)
//   wheel:N             turn the wheel N notches (negative: up, positive: down)
//   key:NAME[+NAME...]  press and release keys (xkb names: Down, Return, a,
//                       ctrl+c, alt+Tab...); modifiers held while the last goes
//   keydown:NAME        (and keyup:NAME) hold or release one key
//   type:TEXT           type lowercase letters, digits and spaces
//   wait:SECONDS        pause (default after every action: 0.05 s)
//
// e.g. w95input click:200,300 wait:0.5 move:250,320 wheel:3
#define _GNU_SOURCE // memfd_create
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static uint32_t now_ms(void);
static struct zwlr_virtual_pointer_manager_v1 *manager;
static struct zwp_virtual_keyboard_manager_v1 *kb_manager;
static struct zwp_virtual_keyboard_v1 *kb;
static struct xkb_keymap *keymap;
static struct xkb_state *kb_state;
static struct wl_seat *seat;
static struct wl_output *output;
static int out_w, out_h;

static void output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph,
		int32_t subpixel, const char *make, const char *model, int32_t transform) {}
static void output_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t refresh)
{
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		out_w = w;
		out_h = h;
	}
}
static void output_done(void *d, struct wl_output *o) {}
static void output_scale(void *d, struct wl_output *o, int32_t f) {}
static const struct wl_output_listener output_listener = {
	.geometry = output_geometry, .mode = output_mode, .done = output_done, .scale = output_scale,
};

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
	if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0)
		manager = wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, version < 2 ? version : 2);
	else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0)
		kb_manager = wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
	else if (strcmp(interface, wl_seat_interface.name) == 0 && !seat)
		seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
	else if (strcmp(interface, wl_output_interface.name) == 0 && !output) {
		output = wl_registry_bind(registry, name, &wl_output_interface, 1);
		wl_output_add_listener(output, &output_listener, NULL);
	}
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { .global = global, .global_remove = global_remove };

static uint32_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void pause_s(double s)
{
	struct timespec ts = { (time_t)s, (long)((s - (time_t)s) * 1e9) };
	nanosleep(&ts, NULL);
}

// the keyboard, created the first time a key is used: the default keymap
// (the compositor's), sent once
static bool keyboard_ready(struct wl_display *display)
{
	if (kb)
		return true;
	if (!kb_manager || !seat) {
		fprintf(stderr, "w95input: the compositor has no virtual keyboard\n");
		return false;
	}
	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	keymap = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	kb_state = xkb_state_new(keymap);
	char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
	size_t size = strlen(text) + 1;
	int fd = memfd_create("w95input-keymap", MFD_CLOEXEC);
	if (fd < 0 || write(fd, text, size) != (ssize_t)size) {
		fprintf(stderr, "w95input: can't share the keymap\n");
		return false;
	}
	kb = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(kb_manager, seat);
	zwp_virtual_keyboard_v1_keymap(kb, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
	close(fd);
	free(text);
	wl_display_roundtrip(display);
	return true;
}

// the keycode that gives this keysym at level 0 (no Shift)
static xkb_keycode_t keycode_for(const char *name)
{
	char lower[64];
	snprintf(lower, sizeof lower, "%s", name);
	// friendly modifier names
	if (strcmp(lower, "ctrl") == 0) snprintf(lower, sizeof lower, "Control_L");
	else if (strcmp(lower, "alt") == 0) snprintf(lower, sizeof lower, "Alt_L");
	else if (strcmp(lower, "shift") == 0) snprintf(lower, sizeof lower, "Shift_L");
	else if (strcmp(lower, "super") == 0) snprintf(lower, sizeof lower, "Super_L");
	else if (strcmp(lower, " ") == 0) snprintf(lower, sizeof lower, "space");
	xkb_keysym_t sym = xkb_keysym_from_name(lower, XKB_KEYSYM_NO_FLAGS);
	if (sym == XKB_KEY_NoSymbol)
		sym = xkb_keysym_from_name(lower, XKB_KEYSYM_CASE_INSENSITIVE);
	// a character that isn't a keysym's name ("-", ".", "/"): by what it is
	// (only those typed without Shift)
	if (sym == XKB_KEY_NoSymbol && strlen(lower) == 1)
		sym = xkb_utf32_to_keysym((unsigned char)lower[0]);
	for (xkb_keycode_t kc = xkb_keymap_min_keycode(keymap); kc <= xkb_keymap_max_keycode(keymap); ++kc) {
		const xkb_keysym_t *syms;
		int n = xkb_keymap_key_get_syms_by_level(keymap, kc, 0, 0, &syms);
		for (int i = 0; i < n; ++i)
			if (syms[i] == sym)
				return kc;
	}
	return 0;
}

static void send_key(xkb_keycode_t kc, bool down)
{
	zwp_virtual_keyboard_v1_key(kb, now_ms(), kc - 8,
			down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
	xkb_state_update_key(kb_state, kc, down ? XKB_KEY_DOWN : XKB_KEY_UP);
	zwp_virtual_keyboard_v1_modifiers(kb,
			xkb_state_serialize_mods(kb_state, XKB_STATE_MODS_DEPRESSED),
			xkb_state_serialize_mods(kb_state, XKB_STATE_MODS_LATCHED),
			xkb_state_serialize_mods(kb_state, XKB_STATE_MODS_LOCKED),
			xkb_state_serialize_layout(kb_state, XKB_STATE_LAYOUT_EFFECTIVE));
}

static int button_code(const char *name)
{
	if (!name || !*name || strcmp(name, "left") == 0) return BTN_LEFT;
	if (strcmp(name, "right") == 0) return BTN_RIGHT;
	if (strcmp(name, "middle") == 0) return BTN_MIDDLE;
	return -1;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s move:X,Y | down:BUTTON | up:BUTTON | click:X,Y[,BUTTON] | wheel:N | wait:S ...\n", argv[0]);
		return 2;
	}
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "w95input: can't connect to WAYLAND_DISPLAY\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	wl_display_roundtrip(display);
	// several displays: positions are in the whole desktop (W95INPUT_EXTENT=WxH),
	// not the first display alone
	const char *extent = getenv("W95INPUT_EXTENT");
	if (extent)
		sscanf(extent, "%dx%d", &out_w, &out_h);
	if (!manager || !output || out_w <= 0) {
		fprintf(stderr, "w95input: the compositor has no virtual pointer or output\n");
		return 1;
	}
	struct zwlr_virtual_pointer_v1 *ptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, seat);

	for (int i = 1; i < argc; ++i) {
		const char *a = argv[i];
		int x, y;
		char name[32] = "";
		if (sscanf(a, "move:%d,%d", &x, &y) == 2) {
			zwlr_virtual_pointer_v1_motion_absolute(ptr, now_ms(), x * 16 + 8, y * 16 + 8, out_w * 16, out_h * 16);
			zwlr_virtual_pointer_v1_frame(ptr);
		} else if (sscanf(a, "down:%31s", name) == 1 || sscanf(a, "up:%31s", name) == 1) {
			int code = button_code(name);
			if (code < 0) { fprintf(stderr, "w95input: unknown button %s\n", name); return 2; }
			zwlr_virtual_pointer_v1_button(ptr, now_ms(), code,
					a[0] == 'd' ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
			zwlr_virtual_pointer_v1_frame(ptr);
		} else if (strncmp(a, "click:", 6) == 0 && sscanf(a, "click:%d,%d", &x, &y) == 2) {
			const char *comma = strchr(strchr(a, ',') + 1, ',');
			int code = button_code(comma ? comma + 1 : NULL);
			if (code < 0) { fprintf(stderr, "w95input: unknown button in %s\n", a); return 2; }
			zwlr_virtual_pointer_v1_motion_absolute(ptr, now_ms(), x * 16 + 8, y * 16 + 8, out_w * 16, out_h * 16);
			zwlr_virtual_pointer_v1_frame(ptr);
			wl_display_flush(display);
			pause_s(0.03);
			zwlr_virtual_pointer_v1_button(ptr, now_ms(), code, WL_POINTER_BUTTON_STATE_PRESSED);
			zwlr_virtual_pointer_v1_frame(ptr);
			wl_display_flush(display);
			pause_s(0.05);
			zwlr_virtual_pointer_v1_button(ptr, now_ms(), code, WL_POINTER_BUTTON_STATE_RELEASED);
			zwlr_virtual_pointer_v1_frame(ptr);
		} else if (sscanf(a, "wheel:%d", &x) == 1) {
			int steps = x < 0 ? -x : x, dir = x < 0 ? -1 : 1;
			for (int s = 0; s < steps; ++s) {
				// as a mouse wheel: 15 degrees, one click, per notch
				zwlr_virtual_pointer_v1_axis_source(ptr, WL_POINTER_AXIS_SOURCE_WHEEL);
				zwlr_virtual_pointer_v1_axis_discrete(ptr, now_ms(), WL_POINTER_AXIS_VERTICAL_SCROLL,
						wl_fixed_from_double(15.0 * dir), dir);
				zwlr_virtual_pointer_v1_frame(ptr);
				wl_display_flush(display);
				pause_s(0.05);
			}
		} else if (strncmp(a, "key:", 4) == 0 || strncmp(a, "keydown:", 8) == 0 || strncmp(a, "keyup:", 6) == 0) {
			if (!keyboard_ready(display))
				return 1;
			const char *names = strchr(a, ':') + 1;
			char buf[128];
			snprintf(buf, sizeof buf, "%s", names);
			xkb_keycode_t codes[8];
			int n = 0;
			for (char *tok = strtok(buf, "+"); tok && n < 8; tok = strtok(NULL, "+")) {
				codes[n] = keycode_for(tok);
				if (!codes[n]) { fprintf(stderr, "w95input: no key \"%s\"\n", tok); return 2; }
				++n;
			}
			bool press = a[3] == ':' || a[3] == 'd'; // key: or keydown:
			bool release = a[3] == ':' || a[3] == 'u';
			if (press)
				for (int k = 0; k < n; ++k) { send_key(codes[k], true); wl_display_flush(display); pause_s(0.02); }
			if (release)
				for (int k = n - 1; k >= 0; --k) { send_key(codes[k], false); wl_display_flush(display); pause_s(0.02); }
		} else if (strncmp(a, "type:", 5) == 0) {
			if (!keyboard_ready(display))
				return 1;
			for (const char *c = a + 5; *c; ++c) {
				char one[2] = {*c, 0};
				xkb_keycode_t kc = keycode_for(one);
				if (!kc) { fprintf(stderr, "w95input: can't type \"%c\"\n", *c); return 2; }
				send_key(kc, true);
				wl_display_flush(display);
				pause_s(0.02);
				send_key(kc, false);
				wl_display_flush(display);
				pause_s(0.02);
			}
		} else if (strncmp(a, "wait:", 5) == 0) {
			wl_display_flush(display);
			pause_s(atof(a + 5));
			continue;
		} else {
			fprintf(stderr, "w95input: don't know \"%s\"\n", a);
			return 2;
		}
		wl_display_flush(display);
		pause_s(0.05);
	}
	wl_display_roundtrip(display);
	if (kb)
		zwp_virtual_keyboard_v1_destroy(kb);
	zwlr_virtual_pointer_v1_destroy(ptr);
	wl_display_roundtrip(display);
	wl_display_disconnect(display);
	return 0;
}
