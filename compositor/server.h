// wayland95-compositor: the shared state of the compositor.
// See docs/architecture.md for the design.
#pragma once

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <xkbcommon/xkbcommon.h>

enum cursor_mode {
	CURSOR_PASSTHROUGH, // events go to the window under the pointer
	CURSOR_MOVE,        // dragging a window
	CURSOR_RESIZE,      // resizing a window from an edge
};

struct server {
	struct wl_display *display;
	struct wl_event_loop *event_loop;
	struct wlr_backend *backend;
	struct wlr_session *session; // NULL nested and headless
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_output_layout *output_layout;

	// the scene's layers, bottom to top
	struct wlr_scene_tree *layer_windows;
	struct wlr_scene_tree *layer_popups; // reserved: popups stay with their windows

	struct wl_list outputs; // struct output.link
	struct wl_listener new_output;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list views; // struct view.link, front (top) first

	struct wlr_seat *seat;
	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	// The seat's keyboard: physical keyboards join this group, which
	// always exists with the default keymap, so programs get a keymap
	// before any key. A virtual keyboard (wtype, on-screen keyboards) brings
	// its own keymap and is the seat's keyboard while it types.
	struct wlr_keyboard_group *kbd_group;
	struct keyboard *group_kbd;
	struct wl_list keyboards; // virtual keyboards: struct keyboard.link
	struct wl_listener new_input;
	struct wl_listener new_virtual_pointer;
	struct wl_listener new_virtual_keyboard;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;
	struct wl_listener request_set_cursor;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;

	// while a button is held, the pointer stays with the surface the press
	// went to (an implicit grab): its layout position at the press
	struct wlr_surface *press_surface;
	double press_ox, press_oy;

	// an interactive move or resize in progress
	enum cursor_mode cursor_mode;
	struct view *grabbed_view;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;
};

struct output {
	struct wl_list link;
	struct server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

// one window (an xdg toplevel; X11 windows come in phase 2)
struct view {
	struct wl_list link;
	struct server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	bool placed; // given a position on its first map
};

struct popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct keyboard {
	struct wl_list link;
	struct server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

// output.c
void output_init(struct server *server);

// input.c
void input_init(struct server *server);
void input_reset_cursor_mode(struct server *server);

// xdg.c
void xdg_init(struct server *server);
struct view *view_at(struct server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);
void view_focus(struct view *view, struct wlr_surface *surface);
void view_begin_interactive(struct view *view, enum cursor_mode mode, uint32_t edges);
