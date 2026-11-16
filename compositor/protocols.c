// The rest of the standard protocols: idle and idle-inhibit, activation,
// window list for other programs (ext-foreign-toplevel-list is created in
// main.c), output configuration, display power and gamma (night light), the
// lock screen, and those wlroots handles entirely (foreign, security
// context, tearing, content type, alpha modifier, shortcut inhibiting).
#include <stdlib.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_content_type_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/util/log.h>

#include "server.h"

// ------------------------------------------------------- idle, inhibitors

struct inhibitor {
	struct server *server;
	struct wl_listener destroy;
};

static void update_inhibited(void *data)
{
	struct server *server = data;
	wlr_idle_notifier_v1_set_inhibited(server->idle_notifier,
			!wl_list_empty(&server->idle_inhibit->inhibitors));
}

static void inhibitor_destroy(struct wl_listener *listener, void *data)
{
	struct inhibitor *inhibitor = wl_container_of(listener, inhibitor, destroy);
	struct server *server = inhibitor->server;
	wl_list_remove(&inhibitor->destroy.link);
	free(inhibitor);
	// it leaves the manager's list after this signal
	wl_event_loop_add_idle(server->event_loop, update_inhibited, server);
}

static void new_idle_inhibitor(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_idle_inhibitor);
	struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
	struct inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
	inhibitor->server = server;
	inhibitor->destroy.notify = inhibitor_destroy;
	wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);
	update_inhibited(server);
}

// ------------------------------------------------------------- activation

// a program raising one of its windows (opening a link in a running
// browser, a notification's button)
static void request_activate(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	struct wlr_xdg_toplevel *toplevel = wlr_xdg_toplevel_try_from_wlr_surface(event->surface);
	if (!toplevel || !toplevel->base->data)
		return;
	struct wlr_scene_tree *tree = toplevel->base->data;
	struct view *view = (struct view *)tree->node.data;
	if (view && view->mapped)
		view_focus(view);
}
static struct wl_listener activate_listener = { .notify = request_activate };

// -------------------------------------------------- output configuration

// what the outputs are now, for programs that show or change it
static void publish_outputs(struct server *server)
{
	struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
	struct output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_output_configuration_head_v1 *head =
				wlr_output_configuration_head_v1_create(config, output->wlr_output);
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
		head->state.enabled = output->wlr_output->enabled;
		head->state.x = box.x;
		head->state.y = box.y;
	}
	wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

static void layout_change(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, layout_change);
	publish_outputs(server);
	layers_arrange(server);
}

static bool apply_config(struct server *server, struct wlr_output_configuration_v1 *config, bool test_only)
{
	bool ok = true;
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_head_v1_state_apply(&head->state, &state);
		if (test_only) {
			ok = ok && wlr_output_test_state(head->state.output, &state);
		} else {
			ok = ok && wlr_output_commit_state(head->state.output, &state);
			if (head->state.enabled)
				wlr_output_layout_add(server->output_layout, head->state.output, head->state.x, head->state.y);
			else
				wlr_output_layout_remove(server->output_layout, head->state.output);
		}
		wlr_output_state_finish(&state);
	}
	return ok;
}

static void output_manager_apply(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, output_manager_apply);
	struct wlr_output_configuration_v1 *config = data;
	if (apply_config(server, config, false))
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);
	publish_outputs(server);
}

static void output_manager_test(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, output_manager_test);
	struct wlr_output_configuration_v1 *config = data;
	if (apply_config(server, config, true))
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);
}

// display on/off (the screen saver turning the screen off)
static void output_power_set_mode(struct wl_listener *listener, void *data)
{
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
	if (!wlr_output_commit_state(event->output, &state))
		wlr_log(WLR_ERROR, "couldn't turn output %s %s", event->output->name,
				event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON ? "on" : "off");
	wlr_output_state_finish(&state);
}
static struct wl_listener output_power_listener = { .notify = output_power_set_mode };

// colour ramps (night light, colour calibration)
static void set_gamma(struct wl_listener *listener, void *data)
{
	struct wlr_gamma_control_manager_v1_set_gamma_event *event = data;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	if (!wlr_gamma_control_v1_apply(event->control, &state) || !wlr_output_commit_state(event->output, &state))
		wlr_gamma_control_v1_send_failed_and_destroy(event->control);
	wlr_output_state_finish(&state);
}
static struct wl_listener gamma_listener = { .notify = set_gamma };

// ------------------------------------------------------------ lock screen

// While locked: a black cover over everything, the locker's surfaces above
// it, and input only to them. If the locker dies without unlocking, the
// screen stays locked (the cover stays) until a new locker unlocks it.

struct lock_surface {
	struct node_tag tag; // NODE_LOCK
	struct server *server;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_scene_tree *tree;
	struct wl_listener map, destroy;
};

static void lock_surface_map(struct wl_listener *listener, void *data)
{
	struct lock_surface *ls = wl_container_of(listener, ls, map);
	input_focus_surface(ls->server, ls->lock_surface->surface);
	input_refresh_pointer(ls->server);
}

static void lock_surface_destroy(struct wl_listener *listener, void *data)
{
	struct lock_surface *ls = wl_container_of(listener, ls, destroy);
	wl_list_remove(&ls->map.link);
	wl_list_remove(&ls->destroy.link);
	free(ls);
}

