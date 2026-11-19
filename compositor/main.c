// wayland95-compositor: Wayland 95's compositor, on wlroots.
//
//   wayland95-compositor [--socket NAME] [--headless WIDTHxHEIGHT]... [--run COMMAND]
//
// Without --headless it picks its backend the wlroots way: DRM/KMS on a
// console or in the login session, a window when started inside another
// Wayland or X11 desktop. --headless draws into memory with no screen at
// all (the automated tests, tests/run.sh). It prints the socket programs
// connect to, as "wayland95-compositor: WAYLAND_DISPLAY=NAME", once ready.
#define _POSIX_C_SOURCE 200809L
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wlr/backend/headless.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "server.h"

// The GL the renderer got, when it's drawn by the CPU (Mesa's llvmpipe or
// softpipe: a VM without 3D, "nomodeset", a GPU Mesa has no driver for);
// NULL for a real GPU. On the screen, GL drawn that way isn't finished when
// the frame is shown: parts of the screen showed an earlier frame (a
// closed window's taskbar button, the pointer a step behind).
static const char *software_gl(struct wlr_renderer *renderer)
{
	if (!wlr_renderer_is_gles2(renderer))
		return NULL;
	struct wlr_egl *egl = wlr_gles2_renderer_get_egl(renderer);
	EGLDisplay display = wlr_egl_get_display(egl);
	EGLContext previous = eglGetCurrentContext();
	if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, wlr_egl_get_context(egl)))
		return NULL;
	const char *name = (const char *)glGetString(GL_RENDERER);
	static char found[128];
	const char *result = NULL;
	if (name && (strstr(name, "llvmpipe") || strstr(name, "softpipe") || strstr(name, "Software Rasterizer"))) {
		snprintf(found, sizeof found, "%s", name);
		result = found;
	}
	eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, previous);
	return result;
}

static void usage(const char *argv0)
{
	fprintf(stderr, "usage: %s [--socket NAME] [--headless WIDTHxHEIGHT]... [--shell PROGRAM] [--session] [--run COMMAND]\n", argv0);
}

static void stop_listening(struct server *s)
{
	struct wl_listener *listeners[] = {
		&s->new_output, &s->new_input, &s->new_layer_surface,
		&s->new_xdg_toplevel, &s->new_xdg_popup, &s->new_xdg_decoration,
		&s->cursor_motion, &s->cursor_motion_absolute, &s->cursor_button, &s->cursor_axis,
		&s->cursor_frame, &s->swipe_begin, &s->swipe_update, &s->swipe_end, &s->pinch_begin,
		&s->pinch_update, &s->pinch_end, &s->hold_begin, &s->hold_end,
		&s->new_virtual_pointer, &s->new_virtual_keyboard, &s->new_constraint,
		&s->request_set_cursor, &s->request_set_selection, &s->request_set_primary_selection,
		&s->request_start_drag, &s->start_drag, &s->request_set_shape,
	};
	for (size_t i = 0; i < sizeof listeners / sizeof listeners[0]; ++i)
		wl_list_remove(&listeners[i]->link);
	if (s->active_constraint)
		wl_list_remove(&s->active_constraint_destroy.link);
	if (s->lock) { // a lock screen still up
		wl_list_remove(&s->lock_new_surface.link);
		wl_list_remove(&s->lock_unlock.link);
		wl_list_remove(&s->lock_destroy.link);
	}
	protocols_finish(s);
	shell_finish(s);
	a11y_finish(s);
	displays_finish(s);
	if (s->settings_source)
		wl_event_source_remove(s->settings_source);
}

static int stop(int signal, void *data)
{
	struct server *server = data;
	wlr_log(WLR_INFO, "signal %d: stopping", signal);
	server->stopping = true; // (the shell isn't started again)
	wl_display_terminate(server->display);
	return 0;
}

