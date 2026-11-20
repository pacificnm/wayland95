// X11 programs drawn at the displays' real pixels on scaled displays (as
// KDE's "apply scaling themselves"). Without this, Xwayland's programs draw
// at scale 1 and are enlarged on a display at 125% or 200%: blurry.
//
// With it (Control Panel > Display > Settings, "Sharp older programs";
// settings.ini Display/SharpX11, on unless turned off) X11 is told its
// screen is S times the desktop's size, S being the largest display scale,
// and that it's 96*S DPI (Xft.dpi), so programs that follow the DPI (Qt,
// Chromium and Electron, most toolkits) draw at the display's own pixels;
// the compositor shows their windows S times smaller, pixel for pixel. Ones
// that don't follow the DPI (old ones, Wine, Java) come out smaller but
// still sharp. X11 coordinates are the desktop's times S throughout:
// windows' places and sizes, menus, the pointer.
//
// Xwayland learns the screen from xdg-output: it gets one of its own (the
// desktop's numbers times S), and only it does; wlroots' stays everyone
// else's (the global filter in shell.c asks xscale_global_visible).
#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include <xcb/xcb.h>

#include "server.h"
#include "xdg-output-unstable-v1-protocol.h"

// --------------------------------------------------------- coordinates

int xscale_to_x(struct server *server, double v)
{
	return (int)lround(v * server->xscale);
}

int xscale_from_x(struct server *server, double v)
{
	return (int)lround(v / server->xscale);
}

// Sharp needs pixel for pixel: an X11 window's size on the desktop times S
// must be its size in X11 exactly (else it's stretched by a pixel, which
// softens all of it). At 125% that's sizes in multiples of 4 (5 X11
// pixels); at 150% of 2; at 200% any.
static int exact_step(double scale)
{
	for (int n = 1; n <= 16; ++n)
		if (fabs(n * scale - lround(n * scale)) < 1e-6)
			return n;
	return 1;
}

int xscale_snap(struct server *server, int size)
{
	int n = server->xscale_step;
	if (n <= 1 || size < n)
		return size;
	return size - size % n;
}

// a point in a surface, in the desktop's units (from the scene) -> in the
// surface's own (X11 surfaces': times S)
void xscale_surface_point(struct server *server, struct wlr_surface *surface, double *sx, double *sy)
{
	if (server->xscale != 1.0 && surface && wlr_xwayland_surface_try_from_wlr_surface(surface)) {
		*sx *= server->xscale;
		*sy *= server->xscale;
	}
}

// ------------------------------------------- showing X11 windows smaller

static void scale_buffer(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	struct server *server = data;
	struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
	if (!ss)
		return;
	int w = ss->surface->current.width, h = ss->surface->current.height;
	if (w <= 0 || h <= 0)
		return;
	wlr_scene_buffer_set_dest_size(buffer, xscale_from_x(server, w), xscale_from_x(server, h));
}

// after an X11 surface commits (the scene helper has just set its size to
// the surface's: this runs after it), or when S changes
void xscale_apply(struct server *server, struct wlr_scene_tree *tree)
{
	if (tree)
		wlr_scene_node_for_each_buffer(&tree->node, scale_buffer, server);
}

// ------------------------------------------------ Xwayland's xdg-output

struct xscale_output {
	struct wl_list link; // server->xdg_outputs
	struct server *server;
	struct wl_resource *resource;        // zxdg_output_v1
	struct wl_resource *output_resource; // its wl_output (NULL once gone)
	struct wl_listener output_destroy;
};

