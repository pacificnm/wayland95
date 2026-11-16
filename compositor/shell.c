// wayland95-shell-v1 (protocols/wayland95/): the compositor's link to its
// shell, win95shell. The desktop's own keys (volume, brightness, power, sleep) are
// caught in input.c and sent here, to the one bound shell.
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

#include "server.h"
#include "wayland95-shell-v1-protocol.h"

static void shell_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wayland95_window_list_v1_interface window_list_impl = {
	.destroy = shell_destroy_request,
};

// the windows on screen, topmost first, frames included
static void shell_list_windows(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
	struct server *server = wl_resource_get_user_data(resource);
	struct wl_resource *list = wl_resource_create(client, &wayland95_window_list_v1_interface,
			wl_resource_get_version(resource), id);
	if (!list) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(list, &window_list_impl, NULL, NULL);
	if (server) {
		struct view *view;
		wl_list_for_each(view, &server->views, link) {
			if (!view->mapped || view->minimized)
				continue;
			struct wlr_box geo;
			view_geometry(view, &geo);
			int left, top, right, bottom;
			view_frame_insets(view, &left, &top, &right, &bottom);
			wayland95_window_list_v1_send_window(list, view->x - left, view->y - top,
					geo.width + left + right, geo.height + top + bottom);
		}
	}
	wayland95_window_list_v1_send_done(list);
}

static const struct wayland95_shell_v1_interface shell_impl = {
	.destroy = shell_destroy_request,
	.list_windows = shell_list_windows,
};

static void shell_resource_destroyed(struct wl_resource *resource)
{
	struct server *server = wl_resource_get_user_data(resource);
	if (server && server->shell_resource == resource) {
		server->shell_resource = NULL;
		wlr_log(WLR_INFO, "the shell let go of wayland95_shell_v1");
	}
}

static void shell_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct server *server = data;
	struct wl_resource *resource = wl_resource_create(client, &wayland95_shell_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	if (server->shell_resource) {
		wl_resource_set_implementation(resource, &shell_impl, NULL, shell_resource_destroyed);
		wl_resource_post_error(resource, WAYLAND95_SHELL_V1_ERROR_ALREADY_BOUND,
				"another shell has wayland95_shell_v1");
		return;
	}
	wl_resource_set_implementation(resource, &shell_impl, server, shell_resource_destroyed);
	server->shell_resource = resource;
	wlr_log(WLR_INFO, "a shell bound wayland95_shell_v1");
}

void shell_init(struct server *server)
{
	server->shell_global = wl_global_create(server->display, &wayland95_shell_v1_interface, 1, server, shell_bind);
}

void shell_finish(struct server *server)
{
	if (server->shell_resource) // (the shell's client is gone by now)
		wl_resource_set_user_data(server->shell_resource, NULL);
	server->shell_resource = NULL;
	if (server->shell_global)
		wl_global_destroy(server->shell_global);
	server->shell_global = NULL;
}

uint32_t shell_key_for(xkb_keysym_t sym, uint32_t modifiers)
{
	// Ctrl+Shift+5 (with Shift, the key may say %)
	const uint32_t ctrl_shift = WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT;
	if ((modifiers & (ctrl_shift | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)) == ctrl_shift
			&& (sym == XKB_KEY_5 || sym == XKB_KEY_percent))
		return WAYLAND95_SHELL_V1_KEY_CAPTURE;
	if (sym == XKB_KEY_Print)
		return (modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL)) ? 0 : WAYLAND95_SHELL_V1_KEY_PRINT;
	switch (sym) {
	case XKB_KEY_XF86AudioRaiseVolume: return WAYLAND95_SHELL_V1_KEY_VOLUME_UP;
	case XKB_KEY_XF86AudioLowerVolume: return WAYLAND95_SHELL_V1_KEY_VOLUME_DOWN;
	case XKB_KEY_XF86AudioMute: return WAYLAND95_SHELL_V1_KEY_VOLUME_MUTE;
	case XKB_KEY_XF86MonBrightnessUp: return WAYLAND95_SHELL_V1_KEY_BRIGHTNESS_UP;
	case XKB_KEY_XF86MonBrightnessDown: return WAYLAND95_SHELL_V1_KEY_BRIGHTNESS_DOWN;
	case XKB_KEY_XF86PowerOff: return WAYLAND95_SHELL_V1_KEY_POWER;
	case XKB_KEY_XF86Sleep: case XKB_KEY_XF86Suspend: return WAYLAND95_SHELL_V1_KEY_SLEEP;
	case XKB_KEY_XF86Hibernate: return WAYLAND95_SHELL_V1_KEY_HIBERNATE;
	default: return 0;
	}
}

bool shell_send_key(struct server *server, uint32_t key, bool pressed)
{
#ifdef WAYLAND95_TEST_BREAK_SHELLKEYS // tests/selftest.sh: the shell never gets its keys
	return false;
#endif
	if (!server->shell_resource || key == 0)
		return false;
	wayland95_shell_v1_send_key(server->shell_resource, key,
			pressed ? WAYLAND95_SHELL_V1_KEY_STATE_PRESSED : WAYLAND95_SHELL_V1_KEY_STATE_RELEASED);
	return true;
}
