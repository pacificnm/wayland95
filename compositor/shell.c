// wayland95-shell-v1 (protocols/wayland95/): the compositor's link to its
// shell, win95shell. The desktop's own keys (volume, brightness, power, sleep) are
// caught in input.c and sent here, to the one bound shell.
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland.h>
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

// the window menu's choice
static void shell_window_action(struct wl_client *client, struct wl_resource *resource, uint32_t id, uint32_t action)
{
	struct server *server = wl_resource_get_user_data(resource);
	struct view *view = server ? view_by_id(server, id) : NULL;
	if (!view || !view->mapped)
		return;
	switch (action) {
	case WAYLAND95_SHELL_V1_WINDOW_ACTION_RESTORE:
		if (view->minimized)
			view_set_minimized(view, false);
		else if (view->maximized)
			view_set_maximized(view, false);
		view_focus(view);
		break;
	case WAYLAND95_SHELL_V1_WINDOW_ACTION_MINIMIZE:
		view_set_minimized(view, true);
		break;
	case WAYLAND95_SHELL_V1_WINDOW_ACTION_MAXIMIZE:
		if (view->minimized)
			view_set_minimized(view, false);
		view_set_maximized(view, true);
		view_focus(view);
		break;
	case WAYLAND95_SHELL_V1_WINDOW_ACTION_CLOSE:
		view_close(view);
		break;
	case WAYLAND95_SHELL_V1_WINDOW_ACTION_MOVE:
		input_begin_keyboard_grab(view, CURSOR_MOVE);
		break;
	case WAYLAND95_SHELL_V1_WINDOW_ACTION_SIZE:
		input_begin_keyboard_grab(view, CURSOR_RESIZE);
		break;
	}
}

// End Task: the program behind a taskbar button, killed
static void shell_end_task(struct wl_client *client, struct wl_resource *resource, struct wl_resource *toplevel)
{
	struct server *server = wl_resource_get_user_data(resource);
	// (the handle's user data is wlroots' handle; none once it's gone)
	struct wlr_foreign_toplevel_handle_v1 *handle = toplevel ? wl_resource_get_user_data(toplevel) : NULL;
	if (!server || !handle)
		return;
	struct view *view = NULL, *v;
	wl_list_for_each(v, &server->views, link)
		if (v->ftl == handle)
			view = v;
	if (!view)
		return;
	pid_t pid = 0;
	if (view->type == VIEW_XDG && view->xdg_toplevel)
		wl_client_get_credentials(view->xdg_toplevel->base->client->client, &pid, NULL, NULL);
	else if (view->type == VIEW_XWAYLAND && view->xsurface)
		pid = view->xsurface->pid;
	// never the desktop itself
	pid_t shell_pid = 0;
	wl_client_get_credentials(client, &shell_pid, NULL, NULL);
	pid_t xwayland_pid = server->xwayland && server->xwayland->server ? server->xwayland->server->pid : 0;
	if (pid <= 1 || pid == getpid() || pid == shell_pid || pid == xwayland_pid) {
		wlr_log(WLR_INFO, "End Task: no process to end for that window (pid %d); asking it to close", (int)pid);
		view_close(view);
		return;
	}
	wlr_log(WLR_INFO, "End Task: ending process %d", (int)pid);
	kill(pid, SIGKILL);
}

static const struct wayland95_shell_v1_interface shell_impl = {
	.window_action = shell_window_action,
	.end_task = shell_end_task,
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
	shell_send_primary(server);
	wlr_log(WLR_INFO, "a shell bound wayland95_shell_v1");
}

// only the shell the compositor started may see wayland95_shell_v1 (with
// no --shell, as in the tests, any client may: the first to bind has it)
static bool global_filter(const struct wl_client *client, const struct wl_global *global, void *data)
{
	struct server *server = data;
	if (global != server->shell_global || !server->shell_program)
		return true;
	return client == server->shell_client;
}