static void send_output(struct xscale_output *o)
{
	if (!o->output_resource)
		return;
	struct wlr_output *wo = wlr_output_from_resource(o->output_resource);
	if (!wo)
		return;
	struct server *server = o->server;
	struct wlr_box box;
	wlr_output_layout_get_box(server->output_layout, wo, &box);
	if (wlr_box_empty(&box))
		return; // (turned off: not in the desktop)
	zxdg_output_v1_send_logical_position(o->resource, xscale_to_x(server, box.x), xscale_to_x(server, box.y));
	zxdg_output_v1_send_logical_size(o->resource, xscale_to_x(server, box.width), xscale_to_x(server, box.height));
	if (wl_resource_get_version(o->resource) >= 3) {
		if (wl_resource_get_version(o->output_resource) >= WL_OUTPUT_DONE_SINCE_VERSION)
			wl_output_send_done(o->output_resource);
	} else {
		zxdg_output_v1_send_done(o->resource);
	}
}

static void output_resource_gone(struct wl_listener *listener, void *data)
{
	struct xscale_output *o = wl_container_of(listener, o, output_destroy);
	wl_list_remove(&o->output_destroy.link);
	wl_list_init(&o->output_destroy.link);
	o->output_resource = NULL;
}

static void xdg_output_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zxdg_output_v1_interface xdg_output_impl = {
	.destroy = xdg_output_destroy_request,
};

static void xdg_output_resource_destroyed(struct wl_resource *resource)
{
	struct xscale_output *o = wl_resource_get_user_data(resource);
	if (!o)
		return;
	wl_list_remove(&o->link);
	wl_list_remove(&o->output_destroy.link);
	free(o);
}

static void manager_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void manager_get_xdg_output(struct wl_client *client, struct wl_resource *manager,
		uint32_t id, struct wl_resource *output_resource)
{
	struct server *server = wl_resource_get_user_data(manager);
	int version = wl_resource_get_version(manager);
	struct wl_resource *resource = wl_resource_create(client, &zxdg_output_v1_interface, version, id);
	struct xscale_output *o = calloc(1, sizeof(*o));
	if (!resource || !o) {
		free(o);
		wl_client_post_no_memory(client);
		return;
	}
	o->server = server;
	o->resource = resource;
	o->output_resource = output_resource;
	o->output_destroy.notify = output_resource_gone;
	wl_resource_add_destroy_listener(output_resource, &o->output_destroy);
	wl_resource_set_implementation(resource, &xdg_output_impl, o, xdg_output_resource_destroyed);
	wl_list_insert(&server->xdg_outputs, &o->link);
	struct wlr_output *wo = wlr_output_from_resource(output_resource);
	if (wo && version >= 2) {
		zxdg_output_v1_send_name(resource, wo->name);
		if (wo->description)
			zxdg_output_v1_send_description(resource, wo->description);
	}
	send_output(o);
}

static const struct zxdg_output_manager_v1_interface manager_impl = {
	.destroy = manager_destroy,
	.get_xdg_output = manager_get_xdg_output,
};

static void manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client, &zxdg_output_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

static const struct wl_client *xwayland_client(struct server *server)
{
	return server->xwayland && server->xwayland->server ? server->xwayland->server->client : NULL;
}

bool xscale_global_visible(struct server *server, const struct wl_client *client, const struct wl_global *global)
{
	const struct wl_client *xw = xwayland_client(server);
	if (global == server->xdg_output_global)
		return xw && client == xw;
	if (server->xdg_output_manager && global == server->xdg_output_manager->global)
		return !xw || client != xw;
	return true;
}

// --------------------------------------------------- X11's DPI (Xft.dpi)

