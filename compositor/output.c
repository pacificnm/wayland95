// Outputs (screens): each new one is turned on at its preferred mode,
// placed in the layout, and drawn from the scene graph every frame.
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "server.h"

static void output_frame(struct wl_listener *listener, void *data)
{
	struct output *output = wl_container_of(listener, output, frame);
	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(output->server->scene, output->wlr_output);
	if (!scene_output)
		return;
	wlr_scene_output_commit(scene_output, NULL);
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

// the backend asks for a new state: nested, the host window was resized
static void output_request_state(struct wl_listener *listener, void *data)
{
	struct output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
	outputs_changed(output->server);
}

// ------------------------------------------------------------ which display

bool output_active(struct output *output)
{
	// (in the layout: one the screen saver has put to sleep still counts,
	// or everything on it would move away each time)
	return output && wlr_output_layout_get(output->server->output_layout, output->wlr_output);
}

struct output *output_at(struct server *server, double lx, double ly)
{
	struct wlr_output *o = wlr_output_layout_output_at(server->output_layout, lx, ly);
	if (o && o->data)
		return o->data;
	return server->primary;
}

struct output *output_for_view(struct view *view)
{
	struct wlr_box geo;
	view_geometry(view, &geo);
	return output_at(view->server, view->x + geo.width / 2.0, view->y + geo.height / 2.0);
}

// In the desktop's layout (at x,y, or wherever there's room), or out of it.
// The scene draws what's in the layout.
void output_place(struct output *output, bool in_layout, bool has_position, int x, int y)
{
	struct server *server = output->server;
	struct wlr_output *o = output->wlr_output;
	bool was = wlr_output_layout_get(server->output_layout, o) != NULL;
	if (!in_layout) {
		if (was)
			wlr_output_layout_remove(server->output_layout, o);
		return;
	}
	struct wlr_output_layout_output *l_output = has_position
			? wlr_output_layout_add(server->output_layout, o, x, y)
			: wlr_output_layout_add_auto(server->output_layout, o);
	if (!was && l_output) {
		struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(server->scene, o);
		if (!scene_output)
			scene_output = wlr_scene_output_create(server->scene, o);
		wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
	}
}

// Displays came, went, moved or changed size: the main one is still one
// that's there, panels and windows are on displays that are, and whoever
// shows the displays hears.
void outputs_changed(struct server *server)
{
	if (server->displays_applying)
		return; // (once, when all of them are set)
	// the main display: as chosen, else the first there is
	if (!output_active(server->primary)) {
		server->primary = NULL;
		struct output *output;
		wl_list_for_each_reverse(output, &server->outputs, link) {
			if (output_active(output)) {
				server->primary = output;
				break;
			}
		}
	}
	// panels of a display that went move to the main one (and stay: the
	// taskbar doesn't vanish with a monitor that's switched off)
	struct layer_surface *ls;
	wl_list_for_each(ls, &server->layer_surfaces, link)
		if (!output_active(ls->output))
			ls->output = server->primary;
	layers_arrange(server);
	// windows left outside every display come to the main one
	if (server->primary) {
		struct view *view;
		int n = 0;
		wl_list_for_each(view, &server->views, link) {
			if (!view->mapped || view->maximized || view->fullscreen)
				continue;
			struct wlr_box geo;
			view_geometry(view, &geo);
			if (wlr_output_layout_output_at(server->output_layout, view->x + geo.width / 2.0, view->y + 10))
				continue;
			int left, top, right, bottom;
			view_frame_insets(view, &left, &top, &right, &bottom);
			const struct wlr_box *area = &server->primary->usable;
			view_move(view, area->x + 40 + n * 30 + left, area->y + 30 + n * 30 + top);
			n = (n + 1) % 8;
		}
	}
	view_arrange_all(server);
	lock_cover_update(server);
	// frames as fine as the finest display
	int scale = displays_frame_scale(server);
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->frame_scale != scale) {
			view->frame_w = 0; // forces a redraw
			frame_update(view);
		}
	}
	xscale_update(server); // (X11's screen follows the displays)
	displays_write_state(server);
	shell_send_primary(server);
	input_refresh_pointer(server);
}

static void output_destroy(struct wl_listener *listener, void *data)
{
	struct output *output = wl_container_of(listener, output, destroy);
	struct server *server = output->server;
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	output->wlr_output->data = NULL;
	if (server->primary == output)
		server->primary = NULL;
	struct layer_surface *ls;
	wl_list_for_each(ls, &server->layer_surfaces, link)
		if (ls->output == output)
			ls->output = NULL;
	free(output);
	if (!server->stopping)
		outputs_changed(server);
}

static void new_output(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
		wlr_log(WLR_ERROR, "can't draw on output %s", wlr_output->name);
		return;
	}

	struct output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;
	wlr_output->data = output;
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	wl_list_insert(&server->outputs, &output->link);

	// on, at its saved settings (Control Panel > Display) or its own best
	// mode, and into the layout
	displays_apply(server);
	wlr_log(WLR_INFO, "output %s: %dx%d", wlr_output->name, wlr_output->width, wlr_output->height);
}

void output_init(struct server *server)
{
	wl_list_init(&server->outputs);
	server->new_output.notify = new_output;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);
}