void shell_init(struct server *server)
{
	server->shell_global = wl_global_create(server->display, &wayland95_shell_v1_interface, 1, server, shell_bind);
	wl_display_set_global_filter(server->display, global_filter, server);
}

// ------------------------------------------------- starting the shell

static bool spawn_shell(struct server *server, bool restarted);

static void reap_shell(struct server *server)
{
	if (server->shell_pid <= 0)
		return;
	int status;
	pid_t r = waitpid(server->shell_pid, &status, WNOHANG);
	if (r == 0) { // connection gone but still running: it's stuck
		kill(server->shell_pid, SIGKILL);
		r = waitpid(server->shell_pid, &status, 0);
	}
	if (r == server->shell_pid) {
		if (WIFSIGNALED(status))
			wlr_log(WLR_ERROR, "the shell (pid %d) was ended by signal %d", (int)server->shell_pid, WTERMSIG(status));
		else if (WIFEXITED(status))
			wlr_log(WLR_INFO, "the shell (pid %d) exited with status %d", (int)server->shell_pid, WEXITSTATUS(status));
	}
	server->shell_pid = 0;
}

static int restart_shell(void *data)
{
	struct server *server = data;
	reap_shell(server);
	if (server->stopping)
		return 0;
	// five restarts within a minute: something's wrong with it; give up
	// (the desktop goes on without a taskbar rather than flicker forever)
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	int now = (int)ts.tv_sec;
	if (server->shell_restarts[0] && now - server->shell_restarts[0] < 60) {
		wlr_log(WLR_ERROR, "the shell keeps going away (five times in a minute): not starting it again");
		return 0;
	}
	memmove(server->shell_restarts, server->shell_restarts + 1, sizeof server->shell_restarts - sizeof server->shell_restarts[0]);
	server->shell_restarts[4] = now;
	spawn_shell(server, true);
	return 0;
}

static void shell_client_destroyed(struct wl_listener *listener, void *data)
{
	struct server *server = wl_container_of(listener, server, shell_client_destroy);
	wl_list_remove(&listener->link);
	server->shell_client = NULL;
	if (server->stopping)
		return;
	wlr_log(WLR_ERROR, "the shell (pid %d) went away: starting it again in a second", (int)server->shell_pid);
	wl_event_source_timer_update(server->shell_restart_timer, 1000);
}

static bool spawn_shell(struct server *server, bool restarted)
{
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
		wlr_log_errno(WLR_ERROR, "can't make the shell's connection");
		return false;
	}
	pid_t pid = fork();
	if (pid < 0) {
		wlr_log_errno(WLR_ERROR, "can't start the shell");
		close(sv[0]);
		close(sv[1]);
		return false;
	}
	if (pid == 0) {
		// the shell's end of the connection, kept across exec (dup drops
		// CLOEXEC); libwayland takes it from WAYLAND_SOCKET
		int fd = dup(sv[1]);
		char value[16];
		snprintf(value, sizeof value, "%d", fd);
		setenv("WAYLAND_SOCKET", value, 1);
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		char *argv[4];
		int n = 0;
		argv[n++] = (char *)server->shell_program;
		if (server->shell_session)
			argv[n++] = "--desktop-session"; // (not --session: Qt takes that, and the next argument, for itself)
		if (restarted)
			argv[n++] = "--restarted";
		argv[n] = NULL;
		execvp(server->shell_program, argv);
		fprintf(stderr, "wayland95-compositor: can't run the shell %s: %s\n", server->shell_program, strerror(errno));
		_exit(127);
	}
	close(sv[1]);
	server->shell_pid = pid;
	server->shell_client = wl_client_create(server->display, sv[0]);
	if (!server->shell_client) {
		wlr_log(WLR_ERROR, "can't connect the shell");
		close(sv[0]);
		return false;
	}
	server->shell_client_destroy.notify = shell_client_destroyed;
	wl_client_add_destroy_listener(server->shell_client, &server->shell_client_destroy);
	wlr_log(WLR_INFO, "started the shell %s (pid %d)%s", server->shell_program, (int)pid, restarted ? ", again" : "");
	return true;
}

