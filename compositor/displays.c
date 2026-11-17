// Displays as Control Panel > Display sets them: for each (by its name:
// eDP-1, HDMI-A-1...) whether it's on, its resolution and refresh rate,
// scale, rotation, place in the desktop, adaptive sync, and which is the
// main display (the taskbar's). Read from ~/.config/wayland95/displays.ini
// (the Control Panel writes it; settings.c watches the folder), applied when
// it changes and when a display is plugged in. A display with no entry is
// on at its own best mode, to the right of the others.
//
//   [HDMI-A-1]
//   Enabled=true
//   Mode=1920x1080@60000        (refresh in mHz; 0 or none: the highest)
//   Scale=1.25
//   Transform=90                (0, 90, 180, 270)
//   X=1920                      (both, or neither: placed automatically)
//   Y=0
//   AdaptiveSync=false
//   Primary=true
//
// What the displays are (and can do) is written to
// $XDG_RUNTIME_DIR/wayland95-displays-SOCKET.ini for the Control Panel to
// show, each time anything changes.
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>

#include "server.h"

struct display_config {
	char name[64];
	bool enabled;
	int width, height, refresh; // 0: its own best
	float scale;
	int transform; // degrees
	bool has_x, has_y;
	int x, y;
	bool adaptive_sync, primary;
};

#define MAX_DISPLAYS 16
static struct display_config configs[MAX_DISPLAYS];
static int nconfigs;

static const char *config_path(char *buf, size_t size)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg)
		snprintf(buf, size, "%s/wayland95/displays.ini", xdg);
	else
		snprintf(buf, size, "%s/.config/wayland95/displays.ini", getenv("HOME") ? getenv("HOME") : "/tmp");
	return buf;
}

static bool is_true(const char *v)
{
	return strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
}

static void load(void)
{
	nconfigs = 0;
	char path[600];
	FILE *f = fopen(config_path(path, sizeof path), "r");
	if (!f)
		return;
	char line[512];
	struct display_config *c = NULL;
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0] == '[') {
			c = NULL;
			if (nconfigs < MAX_DISPLAYS) {
				c = &configs[nconfigs++];
				*c = (struct display_config){.enabled = true, .scale = 1.0f};
				snprintf(c->name, sizeof c->name, "%.*s", (int)strcspn(line + 1, "]"), line + 1);
			}
			continue;
		}
		char *eq = strchr(line, '=');
		if (!eq || !c)
			continue;
		*eq = 0;
		const char *key = line;
		char *value = eq + 1;
		size_t vl = strlen(value);
		if (vl >= 2 && value[0] == '"' && value[vl - 1] == '"') { // QSettings may quote
			value[vl - 1] = 0;
			value++;
		}
		if (strcmp(key, "Enabled") == 0) c->enabled = is_true(value);
		else if (strcmp(key, "Mode") == 0) {
			c->width = c->height = c->refresh = 0;
			if (sscanf(value, "%dx%d@%d", &c->width, &c->height, &c->refresh) < 2)
				c->width = c->height = 0;
		}
		else if (strcmp(key, "Scale") == 0) c->scale = strtof(value, NULL);
		else if (strcmp(key, "Transform") == 0) c->transform = atoi(value);
		else if (strcmp(key, "X") == 0) { c->x = atoi(value); c->has_x = true; }
		else if (strcmp(key, "Y") == 0) { c->y = atoi(value); c->has_y = true; }
		else if (strcmp(key, "AdaptiveSync") == 0) c->adaptive_sync = is_true(value);
		else if (strcmp(key, "Primary") == 0) c->primary = is_true(value);
	}
	fclose(f);
}

static const struct display_config *config_for(const char *name)
{
	for (int i = 0; i < nconfigs; ++i)
		if (strcmp(configs[i].name, name) == 0)
			return &configs[i];
	return NULL;
}

static enum wl_output_transform transform_of(int degrees)
{
	switch (((degrees % 360) + 360) % 360) {
	case 90: return WL_OUTPUT_TRANSFORM_90;
	case 180: return WL_OUTPUT_TRANSFORM_180;
	case 270: return WL_OUTPUT_TRANSFORM_270;
	default: return WL_OUTPUT_TRANSFORM_NORMAL;
	}
}

