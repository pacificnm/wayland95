// Where windows were: a program's window opens again at the place and size
// it had when it was last moved, resized, maximized or snapped, across
// restarts and logins. Kept in ~/.config/wayland95/windows.tsv, one line a
// window, the most recent first:
//
//   STATE  X  Y  WIDTH  HEIGHT  APP  TITLE      (tab-separated)
//
// STATE is normal, maximized, left or right (snapped to that half); X, Y,
// WIDTH and HEIGHT are the window's own (its content, inside the frame) in
// the desktop's layout, before it was maximized or snapped. APP is the xdg
// app_id, or the X11 class (and instance, if it differs). A window is
// matched by its app and title first, then by its app alone (the title is
// often a document's name). Written a moment after the last change, not on
// every movement of the mouse.
//
// Left alone: dialogs and other windows that belong to another (an xdg
// parent, an X11 transient or modal), windows that ask to start maximized or
// full screen, X11 windows that put themselves somewhere (or were told to:
// -geometry +X+Y), and full screen and minimized states (a program that starts
// full screen or out of sight surprises). Windows that can't be resized get
// their place back, not a size.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include <xcb/xcb_icccm.h>

#include "server.h"

#define MAX_PLACES 256
#define MAX_TITLES_PER_APP 8 // (a browser's title changes with every page)
#define WRITE_DELAY_MS 1500
#define CASCADE 30

enum place_state { PLACE_NORMAL, PLACE_MAXIMIZED, PLACE_LEFT, PLACE_RIGHT };
static const char *state_names[] = {"normal", "maximized", "left", "right"};

struct place {
	char *app, *title;
	struct wlr_box box; // width 0: never had a size of its own (maximized from the start)
	enum place_state state;
};

// most recent first
static struct place places[MAX_PLACES];
static int nplaces;
static struct wl_event_source *write_timer;
static bool dirty;

static const char *file_path(char *buf, size_t size)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg)
		snprintf(buf, size, "%s/wayland95/windows.tsv", xdg);
	else
		snprintf(buf, size, "%s/.config/wayland95/windows.tsv", getenv("HOME") ? getenv("HOME") : "/tmp");
	return buf;
}

// ------------------------------------------------------- which windows

// the program, as a string: the xdg app_id, the X11 class (and instance)
static void app_key(struct view *view, char *buf, size_t size)
{
	if (view->type == VIEW_XDG) {
		snprintf(buf, size, "%s", view->xdg_toplevel->app_id ? view->xdg_toplevel->app_id : "");
		return;
	}
	const char *class = view->xsurface->class ? view->xsurface->class : "";
	const char *instance = view->xsurface->instance ? view->xsurface->instance : "";
	if (*instance && strcasecmp(instance, class) != 0)
		snprintf(buf, size, "%s/%s", class, instance);
	else
		snprintf(buf, size, "%s", class);
}

// a main window (not a dialog, not anyone's transient), that can be known
// again by its app or title
static bool remembered(struct view *view)
{
	if (view->modal)
		return false;
	if (view->type == VIEW_XDG) {
		if (view->xdg_toplevel->parent)
			return false;
	} else {
		struct wlr_xwayland_surface *xs = view->xsurface;
		if (xs->parent || xs->modal || xs->override_redirect)
			return false;
	}
	char app[256];
	app_key(view, app, sizeof app);
	return *app || *view_title(view);
}

// tabs and line ends would break the file's lines
static char *clean_copy(const char *s)
{
	char *c = strdup(s ? s : "");
	for (char *p = c; *p; ++p)
		if (*p == '\t' || *p == '\n' || *p == '\r')
			*p = ' ';
	return c;
}

static int find_exact(const char *app, const char *title)
{
	for (int i = 0; i < nplaces; ++i)
		if (strcmp(places[i].app, app) == 0 && strcmp(places[i].title, title) == 0)
			return i;
	return -1;
}

