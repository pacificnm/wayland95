// w95input: drives the pointer of a wlroots compositor for the tests, through
// the virtual-pointer protocol (the same path as a real mouse inside the
// compositor). Keyboard input is wtype's job.
//
//   w95input ACTION...
//
//   move:X,Y            move to X,Y (layout pixels)
//   down:BUTTON         press left, right or middle
//   up:BUTTON           release it
//   click:X,Y[,BUTTON]  move there, press, release (default left)
//   wheel:N             turn the wheel N notches (negative: up, positive: down)
//   wait:SECONDS        pause (default after every action: 0.05 s)
//
// e.g. w95input click:200,300 wait:0.5 move:250,320 wheel:3
#define _POSIX_C_SOURCE 200809L
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct zwlr_virtual_pointer_manager_v1 *manager;
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
			zwlr_virtual_pointer_v1_motion_absolute(ptr, now_ms(), x, y, out_w, out_h);
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
			zwlr_virtual_pointer_v1_motion_absolute(ptr, now_ms(), x, y, out_w, out_h);
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
	zwlr_virtual_pointer_v1_destroy(ptr);
	wl_display_roundtrip(display);
	wl_display_disconnect(display);
	return 0;
}
