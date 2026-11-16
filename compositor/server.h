// wayland95-compositor: the shared state of the compositor.
// See docs/architecture.md for the design.
#pragma once

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <xkbcommon/xkbcommon.h>

struct wlr_xwayland;
struct wlr_xwayland_surface;

enum cursor_mode {
	CURSOR_PASSTHROUGH, // events go to the surface under the pointer
	CURSOR_MOVE,        // dragging a window
	CURSOR_RESIZE,      // resizing a window from an edge
};

// What a scene node belongs to: its node.data points at one of these
// (the first member of view, layer_surface, unmanaged), so a surface under
// the pointer can be traced to what owns it.
enum node_kind { NODE_VIEW, NODE_LAYER, NODE_UNMANAGED, NODE_LOCK };
struct node_tag {
	enum node_kind kind;
};

struct server {
	struct wl_display *display;
	struct wl_event_loop *event_loop;
	struct wlr_backend *backend;
	struct wlr_session *session; // NULL nested and headless
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_compositor *compositor;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_output_layout *output_layout;

	// the scene's layers, bottom to top
	struct wlr_scene_tree *layer_background; // layer-shell background (wallpaper, icons)
	struct wlr_scene_tree *layer_bottom;     // layer-shell bottom
	struct wlr_scene_tree *layer_windows;    // windows, with their popups
	struct wlr_scene_tree *layer_top;        // layer-shell top (the taskbar)
	struct wlr_scene_tree *layer_overlay;    // layer-shell overlay (notifications)
	struct wlr_scene_tree *layer_unmanaged;  // X11 menus and tooltips
	struct wlr_scene_tree *drag_icons;       // what's being dragged
	struct wlr_scene_tree *layer_lock;       // the lock screen, above everything

	// where windows may go: the output minus panels' exclusive zones
	struct wlr_box usable_area;

	struct wl_list outputs; // struct output.link
	struct wl_listener new_output;
	struct wl_listener layout_change;
	struct wlr_output_manager_v1 *output_manager;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_listener new_xdg_decoration;
	struct wl_list views; // struct view.link, front (top) first

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct wl_list layer_surfaces; // struct layer_surface.link
	struct layer_surface *exclusive_layer; // has the keyboard exclusively

	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_ready;
	struct wl_listener new_xwayland_surface;

	struct wlr_foreign_toplevel_manager_v1 *foreign_toplevels;
	struct wlr_ext_foreign_toplevel_list_v1 *ext_toplevels;

	struct wlr_seat *seat;
	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
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
	struct wl_listener swipe_begin, swipe_update, swipe_end;
	struct wl_listener pinch_begin, pinch_update, pinch_end;
	struct wl_listener hold_begin, hold_end;
	struct wl_listener request_set_cursor;
	struct wl_listener request_set_shape;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;
	struct wl_listener request_start_drag;
	struct wl_listener start_drag;

	struct wlr_relative_pointer_manager_v1 *relative_pointers;
	struct wlr_pointer_gestures_v1 *gestures;
	struct wlr_pointer_constraints_v1 *constraints;
	struct wl_listener new_constraint;
	struct wlr_pointer_constraint_v1 *active_constraint;
	struct wl_listener active_constraint_destroy;

	struct wlr_idle_notifier_v1 *idle_notifier;
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit;
	struct wl_listener new_idle_inhibitor;

	// the lock screen (ext-session-lock)
	struct wlr_session_lock_manager_v1 *lock_manager;
	struct wl_listener new_lock;
	struct wlr_session_lock_v1 *lock;
	bool locked; // stays true if the locker dies without unlocking
	struct wlr_scene_rect *lock_cover;
	struct wl_listener lock_new_surface;
	struct wl_listener lock_unlock;
	struct wl_listener lock_destroy;

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

enum view_type { VIEW_XDG, VIEW_XWAYLAND };

// one window: an xdg toplevel or an X11 window
struct view {
	struct node_tag tag; // NODE_VIEW
	struct wl_list link;
	struct server *server;
	enum view_type type;
	struct wlr_xdg_toplevel *xdg_toplevel;   // VIEW_XDG
	struct wlr_xwayland_surface *xsurface;   // VIEW_XWAYLAND
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wlr_scene_tree *scene_tree;
	bool mapped, placed, minimized, maximized, fullscreen;
	int x, y;           // layout position of the window geometry's corner
	struct wlr_box saved; // size and place before maximize/fullscreen

	struct wlr_foreign_toplevel_handle_v1 *ftl;
	struct wlr_ext_foreign_toplevel_handle_v1 *ext_ftl;
	struct wl_listener ftl_activate, ftl_close, ftl_minimize, ftl_maximize, ftl_fullscreen;

	struct wl_listener map, unmap, commit, destroy;
	struct wl_listener request_move, request_resize, request_maximize;
	struct wl_listener request_minimize, request_fullscreen;
	struct wl_listener set_title, set_app_id;
	// X11 only
	struct wl_listener associate, dissociate, request_configure, request_activate;
	struct wl_listener decoration_mode, decoration_destroy;
};

// an X11 menu, tooltip or other override-redirect window
struct unmanaged {
	struct node_tag tag; // NODE_UNMANAGED
	struct server *server;
	struct wlr_xwayland_surface *xsurface;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener associate, dissociate, map, unmap, destroy;
	struct wl_listener request_configure, set_geometry;
};

struct layer_surface {
	struct node_tag tag; // NODE_LAYER
	struct wl_list link;
	struct server *server;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene;
	struct wl_listener map, unmap, commit, destroy, new_popup;
};

struct popup {
	struct server *server;
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener map, unmap;
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
void input_refresh_pointer(struct server *server); // re-pick the surface under the pointer
void input_focus_surface(struct server *server, struct wlr_surface *surface); // keyboard

// view.c
struct view *view_from_surface_at(struct server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy, struct node_tag **owner);
void view_init_common(struct view *view, struct server *server, enum view_type type);
struct wlr_surface *view_surface(struct view *view);
void view_geometry(struct view *view, struct wlr_box *box); // size, and offset in the surface
void view_move(struct view *view, int x, int y);
void view_resize(struct view *view, int width, int height);
void view_focus(struct view *view);
void view_close(struct view *view);
void view_set_minimized(struct view *view, bool minimized);
void view_set_maximized(struct view *view, bool maximized);
void view_set_fullscreen(struct view *view, bool fullscreen);
void view_mapped(struct view *view);   // after the type-specific map
void view_unmapped(struct view *view); // before the type-specific unmap
void view_update_title(struct view *view);
void view_begin_interactive(struct view *view, enum cursor_mode mode, uint32_t edges);
struct view *view_top(struct server *server); // topmost mapped, not minimized
void view_arrange_all(struct server *server); // usable area changed

// xdg.c
void xdg_init(struct server *server);

// xwayland.c
void xwayland_init(struct server *server);

// layers.c
void layers_init(struct server *server);
void layers_arrange(struct server *server);

// protocols.c: everything else (idle, activation, lock, outputs, ...)
void protocols_init(struct server *server);