static void lock_new_surface(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, lock_new_surface);
	struct wlr_session_lock_surface_v1 *surface = data;
	struct lock_surface *ls = calloc(1, sizeof(*ls));
	ls->tag.kind = NODE_LOCK;
	ls->server = server;
	ls->lock_surface = surface;
	ls->tree = wlr_scene_subsurface_tree_create(server->layer_lock, surface->surface);
	ls->tree->node.data = &ls->tag;
	struct wlr_box box;
	wlr_output_layout_get_box(server->output_layout, surface->output, &box);
	wlr_scene_node_set_position(&ls->tree->node, box.x, box.y);
	wlr_session_lock_surface_v1_configure(surface, box.width, box.height);
	ls->map.notify = lock_surface_map;
	wl_signal_add(&surface->surface->events.map, &ls->map);
	ls->destroy.notify = lock_surface_destroy;
	wl_signal_add(&surface->events.destroy, &ls->destroy);
}

static void lock_finish(struct server *server)
{
	wl_list_remove(&server->lock_new_surface.link);
	wl_list_remove(&server->lock_unlock.link);
	wl_list_remove(&server->lock_destroy.link);
	server->lock = NULL;
}

static void lock_unlock(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, lock_unlock);
	lock_finish(server);
	server->locked = false;
	wlr_scene_node_set_enabled(&server->lock_cover->node, false);
	struct view *top = view_top(server);
	if (top)
		view_focus(top);
	input_refresh_pointer(server);
	wlr_log(WLR_INFO, "unlocked");
}

static void lock_destroy(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, lock_destroy);
	// the locker went away without unlocking: stay locked
	lock_finish(server);
	wlr_log(WLR_ERROR, "the screen locker quit without unlocking; staying locked");
}

static void new_lock(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, new_lock);
	struct wlr_session_lock_v1 *lock = data;
	if (server->lock) {
		wlr_session_lock_v1_destroy(lock); // one locker at a time
		return;
	}
	server->lock = lock;
	server->locked = true;
	struct wlr_box all;
	wlr_output_layout_get_box(server->output_layout, NULL, &all);
	wlr_scene_node_set_position(&server->lock_cover->node, all.x, all.y);
	wlr_scene_rect_set_size(server->lock_cover, all.width, all.height);
	wlr_scene_node_set_enabled(&server->lock_cover->node, true);
	wlr_scene_node_raise_to_top(&server->lock_cover->node);
	input_focus_surface(server, NULL);
	server->lock_new_surface.notify = lock_new_surface;
	wl_signal_add(&lock->events.new_surface, &server->lock_new_surface);
	server->lock_unlock.notify = lock_unlock;
	wl_signal_add(&lock->events.unlock, &server->lock_unlock);
	server->lock_destroy.notify = lock_destroy;
	wl_signal_add(&lock->events.destroy, &server->lock_destroy);
	// everything is covered from now on
	wlr_session_lock_v1_send_locked(lock);
	wlr_log(WLR_INFO, "locked");
}

// ------------------------------------------------------------------- init

void protocols_init(struct server *server)
{
	server->idle_notifier = wlr_idle_notifier_v1_create(server->display);
	server->idle_inhibit = wlr_idle_inhibit_v1_create(server->display);
	server->new_idle_inhibitor.notify = new_idle_inhibitor;
	wl_signal_add(&server->idle_inhibit->events.new_inhibitor, &server->new_idle_inhibitor);

	struct wlr_xdg_activation_v1 *activation = wlr_xdg_activation_v1_create(server->display);
	wl_signal_add(&activation->events.request_activate, &activate_listener);

	struct wlr_xdg_foreign_registry *registry = wlr_xdg_foreign_registry_create(server->display);
	wlr_xdg_foreign_v1_create(server->display, registry);
	wlr_xdg_foreign_v2_create(server->display, registry);
	wlr_security_context_manager_v1_create(server->display);
	wlr_tearing_control_manager_v1_create(server->display, 1);
	wlr_content_type_manager_v1_create(server->display, 1);
	wlr_alpha_modifier_v1_create(server->display);
	wlr_keyboard_shortcuts_inhibit_v1_create(server->display);

	server->output_manager = wlr_output_manager_v1_create(server->display);
	server->output_manager_apply.notify = output_manager_apply;
	wl_signal_add(&server->output_manager->events.apply, &server->output_manager_apply);
	server->output_manager_test.notify = output_manager_test;
	wl_signal_add(&server->output_manager->events.test, &server->output_manager_test);
	server->layout_change.notify = layout_change;
	wl_signal_add(&server->output_layout->events.change, &server->layout_change);

	struct wlr_output_power_manager_v1 *power = wlr_output_power_manager_v1_create(server->display);
	wl_signal_add(&power->events.set_mode, &output_power_listener);
	struct wlr_gamma_control_manager_v1 *gamma = wlr_gamma_control_manager_v1_create(server->display);
	wl_signal_add(&gamma->events.set_gamma, &gamma_listener);

	server->lock_manager = wlr_session_lock_manager_v1_create(server->display);
	server->new_lock.notify = new_lock;
	wl_signal_add(&server->lock_manager->events.new_lock, &server->new_lock);
	static const float black[4] = {0, 0, 0, 1};
	server->lock_cover = wlr_scene_rect_create(server->layer_lock, 0, 0, black);
	wlr_scene_node_set_enabled(&server->lock_cover->node, false);
}