static int degrees_of(enum wl_output_transform t)
{
	switch (t) {
	case WL_OUTPUT_TRANSFORM_90: case WL_OUTPUT_TRANSFORM_FLIPPED_90: return 90;
	case WL_OUTPUT_TRANSFORM_180: case WL_OUTPUT_TRANSFORM_FLIPPED_180: return 180;
	case WL_OUTPUT_TRANSFORM_270: case WL_OUTPUT_TRANSFORM_FLIPPED_270: return 270;
	default: return 0;
	}
}

// the display's mode nearest to what's asked: that size, and the refresh
// rate closest to it (the highest when none is asked)
static struct wlr_output_mode *find_mode(struct wlr_output *o, int width, int height, int refresh)
{
	struct wlr_output_mode *mode, *best = NULL;
	wl_list_for_each(mode, &o->modes, link) {
		if (mode->width != width || mode->height != height)
			continue;
		if (!best) {
			best = mode;
		} else if (refresh > 0) {
			if (abs(mode->refresh - refresh) < abs(best->refresh - refresh))
				best = mode;
		} else if (mode->refresh > best->refresh) {
			best = mode;
		}
	}
	return best;
}

// One display to its settings. Returns whether it's on.
static bool apply_one(struct output *output, const struct display_config *c, bool force_on)
{
	struct wlr_output *o = output->wlr_output;
	bool enabled = force_on || !c || c->enabled;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, enabled);
	if (enabled) {
		struct wlr_output_mode *mode = c && c->width > 0 ? find_mode(o, c->width, c->height, c->refresh) : NULL;
		if (c && c->width > 0 && !mode && !wl_list_empty(&o->modes))
			wlr_log(WLR_ERROR, "%s has no %dx%d mode: using its own best", o->name, c->width, c->height);
		if (!mode)
			mode = wlr_output_preferred_mode(o);
		if (mode)
			wlr_output_state_set_mode(&state, mode);
		else if (c && c->width > 0) // no list of modes (a window, a virtual display): any size
			wlr_output_state_set_custom_mode(&state, c->width, c->height, c->refresh);
		float scale = c && c->scale >= 0.5f && c->scale <= 4.0f ? c->scale : 1.0f;
		wlr_output_state_set_scale(&state, scale);
		wlr_output_state_set_transform(&state, transform_of(c ? c->transform : 0));
		if (o->adaptive_sync_supported)
			wlr_output_state_set_adaptive_sync_enabled(&state, c && c->adaptive_sync);
	}
	bool ok = wlr_output_test_state(o, &state) && wlr_output_commit_state(o, &state);
	wlr_output_state_finish(&state);
	if (!ok && enabled) {
		// what it can surely do: on, its own best mode
		wlr_log(WLR_ERROR, "%s can't do its saved settings: using its own best mode", o->name);
		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, true);
		struct wlr_output_mode *mode = wlr_output_preferred_mode(o);
		if (mode)
			wlr_output_state_set_mode(&state, mode);
		wlr_output_state_set_scale(&state, 1.0f);
		wlr_output_state_set_transform(&state, WL_OUTPUT_TRANSFORM_NORMAL);
		ok = wlr_output_commit_state(o, &state);
		wlr_output_state_finish(&state);
		if (!ok)
			wlr_log(WLR_ERROR, "can't turn on output %s", o->name);
		c = NULL; // (and placed automatically)
	} else if (!ok) {
		wlr_log(WLR_ERROR, "can't turn off output %s", o->name);
	}
	bool on = o->enabled;
	output_place(output, on, c && c->has_x && c->has_y, c ? c->x : 0, c ? c->y : 0);
	return on;
}