// the window's own place (its app and title), else its app's latest
static int find_for(struct view *view)
{
	char app[256];
	app_key(view, app, sizeof app);
	char *title = clean_copy(view_title(view));
	int i = find_exact(app, title);
	free(title);
	if (i >= 0 || !*app)
		return i;
	for (i = 0; i < nplaces; ++i)
		if (strcmp(places[i].app, app) == 0)
			return i;
	return -1;
}

static void drop(int i)
{
	free(places[i].app);
	free(places[i].title);
	memmove(&places[i], &places[i + 1], (nplaces - i - 1) * sizeof places[0]);
	--nplaces;
}

// ---------------------------------------------------------- the file

static void load(void)
{
	char path[600];
	FILE *f = fopen(file_path(path, sizeof path), "r");
	if (!f)
		return;
	char line[2048];
	while (nplaces < MAX_PLACES && fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == '#' || !line[0])
			continue;
		char *fields[7];
		int n = 0;
		char *p = line;
		while (n < 7) {
			fields[n++] = p;
			char *tab = strchr(p, '\t');
			if (!tab)
				break;
			*tab = 0;
			p = tab + 1;
		}
		if (n < 7)
			continue;
		int state = -1;
		for (int s = 0; s < 4; ++s)
			if (strcmp(fields[0], state_names[s]) == 0)
				state = s;
		struct wlr_box box = {atoi(fields[1]), atoi(fields[2]), atoi(fields[3]), atoi(fields[4])};
		if (state < 0 || box.width < 0 || box.height < 0 || (!*fields[5] && !*fields[6])
				|| find_exact(fields[5], fields[6]) >= 0)
			continue;
		places[nplaces++] = (struct place){strdup(fields[5]), strdup(fields[6]), box, state};
	}
	fclose(f);
}

static void save(void)
{
	char path[600], tmp[620];
	file_path(path, sizeof path);
	// (the folder may not be there yet: a first login)
	char dir[600];
	snprintf(dir, sizeof dir, "%s", path);
	*strrchr(dir, '/') = 0;
	char parent[600];
	snprintf(parent, sizeof parent, "%s", dir);
	char *slash = strrchr(parent, '/');
	if (slash && slash != parent) {
		*slash = 0;
		mkdir(parent, 0700);
	}
	mkdir(dir, 0700);
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		wlr_log(WLR_ERROR, "can't write %s: %s", tmp, strerror(errno));
		return;
	}
	fputs("# Where windows were (wayland95-compositor writes this; the newest first)\n"
			"# state\tx\ty\twidth\theight\tapp\ttitle\n", f);
	for (int i = 0; i < nplaces; ++i) {
		const struct place *p = &places[i];
		fprintf(f, "%s\t%d\t%d\t%d\t%d\t%s\t%s\n", state_names[p->state], p->box.x, p->box.y,
				p->box.width, p->box.height, p->app, p->title);
	}
	if (fclose(f) == 0)
		rename(tmp, path);
	dirty = false;
}

static int write_later(void *data)
{
	struct server *server = data;
	// (not while a window is being dragged: when it's let go)
	if (server->cursor_mode != CURSOR_PASSTHROUGH) {
		wl_event_source_timer_update(write_timer, WRITE_DELAY_MS);
		return 0;
	}
	save();
	return 0;
}

// --------------------------------------------------- remembering