// Into the X server's resources (RESOURCE_MANAGER, what xrdb sets), from a
// process of its own: the compositor mustn't wait on Xwayland, which may be
// waiting on it. Programs read them as they connect, so on a scaled display
// this starts Xwayland (it's started when something connects) before any
// program does: the first one gets the DPI too.
static void set_x_resources(struct server *server)
{
	if (!server->xwayland)
		return;
	if (server->xscale == 1.0 && !server->xwayland->xwm)
		return; // (not running, and nothing to tell it)
	const char *display = server->xwayland->display_name;
	int dpi = (int)lround(96.0 * server->xscale);
	pid_t child = fork();
	if (child < 0)
		return;
	if (child > 0) {
		waitpid(child, NULL, 0);
		return;
	}
	// (the grandchild does it, so nothing's left to reap)
	if (fork() != 0)
		_exit(0);
	// (none of the compositor's files: the GPU, input devices, clients)
	close_range(3, ~0U, 0);
	alarm(15); // (Xwayland that never comes: don't wait for ever)
	xcb_connection_t *c = xcb_connect(display, NULL);
	if (!c || xcb_connection_has_error(c))
		_exit(1);
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
	xcb_intern_atom_reply_t *rm = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, 16, "RESOURCE_MANAGER"), NULL);
	if (!screen || !rm)
		_exit(1);
	// keep what's there, but these two
	char *old = NULL;
	xcb_get_property_reply_t *prop = xcb_get_property_reply(c,
			xcb_get_property(c, 0, screen->root, rm->atom, XCB_ATOM_STRING, 0, 1 << 16), NULL);
	if (prop && xcb_get_property_value_length(prop) > 0)
		old = strndup(xcb_get_property_value(prop), xcb_get_property_value_length(prop));
	size_t size = (old ? strlen(old) : 0) + 128;
	char *out = calloc(1, size);
	if (old) {
		for (char *line = strtok(old, "\n"); line; line = strtok(NULL, "\n")) {
			if (strncmp(line, "Xft.dpi:", 8) != 0 && strncmp(line, "Xcursor.size:", 13) != 0) {
				strcat(out, line);
				strcat(out, "\n");
			}
		}
	}
	// (the pointer the same size as the desktop's own)
	snprintf(out + strlen(out), size - strlen(out), "Xft.dpi:\t%d\nXcursor.size:\t24\n", dpi);
	xcb_change_property(c, XCB_PROP_MODE_REPLACE, screen->root, rm->atom, XCB_ATOM_STRING, 8,
			strlen(out), out);
	xcb_flush(c);
	xcb_disconnect(c);
	_exit(0);
}

// --------------------------------------------------------------- S

static double wanted_scale(struct server *server)
{
	if (!server->settings.sharp_x11)
		return 1.0;
	double scale = 1.0;
	struct output *output;
	wl_list_for_each(output, &server->outputs, link)
		if (output->wlr_output->enabled && output->wlr_output->scale > scale)
			scale = output->wlr_output->scale;
	return scale;
}

void xscale_xwayland_ready(struct server *server)
{
	set_x_resources(server);
}

// the displays or the setting changed: S again, and if it's different,
// tell Xwayland, and put X11 windows back where they were on the desktop
void xscale_update(struct server *server)
{
	double scale = wanted_scale(server);
	double old = server->xscale;
	if (scale != old) {
		wlr_log(WLR_INFO, "X11 programs: %s (X11 at %.2fx the desktop)",
				scale == 1.0 ? "scaled by the desktop" : "drawn at the displays' own pixels", scale);
		server->xscale = scale;
		server->xscale_step = exact_step(scale);
	}
	struct xscale_output *o;
	wl_list_for_each(o, &server->xdg_outputs, link)
		send_output(o);
	if (scale == old)
		return;
	set_x_resources(server);
	// X11 windows keep their place and size on the desktop; X11's numbers
	// for them change
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->type != VIEW_XWAYLAND || !view->mapped)
			continue;
		int w = (int)lround(view->xsurface->width / old), h = (int)lround(view->xsurface->height / old);
		xscale_apply(server, view->content);
		view_move(view, view->x, view->y);
		view_resize(view, w, h);
		frame_update(view);
	}
}

void xscale_init(struct server *server)
{
	server->xscale = 1.0;
	server->xscale_step = 1;
	wl_list_init(&server->xdg_outputs);
	server->xdg_output_global = wl_global_create(server->display, &zxdg_output_manager_v1_interface, 3,
			server, manager_bind);
}
