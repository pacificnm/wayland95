// w95query: the window list for the tests, through the foreign-toplevel
// protocol (what the shell's taskbar uses):
//
//   w95query list                       one line per window: TITLE|APP_ID|STATES
//   w95query ACTION TITLE               ACTION: activate close minimize unminimize
//                                       maximize unmaximize fullscreen unfullscreen
//
// Exit status 1 if no window has that title.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

struct toplevel {
	struct zwlr_foreign_toplevel_handle_v1 *handle;
	char *title, *app_id;
	char states[64];
	struct toplevel *next;
};

static struct zwlr_foreign_toplevel_manager_v1 *manager;
static struct wl_seat *seat;
static struct toplevel *toplevels;

static void h_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const char *title)
{
	struct toplevel *t = data;
	free(t->title);
	t->title = strdup(title);
}
static void h_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const char *app_id)
{
	struct toplevel *t = data;
	free(t->app_id);
	t->app_id = strdup(app_id);
}
static void h_output_enter(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o) {}
static void h_output_leave(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_output *o) {}
static void h_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_array *state)
{
	struct toplevel *t = data;
	t->states[0] = 0;
	uint32_t *s;
	wl_array_for_each(s, state) {
		const char *name = *s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED ? "maximized"
			: *s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED ? "minimized"
			: *s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED ? "activated"
			: *s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN ? "fullscreen" : "other";
		if (t->states[0])
			strncat(t->states, ",", sizeof(t->states) - strlen(t->states) - 1);
		strncat(t->states, name, sizeof(t->states) - strlen(t->states) - 1);
	}
}
static void h_done(void *d, struct zwlr_foreign_toplevel_handle_v1 *h) {}
static void h_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *h)
{
	struct toplevel *t = data;
	t->handle = NULL;
}
static void h_parent(void *d, struct zwlr_foreign_toplevel_handle_v1 *h, struct zwlr_foreign_toplevel_handle_v1 *p) {}
static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
	.title = h_title, .app_id = h_app_id, .output_enter = h_output_enter, .output_leave = h_output_leave,
	.state = h_state, .done = h_done, .closed = h_closed, .parent = h_parent,
};

static void m_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *m, struct zwlr_foreign_toplevel_handle_v1 *h)
{
	struct toplevel *t = calloc(1, sizeof(*t));
	t->handle = h;
	t->next = toplevels;
	toplevels = t;
	zwlr_foreign_toplevel_handle_v1_add_listener(h, &handle_listener, t);
}
static void m_finished(void *d, struct zwlr_foreign_toplevel_manager_v1 *m) {}
static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
	.toplevel = m_toplevel, .finished = m_finished,
};

static void global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
	if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
		manager = wl_registry_bind(r, name, &zwlr_foreign_toplevel_manager_v1_interface, version < 3 ? version : 3);
		zwlr_foreign_toplevel_manager_v1_add_listener(manager, &manager_listener, NULL);
	} else if (strcmp(iface, wl_seat_interface.name) == 0 && !seat) {
		seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
	}
}
static void global_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { .global = global, .global_remove = global_remove };

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s list | ACTION TITLE\n", argv[0]);
		return 2;
	}
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "w95query: can't connect\n");
		return 2;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!manager) {
		fprintf(stderr, "w95query: no foreign-toplevel manager\n");
		return 2;
	}
	wl_display_roundtrip(display);
	wl_display_roundtrip(display);

	if (strcmp(argv[1], "list") == 0) {
		for (struct toplevel *t = toplevels; t; t = t->next)
			if (t->handle)
				printf("%s|%s|%s\n", t->title ? t->title : "", t->app_id ? t->app_id : "", t->states);
		return 0;
	}
	if (argc < 3) {
		fprintf(stderr, "usage: %s ACTION TITLE\n", argv[0]);
		return 2;
	}
	struct toplevel *found = NULL;
	for (struct toplevel *t = toplevels; t; t = t->next)
		if (t->handle && t->title && strcmp(t->title, argv[2]) == 0)
			found = t;
	if (!found) {
		fprintf(stderr, "w95query: no window titled \"%s\"\n", argv[2]);
		return 1;
	}
	struct zwlr_foreign_toplevel_handle_v1 *h = found->handle;
	const char *a = argv[1];
	if (strcmp(a, "activate") == 0) zwlr_foreign_toplevel_handle_v1_activate(h, seat);
	else if (strcmp(a, "close") == 0) zwlr_foreign_toplevel_handle_v1_close(h);
	else if (strcmp(a, "minimize") == 0) zwlr_foreign_toplevel_handle_v1_set_minimized(h);
	else if (strcmp(a, "unminimize") == 0) zwlr_foreign_toplevel_handle_v1_unset_minimized(h);
	else if (strcmp(a, "maximize") == 0) zwlr_foreign_toplevel_handle_v1_set_maximized(h);
	else if (strcmp(a, "unmaximize") == 0) zwlr_foreign_toplevel_handle_v1_unset_maximized(h);
	else if (strcmp(a, "fullscreen") == 0) zwlr_foreign_toplevel_handle_v1_set_fullscreen(h, NULL);
	else if (strcmp(a, "unfullscreen") == 0) zwlr_foreign_toplevel_handle_v1_unset_fullscreen(h);
	else {
		fprintf(stderr, "w95query: unknown action %s\n", a);
		return 2;
	}
	wl_display_roundtrip(display);
	wl_display_disconnect(display);
	return 0;
}