void places_note(struct view *view)
{
	if (!view->mapped || !remembered(view))
		return;
	struct wlr_box box;
	enum place_state state;
	if (view->maximized || view->snap_edge || view->fullscreen) {
		// its size from before (full screen isn't remembered: what it
		// was in before, is)
		box = view->saved;
		state = view->maximized ? PLACE_MAXIMIZED : view->snap_edge == WLR_EDGE_LEFT ? PLACE_LEFT
				: view->snap_edge == WLR_EDGE_RIGHT ? PLACE_RIGHT : PLACE_NORMAL;
		if (state == PLACE_NORMAL && box.width <= 0)
			return;
	} else {
		struct wlr_box geo;
		view_geometry(view, &geo);
		if (geo.width <= 0 || geo.height <= 0)
			return;
		box = (struct wlr_box){view->x, view->y, geo.width, geo.height};
		state = PLACE_NORMAL;
	}
	// (the same as last time: most commits are just new pictures)
	if (view->place_noted && wlr_box_equal(&box, &view->place_box) && (int)state == view->place_state)
		return;
	view->place_noted = true;
	view->place_box = box;
	view->place_state = state;

	char app[256];
	app_key(view, app, sizeof app);
	char *title = clean_copy(view_title(view));
	int i = find_exact(app, title);
	struct place p;
	if (i >= 0) {
		p = places[i];
		free(title);
		memmove(&places[1], &places[0], i * sizeof places[0]);
	} else {
		// a new one: the oldest of this app's titles goes, if it has many,
		// and the oldest of all if the list is full
		int same = 0, oldest = -1;
		for (int j = 0; j < nplaces; ++j)
			if (strcmp(places[j].app, app) == 0) {
				++same;
				oldest = j;
			}
		if (*app && same >= MAX_TITLES_PER_APP)
			drop(oldest);
		else if (nplaces == MAX_PLACES)
			drop(nplaces - 1);
		p = (struct place){.app = strdup(app), .title = title};
		memmove(&places[1], &places[0], nplaces * sizeof places[0]);
		++nplaces;
	}
	p.box = box;
	p.state = state;
	places[0] = p;
	dirty = true;
	if (write_timer)
		wl_event_source_timer_update(write_timer, WRITE_DELAY_MS);
}

// --------------------------------------------------- putting it back

// where the frame would be, for a window's own box
static struct wlr_box frame_of(struct view *view, struct wlr_box box)
{
	int left, top, right, bottom;
	view_frame_insets(view, &left, &top, &right, &bottom);
	return (struct wlr_box){box.x - left, box.y - top, box.width + left + right, box.height + top + bottom};
}

// the display a remembered window is mostly on (if it's still there),
// else the one the pointer is on
static struct output *output_for_box(struct server *server, struct wlr_box frame)
{
	struct output *best = NULL, *output;
	long best_area = 0;
	wl_list_for_each(output, &server->outputs, link) {
		if (!output_active(output))
			continue;
		struct wlr_box in;
		if (!wlr_box_intersection(&in, &frame, &output->usable))
			continue;
		long area = (long)in.width * in.height;
		if (area > best_area) {
			best_area = area;
			best = output;
		}
	}
	return best ? best : output_at(server, server->cursor->x, server->cursor->y);
}

static int clampi(int v, int lo, int hi)
{
	return hi < lo ? lo : v < lo ? lo : v > hi ? hi : v;
}

// another window of the same program at about that place
static bool taken(struct view *view, const char *app, int x, int y)
{
	struct view *other;
	char other_app[256];
	wl_list_for_each(other, &view->server->views, link) {
		if (other == view || !other->mapped)
			continue;
		app_key(other, other_app, sizeof other_app);
		if (strcmp(other_app, app) == 0 && abs(other->x - x) < 10 && abs(other->y - y) < 10)
			return true;
	}
	return false;
}

