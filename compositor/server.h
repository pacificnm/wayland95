// wayland95-compositor: the shared state of the compositor.
// See docs/architecture.md for the design.
#pragma once

#include <stdbool.h>
#include <sys/types.h>
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
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <xkbcommon/xkbcommon.h>

struct wlr_xwayland;
struct wlr_xwayland_surface;

// The Win95 window frame (decoration.c): a 3-pixel border (2-pixel bevel
// and 1 of face) and a 22-pixel title bar, as the Qt compositor drew them.
#define FRAME_BORDER 3
#define FRAME_TITLE 22
#define FRAME_TOP (FRAME_BORDER + FRAME_TITLE)
#define FRAME_RESIZE_MARGIN 4 // invisible grab zone outside the border

enum frame_part {
	PART_NONE, PART_TITLE, PART_MINIMIZE, PART_MAXIMIZE, PART_CLOSE, PART_EDGE,
};

// ~/.config/wayland95/settings.ini, as the Control Panel writes it
// (settings.c); re-read when it changes
struct settings {
	float face[4], light[4], highlight[4], shadow[4], dark[4];
	float active_title[4], active_title2[4], active_title_text[4];
	float inactive_title[4], inactive_title2[4], inactive_title_text[4];
	char font[64];
	char layout[64]; // "us", "de(nodeadkeys)"...
	int repeat_delay, repeat_rate;
	int double_click_ms;
	// X11 programs drawn at the displays' own pixels (xscale.c)
	bool sharp_x11;
	// touchpad
	bool tap_to_click, natural_scroll, disable_while_typing, middle_emulation;
	double pointer_speed; // -1..1
	// Control Panel > Accessibility Options (accessibility.c)
	struct {
		bool sticky_keys, sticky_lock, sticky_sound, sticky_shortcut;
		bool filter_keys, filter_shortcut;
		int filter_bounce_ms, filter_slow_ms;
		bool toggle_keys, toggle_shortcut;
		bool mouse_keys, mouse_keys_numlock_on, mouse_keys_shortcut;
		int mouse_keys_speed, mouse_keys_acceleration; // 1-10
		bool high_contrast_shortcut;
	} a11y;
};

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
	struct wlr_scene_tree *layer_snap;       // the outline of where a dragged window will snap (decoration.c)
	struct wlr_scene_tree *layer_top;        // layer-shell top (the taskbar)
	struct wlr_scene_tree *layer_fullscreen; // the full screen window, while it's in front (view.c)
	struct wlr_scene_tree *layer_overlay;    // layer-shell overlay (notifications)
	struct wlr_scene_tree *layer_unmanaged;  // X11 menus and tooltips
	struct wlr_scene_tree *drag_icons;       // what's being dragged
	struct wlr_scene_tree *layer_lock;       // the lock screen, above everything

	// the main display: the taskbar's, and where things go by default
	// (Control Panel > Display; else the first)
	struct output *primary;
	char socket_name[64]; // WAYLAND_DISPLAY (names the displays' state file)
	bool displays_applying;

	struct settings settings;
	int settings_watch; // inotify
	struct wl_event_source *settings_source;
	struct wl_list physical_keyboards; // struct physical_keyboard.link
	struct wl_list pointers; // struct pointer_device.link

	// a press on a frame button (it acts on release, if still over it)
	struct view *frame_press_view;
	enum frame_part frame_press_part;
	// double-clicks on title bars
	struct view *last_title_click_view;
	uint32_t last_title_click_ms;

	// Alt+Tab: switching through the windows, most recent first
	bool switching;
	struct view *switch_current;
	bool switch_box;     // the shell draws the box (else the windows come forward)
	bool keyboard_grab;  // a Move or Size from the window menu

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
	// KDE's older server-decoration protocol: how GTK 3 (and Firefox with
	// its own title bar turned off) ask for the Win95 frame
	struct wlr_server_decoration_manager *kde_decorations;
	struct wl_listener new_kde_decoration;
	struct wl_list kde_decos; // struct kde_deco.link
	// X11 programs at the displays' own pixels (xscale.c): X11's
	// coordinates are the desktop's times xscale; Xwayland's own xdg-output
	double xscale;
	int xscale_step; // desktop sizes that are whole X11 pixels: multiples of this
	struct wl_global *xdg_output_global;
	struct wl_list xdg_outputs; // struct xscale_output.link
	struct wlr_xdg_output_manager_v1 *xdg_output_manager; // everyone else's
	struct wl_list views; // struct view.link, front (top) first

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct wl_list layer_surfaces; // struct layer_surface.link
	struct layer_surface *exclusive_layer; // has the keyboard exclusively
	uint32_t layer_map_seq;

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

	struct wlr_keyboard_shortcuts_inhibit_manager_v1 *shortcuts_inhibit;
	struct wl_listener new_shortcuts_inhibitor;

	// when the desktop itself (the shell: taskbar, Start menu, a
	// notification) last got a click or key: a program may come forward
	// just after (protocols.c)
	uint32_t shell_input_ms;
	bool shell_input_seen;
	// the shell (wayland95-shell-v1, shell.c): the one bound, if any
	struct wl_global *shell_global;
	struct wl_resource *shell_resource;
	// --shell: the compositor starts it on a private connection (only it
	// may bind wayland95-shell-v1) and restarts it if it goes away
	const char *shell_program;
	bool shell_session;           // --session: it's the login session's
	struct wl_client *shell_client;
	struct wl_listener shell_client_destroy;
	pid_t shell_pid;
	struct wl_event_source *shell_restart_timer;
	int shell_restarts[5];        // when the last restarts were (s), oldest first
	bool stopping;

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
	double grab_start_x, grab_start_y; // where the pointer was when it began
	// a move with the pointer at a display's left or right edge: letting
	// go there snaps the window to that half (WLR_EDGE_LEFT/RIGHT, or NONE)
	uint32_t snap_edge;
	struct wlr_box snap_drawn; // where the outline is (width 0: hidden)
	struct wlr_scene_buffer *snap_outline[4];
};