void shell_launch(struct server *server)
{
	if (!server->shell_program)
		return;
	server->shell_restart_timer = wl_event_loop_add_timer(server->event_loop, restart_shell, server);
	spawn_shell(server, false);
}

void shell_finish(struct server *server)
{
	if (server->shell_restart_timer) {
		wl_event_source_remove(server->shell_restart_timer);
		server->shell_restart_timer = NULL;
	}
	if (server->shell_client) // (gone by now: the clients went first)
		wl_list_remove(&server->shell_client_destroy.link);
	server->shell_client = NULL;
	reap_shell(server);
	if (server->shell_resource) // (the shell's client is gone by now)
		wl_resource_set_user_data(server->shell_resource, NULL);
	server->shell_resource = NULL;
	if (server->shell_global)
		wl_global_destroy(server->shell_global);
	server->shell_global = NULL;
}

// which display is the main one (the shell puts the taskbar there)
void shell_send_primary(struct server *server)
{
	if (server->shell_resource && server->primary)
		wayland95_shell_v1_send_primary_output(server->shell_resource, server->primary->wlr_output->name);
}

uint32_t shell_key_for(xkb_keysym_t sym, uint32_t modifiers)
{
	// Ctrl+Esc: the Start menu; Ctrl+Shift+Esc: the System Monitor
	if (sym == XKB_KEY_Escape) {
		const uint32_t mods = modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO);
		if (mods == WLR_MODIFIER_CTRL)
			return WAYLAND95_SHELL_V1_KEY_START_MENU;
		if (mods == (WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT))
			return WAYLAND95_SHELL_V1_KEY_TASK_MANAGER;
		return 0;
	}
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

void shell_send_tone(struct server *server, uint32_t tone)
{
	if (server->shell_resource)
		wayland95_shell_v1_send_tone(server->shell_resource, tone);
}

void shell_send_shortcut(struct server *server, uint32_t feature)
{
	if (server->shell_resource)
		wayland95_shell_v1_send_shortcut(server->shell_resource, feature);
}

void shell_send_sticky(struct server *server, const char *held)
{
	if (server->shell_resource)
		wayland95_shell_v1_send_sticky_modifiers(server->shell_resource, held);
}

bool shell_switcher_show(struct server *server, struct view **views, int n, int selected)
{
	if (!server->shell_resource)
		return false;
	for (int i = 0; i < n; ++i) {
		const char *app_id = "";
		if (views[i]->type == VIEW_XDG && views[i]->xdg_toplevel && views[i]->xdg_toplevel->app_id)
			app_id = views[i]->xdg_toplevel->app_id;
		else if (views[i]->type == VIEW_XWAYLAND && views[i]->xsurface && views[i]->xsurface->class)
			app_id = views[i]->xsurface->class;
		wayland95_shell_v1_send_switcher_entry(server->shell_resource, view_title(views[i]), app_id,
				views[i]->minimized);
	}
	wayland95_shell_v1_send_switcher_show(server->shell_resource, (uint32_t)selected);
	return true;
}

void shell_switcher_select(struct server *server, int selected)
{
	if (server->shell_resource)
		wayland95_shell_v1_send_switcher_select(server->shell_resource, (uint32_t)selected);
}

void shell_switcher_hide(struct server *server)
{
	if (server->shell_resource)
		wayland95_shell_v1_send_switcher_hide(server->shell_resource);
}

bool shell_window_menu(struct view *view, int x, int y)
{
	struct server *server = view->server;
	if (!server->shell_resource)
		return false;
	uint32_t state = (view->maximized ? WAYLAND95_SHELL_V1_WINDOW_STATE_MAXIMIZED : 0)
			| (view->minimized ? WAYLAND95_SHELL_V1_WINDOW_STATE_MINIMIZED : 0);
	wayland95_shell_v1_send_window_menu(server->shell_resource, view->id, x, y, state);
	return true;
}
