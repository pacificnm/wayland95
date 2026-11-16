// wayland95-compositor: Wayland 95's compositor, on wlroots.
//
//   wayland95-compositor [--socket NAME] [--headless WIDTHxHEIGHT] [--run COMMAND]
//
// Without --headless it picks its backend the wlroots way: DRM/KMS on a
// console or in the login session, a window when started inside another
// Wayland or X11 desktop. --headless draws into memory with no screen at
// all (the automated tests, tests/run.sh). It prints the socket programs
// connect to, as "wayland95-compositor: WAYLAND_DISPLAY=NAME", once ready.
#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/backend/headless.h>
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

#include "server.h"

static void usage(const char *argv0)
{
	fprintf(stderr, "usage: %s [--socket NAME] [--headless WIDTHxHEIGHT] [--run COMMAND]\n", argv0);
}

int main(int argc, char *argv[])
{
	const char *socket_name = NULL;
	const char *run = NULL;
	int headless_w = 0, headless_h = 0;
	bool debug = getenv("WAYLAND95_DEBUG") != NULL;

	static const struct option options[] = {
		{"socket", required_argument, NULL, 's'},
		{"headless", required_argument, NULL, 'H'},
		{"run", required_argument, NULL, 'r'},
		{"help", no_argument, NULL, 'h'},
		{0, 0, 0, 0},
	};
	int c;
	while ((c = getopt_long(argc, argv, "s:H:r:h", options, NULL)) != -1) {
		switch (c) {
		case 's': socket_name = optarg; break;
		case 'r': run = optarg; break;
		case 'H':
			if (sscanf(optarg, "%dx%d", &headless_w, &headless_h) != 2 || headless_w <= 0 || headless_h <= 0) {
				usage(argv[0]);
				return 1;
			}
			break;
		default:
			usage(argv[0]);
			return c == 'h' ? 0 : 1;
		}
	}

	wlr_log_init(debug ? WLR_DEBUG : WLR_INFO, NULL);
	// programs started with --run are reaped automatically
	signal(SIGCHLD, SIG_IGN);

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
	// wl_shm, and linux-dmabuf when the renderer can import GPU buffers
	wlr_renderer_init_wl_display(server.renderer, server.display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (!server.allocator) {
		wlr_log(WLR_ERROR, "no allocator");
		return 1;
	}

	// the core and buffer protocols
	wlr_compositor_create(server.display, 6, server.renderer);
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
	server.layer_windows = wlr_scene_tree_create(&server.scene->tree);
	server.layer_popups = wlr_scene_tree_create(&server.scene->tree);

	// screenshots and screen recording (grim, the tests, screen sharing)
	wlr_screencopy_manager_v1_create(server.display);
	wlr_export_dmabuf_manager_v1_create(server.display);
	wlr_xdg_output_manager_v1_create(server.display, server.output_layout);

	output_init(&server);
	xdg_init(&server);
	input_init(&server);

	if (headless_w > 0)
		wlr_headless_add_output(server.backend, headless_w, headless_h);

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

	if (!wlr_backend_start(server.backend)) {
		wlr_log(WLR_ERROR, "can't start the backend");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	unsetenv("DISPLAY"); // X11 programs get Xwayland's (phase 2)
	fprintf(stderr, "wayland95-compositor: WAYLAND_DISPLAY=%s\n", socket);
	fflush(stderr);

	if (run && fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", run, (char *)NULL);
		_exit(127);
	}

	wl_display_run(server.display);

	wl_display_destroy_clients(server.display);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.display);
	return 0;
}