int main(int argc, char *argv[])
{
	const char *socket_name = NULL;
	const char *run = NULL;
	const char *shell = NULL;  // --shell PROGRAM: started, and restarted if it dies
	bool session = false;      // --session: the login session (the shell's too)
	// --headless may be given more than once: that many displays
	int headless_w = 0, headless_h = 0;
	int headless_n = 0, headless_sizes[8][2];
	bool debug = getenv("WAYLAND95_DEBUG") != NULL;

	static const struct option options[] = {
		{"socket", required_argument, NULL, 's'},
		{"headless", required_argument, NULL, 'H'},
		{"run", required_argument, NULL, 'r'},
		{"shell", required_argument, NULL, 'S'},
		{"session", no_argument, NULL, 'E'},
		{"help", no_argument, NULL, 'h'},
		{0, 0, 0, 0},
	};
	int c;
	while ((c = getopt_long(argc, argv, "s:H:r:S:Eh", options, NULL)) != -1) {
		switch (c) {
		case 's': socket_name = optarg; break;
		case 'r': run = optarg; break;
		case 'S': shell = optarg; break;
		case 'E': session = true; break;
		case 'H':
			if (sscanf(optarg, "%dx%d", &headless_w, &headless_h) != 2 || headless_w <= 0 || headless_h <= 0
					|| headless_n >= 8) {
				usage(argv[0]);
				return 1;
			}
			headless_sizes[headless_n][0] = headless_w;
			headless_sizes[headless_n][1] = headless_h;
			headless_n++;
			break;
		default:
			usage(argv[0]);
			return c == 'h' ? 0 : 1;
		}
	}

	wlr_log_init(debug ? WLR_DEBUG : WLR_INFO, NULL);
	// (SIGCHLD stays default: wlroots waits for Xwayland's start-up fork)

	struct server server = {0};
	server.display = wl_display_create();
	server.event_loop = wl_display_get_event_loop(server.display);

	if (headless_w > 0) {
		// no GPU needed: draw with pixman into memory
		setenv("WLR_RENDERER", "pixman", 0);
		server.backend = wlr_headless_backend_create(server.event_loop);
	} else {
		server.backend = wlr_backend_autocreate(server.event_loop, &server.session);
	}
	if (!server.backend) {
		wlr_log(WLR_ERROR, "no backend (no screen, no seat access, and no desktop to run inside)");
		return 1;
	}

	server.renderer = wlr_renderer_autocreate(server.backend);
	if (!server.renderer) {
		wlr_log(WLR_ERROR, "no renderer");
		return 1;
	}
	// no real GPU: draw with pixman (the CPU, finished before it's shown),
	// unless WLR_RENDERER asked for something
	const char *software = getenv("WLR_RENDERER") ? NULL : software_gl(server.renderer);
	if (software) {
		wlr_log(WLR_INFO, "GL is drawn by the CPU here (%s): drawing with pixman instead", software);
		wlr_renderer_destroy(server.renderer);
		setenv("WLR_RENDERER", "pixman", true);
		server.renderer = wlr_renderer_autocreate(server.backend);
		unsetenv("WLR_RENDERER"); // (not for the programs started from here)
		if (!server.renderer) {
			wlr_log(WLR_ERROR, "no renderer");
			return 1;
		}
	}
	// wl_shm, and linux-dmabuf when the renderer can import GPU buffers
	wlr_renderer_init_wl_display(server.renderer, server.display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (!server.allocator) {
		wlr_log(WLR_ERROR, "no allocator");
		return 1;
	}

	// the core and buffer protocols
	server.compositor = wlr_compositor_create(server.display, 6, server.renderer);
	wlr_subcompositor_create(server.display);
	wlr_data_device_manager_create(server.display);
	wlr_primary_selection_v1_device_manager_create(server.display);
	wlr_data_control_manager_v1_create(server.display);
	wlr_viewporter_create(server.display);
	wlr_single_pixel_buffer_manager_v1_create(server.display);
	wlr_fractional_scale_manager_v1_create(server.display, 1);
	wlr_presentation_create(server.display, server.backend);

	server.output_layout = wlr_output_layout_create(server.display);
	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);
	// bottom to top (server.h)
	server.layer_background = wlr_scene_tree_create(&server.scene->tree);
	server.layer_bottom = wlr_scene_tree_create(&server.scene->tree);
	server.layer_windows = wlr_scene_tree_create(&server.scene->tree);
	server.layer_snap = wlr_scene_tree_create(&server.scene->tree);
	server.layer_top = wlr_scene_tree_create(&server.scene->tree);
	server.layer_fullscreen = wlr_scene_tree_create(&server.scene->tree);
	server.layer_overlay = wlr_scene_tree_create(&server.scene->tree);
	server.layer_unmanaged = wlr_scene_tree_create(&server.scene->tree);
	server.drag_icons = wlr_scene_tree_create(&server.scene->tree);
	server.layer_lock = wlr_scene_tree_create(&server.scene->tree);

	// screenshots and screen recording (grim, the tests, screen sharing)
	wlr_screencopy_manager_v1_create(server.display);
	wlr_export_dmabuf_manager_v1_create(server.display);
	wlr_xdg_output_manager_v1_create(server.display, server.output_layout);

	// the window list for other programs (the taskbar)
	server.foreign_toplevels = wlr_foreign_toplevel_manager_v1_create(server.display);
	server.ext_toplevels = wlr_ext_foreign_toplevel_list_v1_create(server.display, 1);

	settings_init(&server); // before the keyboards and window frames need it
	output_init(&server);
	xdg_init(&server);
	layers_init(&server);
	input_init(&server);
	protocols_init(&server);
	dialog_init(&server);
	shell_init(&server);
	xwayland_init(&server);


	const char *socket = NULL;
	if (socket_name) {
		if (wl_display_add_socket(server.display, socket_name) == 0)
			socket = socket_name;
	} else {
		socket = wl_display_add_socket_auto(server.display);
	}
	if (!socket) {
		wlr_log(WLR_ERROR, "can't open the Wayland socket %s", socket_name ? socket_name : "(auto)");
		wlr_backend_destroy(server.backend);
		return 1;
	}

	snprintf(server.socket_name, sizeof server.socket_name, "%s", socket);
	if (!wlr_backend_start(server.backend)) {
		wlr_log(WLR_ERROR, "can't start the backend");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}

	// (once it's started: each is announced as it's added, in this order)
	for (int i = 0; i < headless_n; ++i)
		wlr_headless_add_output(server.backend, headless_sizes[i][0], headless_sizes[i][1]);

	setenv("WAYLAND_DISPLAY", socket, true);
	// the shell ends the desktop (Start > Shut Down > exit) by asking this
	// process to stop; SIGTERM and SIGINT stop it cleanly
	char pid[16];
	snprintf(pid, sizeof pid, "%d", (int)getpid());
	setenv("WAYLAND95_COMPOSITOR_PID", pid, true);
	wl_event_loop_add_signal(server.event_loop, SIGTERM, stop, &server);
	wl_event_loop_add_signal(server.event_loop, SIGINT, stop, &server);
	// DISPLAY is Xwayland's (xwayland.c), started when an X11 program connects
	fprintf(stderr, "wayland95-compositor: WAYLAND_DISPLAY=%s DISPLAY=%s\n", socket,
			getenv("DISPLAY") ? getenv("DISPLAY") : "");
	fflush(stderr);

	// the shell (taskbar, desktop...), started and kept going by us
	server.shell_program = shell;
	server.shell_session = session;
	shell_launch(&server);

	if (run) {
		// double fork: the program belongs to init, so it leaves no zombie
		pid_t child = fork();
		if (child == 0) {
			if (fork() == 0) {
				execl("/bin/sh", "/bin/sh", "-c", run, (char *)NULL);
				_exit(127);
			}
			_exit(0);
		}
		if (child > 0)
			waitpid(child, NULL, 0);
	}

	wl_display_run(server.display);

	// shutting down: programs first (their windows go), then everything
	// that listens to wlroots' objects stops (wlroots checks), then those
	server.stopping = true;
	wl_display_destroy_clients(server.display);
	if (server.xwayland) {
		wl_list_remove(&server.xwayland_ready.link);
		wl_list_remove(&server.new_xwayland_surface.link);
		wlr_xwayland_destroy(server.xwayland);
	}
	stop_listening(&server);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.display);
	return 0;
}
