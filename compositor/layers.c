// layer-shell: panels (the shell's desktop, taskbar, notifications). Each
// goes in its layer of the scene; panels with an exclusive zone (the
// taskbar) shrink the area windows use. Keyboard: "exclusive" panels (a Run
// box, the Start menu) take it while they're shown, "on demand" ones when
// clicked.
#include <stdlib.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "server.h"

static struct wlr_scene_tree *tree_for(struct server *server, enum zwlr_layer_shell_v1_layer layer)
{
	switch (layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND: return server->layer_background;
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM: return server->layer_bottom;
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP: return server->layer_top;
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY: default: return server->layer_overlay;
	}
}

// Lay out every panel, top layers first (their exclusive zones go first),
// and work out the usable area for windows. One output for now: the first.
void layers_arrange(struct server *server)
{
	struct wlr_box full;
	wlr_output_layout_get_box(server->output_layout, NULL, &full);
	struct wlr_box usable = full;
	static const enum zwlr_layer_shell_v1_layer order[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
		ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
	};
	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
		struct layer_surface *ls;
		wl_list_for_each(ls, &server->layer_surfaces, link) {
			struct wlr_layer_surface_v1 *l = ls->layer_surface;
			if (l->current.layer != order[i] || !l->initialized)
				continue;
			wlr_scene_layer_surface_v1_configure(ls->scene, &full, &usable);
		}
	}
	bool changed = usable.x != server->usable_area.x || usable.y != server->usable_area.y
			|| usable.width != server->usable_area.width || usable.height != server->usable_area.height;
#ifdef WAYLAND95_TEST_BREAK_EXCLUSIVE // tests/selftest.sh: windows ignore the taskbar's edge
	usable = full;
#endif
	server->usable_area = usable;
	wlr_log(WLR_DEBUG, "usable area %d,%d %dx%d", usable.x, usable.y, usable.width, usable.height);
	if (changed)
		view_arrange_all(server);
}

// keyboard: an exclusive panel takes it, and gives it back when it goes
static void update_keyboard(struct server *server)
{
	struct layer_surface *found = NULL, *ls;
	wl_list_for_each(ls, &server->layer_surfaces, link) {
		struct wlr_layer_surface_v1 *l = ls->layer_surface;
		if (l->surface->mapped
				&& l->current.keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
				&& (l->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP
					|| l->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY))
			found = ls;
	}
	if (found == server->exclusive_layer)
		return;
	server->exclusive_layer = found;
	if (found) {
		input_focus_surface(server, found->layer_surface->surface);
	} else {
		struct view *top = view_top(server);
		if (top)
			view_focus(top);
		else
			input_focus_surface(server, NULL);
	}
}

static void layer_map(struct wl_listener *listener, void *data)
{
	struct layer_surface *ls = wl_container_of(listener, ls, map);
	layers_arrange(ls->server);
	update_keyboard(ls->server);
	input_refresh_pointer(ls->server);
}

static void layer_unmap(struct wl_listener *listener, void *data)
{
	struct layer_surface *ls = wl_container_of(listener, ls, unmap);
	struct server *server = ls->server;
	layers_arrange(server);
	if (server->exclusive_layer == ls)
		server->exclusive_layer = NULL;
	if (server->seat->keyboard_state.focused_surface == ls->layer_surface->surface) {
		struct view *top = view_top(server);
		if (top)
			view_focus(top);
		else
			input_focus_surface(server, NULL);
	}
	update_keyboard(server);
	input_refresh_pointer(server);
}

static void layer_commit(struct wl_listener *listener, void *data)
{
	struct layer_surface *ls = wl_container_of(listener, ls, commit);
	struct wlr_layer_surface_v1 *l = ls->layer_surface;
	struct server *server = ls->server;
	// moved to another layer
	struct wlr_scene_tree *want = tree_for(server, l->current.layer);
	if (ls->scene->tree->node.parent != want)
		wlr_scene_node_reparent(&ls->scene->tree->node, want);
	// its first commit is answered with a configure (the arrangement);
	// later ones re-arrange when anything that matters changed
	if (l->initial_commit || l->current.committed)
		layers_arrange(server);
	if (l->surface->mapped)
		update_keyboard(server);
}

static void layer_destroy(struct wl_listener *listener, void *data)
{
	struct layer_surface *ls = wl_container_of(listener, ls, destroy);
	struct server *server = ls->server;
	wl_list_remove(&ls->map.link);
	wl_list_remove(&ls->unmap.link);
	wl_list_remove(&ls->commit.link);
	wl_list_remove(&ls->destroy.link);
	wl_list_remove(&ls->link);
	if (server->exclusive_layer == ls)
		server->exclusive_layer = NULL;
	free(ls);
	layers_arrange(server);
}

static void new_layer_surface(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *l = data;
	if (!l->output) {
		// the program left it to us: the first output
		struct output *first = wl_list_empty(&server->outputs) ? NULL
				: wl_container_of(server->outputs.next, first, link);
		if (!first) {
			wlr_layer_surface_v1_destroy(l);
			return;
		}
		l->output = first->wlr_output;
	}
	struct layer_surface *ls = calloc(1, sizeof(*ls));
	ls->tag.kind = NODE_LAYER;
	ls->server = server;
	ls->layer_surface = l;
	ls->scene = wlr_scene_layer_surface_v1_create(tree_for(server, l->pending.layer), l);
	ls->scene->tree->node.data = &ls->tag;
	l->data = ls;

	ls->map.notify = layer_map;
	wl_signal_add(&l->surface->events.map, &ls->map);
	ls->unmap.notify = layer_unmap;
	wl_signal_add(&l->surface->events.unmap, &ls->unmap);
	ls->commit.notify = layer_commit;
	wl_signal_add(&l->surface->events.commit, &ls->commit);
	ls->destroy.notify = layer_destroy;
	wl_signal_add(&l->events.destroy, &ls->destroy);
	wl_list_insert(&server->layer_surfaces, &ls->link);
}

void layers_init(struct server *server)
{
	wl_list_init(&server->layer_surfaces);
	server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
	server->new_layer_surface.notify = new_layer_surface;
	wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);
}