struct output {
	struct wl_list link;
	struct server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
	// where windows may go on it: its place in the layout, less its
	// panels' exclusive zones (the taskbar's, on the main display)
	struct wlr_box usable;
};

enum view_type { VIEW_XDG, VIEW_XWAYLAND };

// one window: an xdg toplevel or an X11 window
struct view {
	struct node_tag tag; // NODE_VIEW
	struct wl_list link;
	uint32_t id; // for the shell (wayland95-shell-v1: its window menu)
	struct server *server;
	enum view_type type;
	struct wlr_xdg_toplevel *xdg_toplevel;   // VIEW_XDG
	struct wlr_xwayland_surface *xsurface;   // VIEW_XWAYLAND
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wlr_scene_tree *scene_tree;  // at the window's corner: frame and content
	struct wlr_scene_tree *content;     // the program's surfaces
	// the Win95 frame, when the program leaves decorations to us
	bool ssd;
	struct wlr_scene_tree *frame_tree;
	struct wlr_scene_buffer *frame[4]; // top (title bar), bottom, left, right
	struct wlr_scene_rect *resize_zone;
	int frame_w, frame_h; // the window size it was drawn for
	int frame_scale;      // and how many times finer than the desktop's pixels
	bool frame_active;  // the window is the active one
	enum frame_part frame_pressed; // a title button held down
	// what the current picture was drawn for
	bool frame_drawn_active, frame_maximized;
	enum frame_part frame_drawn_pressed;
	char *frame_title;
	bool mapped, placed, minimized, maximized, fullscreen;
	bool modal; // a modal dialog (xdg-dialog-v1): its owner waits on it
	int x, y;           // layout position of the window geometry's corner
	struct wlr_box saved; // size and place before maximize/fullscreen/snap
	// snapped to the left or right half of its display (WLR_EDGE_LEFT or
	// RIGHT), by dragging it to that edge; dragging it off restores it
	uint32_t snap_edge;
	// resizing with the mouse: the next size is sent when the program has
	// drawn the last one asked for (not one per mouse movement)
	uint32_t resize_serial;
	bool resize_waiting;
	int resize_w, resize_h;

	struct wlr_foreign_toplevel_handle_v1 *ftl;
	struct wlr_ext_foreign_toplevel_handle_v1 *ext_ftl;
	struct wl_listener ftl_activate, ftl_close, ftl_minimize, ftl_maximize, ftl_fullscreen;