void displays_apply(struct server *server)
{
	load();
	server->displays_applying = true;
	struct output *output, *chosen = NULL, *first = NULL;
	int on = 0;
	// (oldest first, so those placed automatically keep their order)
	wl_list_for_each_reverse(output, &server->outputs, link) {
		const struct display_config *c = config_for(output->wlr_output->name);
		if (apply_one(output, c, false)) {
			on++;
			if (c && c->primary && !chosen)
				chosen = output;
		}
		if (!first)
			first = output;
	}
	// never all of them off: there'd be no way to turn one back on
	if (on == 0 && first) {
		wlr_log(WLR_ERROR, "every display is turned off in the settings: turning %s on", first->wlr_output->name);
		apply_one(first, config_for(first->wlr_output->name), true);
	}
	// the main display: the one chosen, else the first (outputs_changed picks)
	server->primary = chosen;
	// the pointer as fine as the finest display
	wl_list_for_each(output, &server->outputs, link)
		if (output->wlr_output->enabled)
			wlr_xcursor_manager_load(server->cursor_mgr, ceilf(output->wlr_output->scale));
	server->displays_applying = false;
	outputs_changed(server);
}

int displays_frame_scale(struct server *server)
{
	float scale = 1.0f;
	struct output *output;
	wl_list_for_each(output, &server->outputs, link)
		if (output_active(output) && output->wlr_output->scale > scale)
			scale = output->wlr_output->scale;
	int s = (int)ceilf(scale);
	return s < 1 ? 1 : s > 4 ? 4 : s;
}

// ---------------------------------------------- what there is, for the UI

static const char *state_path(struct server *server, char *buf, size_t size)
{
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (!dir || !*dir || !server->socket_name[0])
		return NULL;
	snprintf(buf, size, "%s/wayland95-displays-%s.ini", dir, server->socket_name);
	return buf;
}

// a value QSettings reads back as one string
static void put_string(FILE *f, const char *key, const char *value)
{
	fprintf(f, "%s=\"", key);
	for (const char *p = value ? value : ""; *p; ++p) {
		if (*p == '"' || *p == '\\')
			fputc('\\', f);
		if ((unsigned char)*p >= 0x20)
			fputc(*p, f);
	}
	fputs("\"\n", f);
}

void displays_write_state(struct server *server)
{
	char path[600], tmp[620];
	if (!state_path(server, path, sizeof path))
		return;
	snprintf(tmp, sizeof tmp, "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f)
		return;
	struct output *output;
	wl_list_for_each_reverse(output, &server->outputs, link) {
		struct wlr_output *o = output->wlr_output;
		struct wlr_box box = {0};
		if (output_active(output))
			wlr_output_layout_get_box(server->output_layout, o, &box);
		fprintf(f, "[%s]\n", o->name);
		put_string(f, "Description", o->description);
		put_string(f, "Make", o->make);
		put_string(f, "Model", o->model);
		fprintf(f, "Enabled=%s\n", output_active(output) ? "true" : "false");
		fprintf(f, "Primary=%s\n", output == server->primary ? "true" : "false");
		fprintf(f, "Width=%d\nHeight=%d\nRefresh=%d\n", o->width, o->height, o->refresh);
		fprintf(f, "Scale=%.2f\n", o->scale);
		fprintf(f, "Transform=%d\n", degrees_of(o->transform));
		fprintf(f, "X=%d\nY=%d\nLogicalWidth=%d\nLogicalHeight=%d\n", box.x, box.y, box.width, box.height);
		fprintf(f, "PhysicalWidth=%d\nPhysicalHeight=%d\n", o->phys_width, o->phys_height);
		fprintf(f, "AdaptiveSyncSupported=%s\n", o->adaptive_sync_supported ? "true" : "false");
		fprintf(f, "AdaptiveSync=%s\n", o->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED ? "true" : "false");
		// its modes, best first as it lists them; "*" marks its own best.
		// None: any size goes (a window, a virtual display)
		fputs("Modes=\"", f);
		struct wlr_output_mode *mode;
		bool any = false;
		wl_list_for_each(mode, &o->modes, link) {
			fprintf(f, "%s%dx%d@%d%s", any ? " " : "", mode->width, mode->height, mode->refresh,
					mode->preferred ? "*" : "");
			any = true;
		}
		fputs("\"\n\n", f);
	}
	fclose(f);
	rename(tmp, path);
}

void displays_init(struct server *server)
{
	(void)server; // (displays are applied as they're found: output.c)
}

void displays_finish(struct server *server)
{
	char path[600];
	if (state_path(server, path, sizeof path))
		unlink(path);
}