bool places_restore(struct view *view)
{
	struct server *server = view->server;
	// (one that asked to start maximized or full screen: as it asked)
	if (view->placed || view->maximized || view->fullscreen || !remembered(view))
		return false;
	if (view->type == VIEW_XWAYLAND) {
		// told where to go (-geometry +X+Y): there. (Not USSize: Qt sets
		// that on every window, so it says nothing about who chose it.)
		const xcb_size_hints_t *h = view->xsurface->size_hints;
		if (h && (h->flags & XCB_ICCCM_SIZE_HINT_US_POSITION))
			return false;
	}
	int i = find_for(view);
	if (i < 0)
		return false;
	struct place p = places[i];
	bool resizable = view_resizable(view);
	struct wlr_box box = p.box;
	if (box.width <= 0 || box.height <= 0 || !resizable) {
		// its own size (the one it has, or will choose)
		struct wlr_box geo;
		view_geometry(view, &geo);
		box.width = geo.width;
		box.height = geo.height;
	}
	char app[256];
	app_key(view, app, sizeof app);

	// on a display that's there, with the frame on it (a smaller screen:
	// smaller); another window of the program already there: cascaded
	struct output *output = output_for_box(server, frame_of(view, box));
	struct wlr_box area = output ? output->usable : (struct wlr_box){0, 0, 640, 480};
	int left, top, right, bottom;
	view_frame_insets(view, &left, &top, &right, &bottom);
	if (resizable && box.width > 0) {
		if (box.width > area.width - left - right)
			box.width = area.width - left - right;
		if (box.height > area.height - top - bottom)
			box.height = area.height - top - bottom;
	}
	for (int n = 0; n < 8 && taken(view, app, box.x, box.y); ++n) {
		box.x += CASCADE;
		box.y += CASCADE;
		if (box.x + box.width + right > area.x + area.width || box.y + box.height + bottom > area.y + area.height) {
			box.x = area.x + left + CASCADE * (n + 1) % 240;
			box.y = area.y + top + CASCADE * (n + 1) % 240;
		}
	}
	box.x = clampi(box.x, area.x + left, area.x + area.width - right - box.width);
	box.y = clampi(box.y, area.y + top, area.y + area.height - bottom - box.height);
	wlr_log(WLR_DEBUG, "%s \"%s\": back at %d,%d %dx%d (%s)", app, view_title(view), box.x, box.y,
			box.width, box.height, state_names[p.state]);

	view->x = box.x;
	view->y = box.y;
	wlr_scene_node_set_position(&view->scene_tree->node, view->x, view->y);
	struct wlr_box normal = box;
	if (p.box.width <= 0 || p.box.height <= 0)
		normal.width = 0; // (never had a size of its own: it chooses, when restored)
	if (p.state == PLACE_MAXIMIZED && resizable) {
		view_set_maximized(view, true);
		view->saved = normal;
	} else if ((p.state == PLACE_LEFT || p.state == PLACE_RIGHT) && resizable) {
		view_set_snapped(view, p.state == PLACE_LEFT ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT, output);
		view->saved = normal;
	} else {
		if (resizable && normal.width > 0)
			view_resize(view, box.width, box.height);
		else if (view->type == VIEW_XWAYLAND)
			view_move(view, box.x, box.y); // (X11 hears where it is)
		view->placed = true;
		view->place_restored = true;
	}
	return true;
}

void places_clamp(struct view *view)
{
	view->place_restored = false;
	struct wlr_box geo;
	view_geometry(view, &geo);
	struct wlr_box box = {view->x, view->y, geo.width, geo.height};
	struct output *output = output_for_box(view->server, frame_of(view, box));
	if (!output)
		return;
	const struct wlr_box *area = &output->usable;
	int left, top, right, bottom;
	view_frame_insets(view, &left, &top, &right, &bottom);
	view_move(view, clampi(box.x, area->x + left, area->x + area->width - right - box.width),
			clampi(box.y, area->y + top, area->y + area->height - bottom - box.height));
}

// ------------------------------------------------------------- set up

void places_init(struct server *server)
{
	load();
	write_timer = wl_event_loop_add_timer(server->event_loop, write_later, server);
}

void places_finish(struct server *server)
{
	if (dirty)
		save();
	if (write_timer)
		wl_event_source_remove(write_timer);
	write_timer = NULL;
	for (int i = 0; i < nplaces; ++i) {
		free(places[i].app);
		free(places[i].title);
	}
	nplaces = 0;
}