	struct wl_listener map, unmap, commit, destroy;
	struct wl_listener scale_commit; // X11 windows (xscale.c: after the scene's)
	struct wl_listener request_move, request_resize, request_maximize;
	struct wl_listener request_minimize, request_fullscreen, request_show_window_menu;
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
	struct wl_listener scale_commit; // (xscale.c: after the scene's)
};

struct layer_surface {
	struct node_tag tag; // NODE_LAYER
	struct wl_list link;
	struct server *server;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene;
	struct output *output; // the display it's on (moved if that one goes)
	struct wl_listener map, unmap, commit, destroy, new_popup;
	uint32_t map_seq; // when it was last shown (the newest is on top)
};

struct popup {
	struct server *server;
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit, reposition;
	struct wl_listener map, unmap;
	struct wl_listener destroy;
};

struct keyboard {
	struct wl_list link;
	struct server *server;
	struct wlr_keyboard *wlr_keyboard;
	// while StickyKeys or FilterKeys is on, programs see the keys that got
	// through: this follows those (not the keyboard itself)
	struct xkb_state *delivered;
	struct xkb_keymap *delivered_keymap;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

struct physical_keyboard {
	struct wl_list link;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener destroy;
};

struct pointer_device {
	struct wl_list link;
	struct wlr_input_device *device;
	struct wl_listener destroy;
};

// output.c
void output_init(struct server *server);
bool output_active(struct output *output);                     // on, and part of the desktop
struct output *output_at(struct server *server, double lx, double ly); // (the main one if none is there)
struct output *output_for_view(struct view *view);             // the display most of it is on
void outputs_changed(struct server *server); // displays came, went, moved or changed size
void output_place(struct output *output, bool in_layout, bool has_position, int x, int y);
void lock_cover_update(struct server *server); // (protocols.c) the lock's cover, over every display

// displays.c: Control Panel > Display's settings (displays.ini), and what
// the displays are for it to show
void displays_init(struct server *server);
void displays_apply(struct server *server);       // the saved settings, to every display
void displays_write_state(struct server *server); // what there is, for the Control Panel
void displays_finish(struct server *server);
int displays_frame_scale(struct server *server);  // window frames are drawn this many times finer

// settings.c
void settings_init(struct server *server);
void settings_apply_keyboard(struct server *server, struct wlr_keyboard *keyboard);
void settings_apply_pointer(struct server *server, struct wlr_input_device *device);

// decoration.c
void frame_update(struct view *view); // create, redraw, or remove as needed
void frame_destroy(struct view *view);
enum frame_part frame_part_at(struct view *view, double lx, double ly, uint32_t *edges);
void frame_set_pressed(struct view *view, enum frame_part part);
void snap_outline_show(struct server *server, const struct wlr_box *box); // NULL hides it
const char *frame_cursor(enum frame_part part, uint32_t edges);
// the frame's extent around the window (0 without a frame)
void view_frame_insets(struct view *view, int *left, int *top, int *right, int *bottom);

// input.c
void input_init(struct server *server);
void input_reset_cursor_mode(struct server *server); // the move or resize ends (a move at an edge snaps)
void input_cancel_snap(struct server *server);        // the move ends without snapping
void input_refresh_pointer(struct server *server); // re-pick the surface under the pointer
void input_lock(struct server *server); // the screen locks: grabs, drags and focus end
void input_focus_surface(struct server *server, struct wlr_surface *surface); // keyboard

// view.c
struct view *view_from_surface_at(struct server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy, struct node_tag **owner);
void view_init_common(struct view *view, struct server *server, enum view_type type);
struct wlr_surface *view_surface(struct view *view);
void view_geometry(struct view *view, struct wlr_box *box); // size, and offset in the surface
void view_move(struct view *view, int x, int y);
void view_resize(struct view *view, int width, int height);
void view_resize_paced(struct view *view, int width, int height); // while dragging an edge
void view_resize_drawn(struct view *view);  // the program drew a size: send the next, if one waits
void view_resize_flush(struct view *view);  // the drag ended: the last size now
void view_focus(struct view *view);
void view_close(struct view *view);
void view_set_minimized(struct view *view, bool minimized);
void view_set_maximized(struct view *view, bool maximized);
void view_set_fullscreen(struct view *view, bool fullscreen);
// snap to the left or right half of OUTPUT's usable area (NULL: the one
// it's on); WLR_EDGE_NONE restores the size and place from before
void view_set_snapped(struct view *view, uint32_t edge, struct output *output);
void view_snap_box(struct view *view, uint32_t edge, struct output *output, struct wlr_box *frame, struct wlr_box *content);
bool view_resizable(struct view *view); // not a fixed-size window
void view_mapped(struct view *view);   // after the type-specific map
void view_unmapped(struct view *view); // before the type-specific unmap
void view_update_title(struct view *view);
const char *view_title(struct view *view); // never NULL
void view_begin_interactive(struct view *view, enum cursor_mode mode, uint32_t edges);
struct view *view_top(struct server *server); // topmost mapped, not minimized
struct view *view_active(struct server *server); // the active window (its dialogs may be in front)
void view_arrange_all(struct server *server); // usable area changed

// xdg.c
void xdg_init(struct server *server);

// xwayland.c
void xwayland_init(struct server *server);

// xscale.c: X11 programs at the displays' own pixels
void xscale_init(struct server *server);
void xscale_update(struct server *server); // the displays or the setting changed
void xscale_xwayland_ready(struct server *server);
bool xscale_global_visible(struct server *server, const struct wl_client *client, const struct wl_global *global);
void xscale_apply(struct server *server, struct wlr_scene_tree *tree); // an X11 surface committed
int xscale_to_x(struct server *server, double v);   // desktop -> X11
int xscale_from_x(struct server *server, double v); // X11 -> desktop
int xscale_snap(struct server *server, int size); // an X11 window's size, made exact (no stretching)
// a point in a surface from the scene (desktop units) -> the surface's own
void xscale_surface_point(struct server *server, struct wlr_surface *surface, double *sx, double *sy);

// layers.c
void layers_init(struct server *server);
void layers_arrange(struct server *server);
void layers_focus(struct server *server); // the keyboard to an exclusive panel, else the top window

// protocols.c: everything else (idle, activation, lock, outputs, ...)
void protocols_init(struct server *server);
void dialog_init(struct server *server); // xdg-dialog-v1 (dialog.c)
struct view *view_from_xdg_toplevel(struct wlr_xdg_toplevel *toplevel); // or NULL
void protocols_finish(struct server *server);
void shell_init(struct server *server);
void shell_launch(struct server *server); // --shell: start it (if given)
void shell_finish(struct server *server);
uint32_t shell_key_for(xkb_keysym_t sym, uint32_t modifiers); // 0: not one of the shell's keys
bool shell_send_key(struct server *server, uint32_t key, bool pressed); // false: no shell
void shell_send_tone(struct server *server, uint32_t tone);
void shell_send_shortcut(struct server *server, uint32_t feature);
void shell_send_sticky(struct server *server, const char *held);
void shell_send_primary(struct server *server); // which display is the main one
// Alt+Tab's box and the window menu are the shell's to draw: false if
// there's no shell (the compositor then does without)
bool shell_switcher_show(struct server *server, struct view **views, int n, int selected);
void shell_switcher_select(struct server *server, int selected);
void shell_switcher_hide(struct server *server);
bool shell_window_menu(struct view *view, int x, int y);
struct view *view_by_id(struct server *server, uint32_t id);
// the window menu's Move and Size: follow the pointer until a click
void input_begin_keyboard_grab(struct view *view, enum cursor_mode mode);
// Accessibility Options (accessibility.c): keys pass through here first
struct keyboard;
void a11y_key(struct server *server, struct keyboard *keyboard, uint32_t time, uint32_t keycode, bool pressed);
void a11y_pointer_released(struct server *server); // a button let go: latched keys go too
void a11y_settings_changed(struct server *server);
void a11y_keyboard_gone(struct server *server, struct keyboard *keyboard);
bool a11y_owns_modifiers(struct server *server); // StickyKeys or FilterKeys: modifiers are ours
void a11y_finish(struct server *server);
// input.c, for accessibility.c: deliver a key as if it had just arrived;
// the modifiers programs see; the pointer
void input_key(struct server *server, struct keyboard *keyboard, uint32_t time, uint32_t keycode, bool pressed);
void input_modifiers(struct keyboard *keyboard, struct wlr_keyboard_modifiers *out);
void input_pointer_move(struct server *server, uint32_t time, double dx, double dy);
void input_pointer_button(struct server *server, uint32_t time, uint32_t button, bool pressed);
