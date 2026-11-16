// Settings from ~/.config/wayland95/settings.ini (written by the Control
// Panel, through QSettings): the colour scheme for window frames, the
// keyboard layout and repeat, the double-click time, and touchpad options.
// The file is watched; changes apply at once.
#define _GNU_SOURCE
#include <libinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>

#include "server.h"

static void set_color(float out[4], unsigned rgb)
{
	out[0] = ((rgb >> 16) & 0xff) / 255.0f;
	out[1] = ((rgb >> 8) & 0xff) / 255.0f;
	out[2] = (rgb & 0xff) / 255.0f;
	out[3] = 1.0f;
}

static bool parse_color(const char *value, float out[4])
{
	const char *hex = value[0] == '#' ? value + 1 : value;
	if (strlen(hex) < 6)
		return false;
	char *end;
	unsigned long rgb = strtoul(hex, &end, 16);
	if (end - hex != 6)
		return false;
	set_color(out, (unsigned)rgb);
	return true;
}

static void mix(const float a[4], const float b[4], float t, float out[4])
{
	for (int i = 0; i < 3; ++i)
		out[i] = a[i] + (b[i] - a[i]) * t;
	out[3] = 1.0f;
}

// the bevel colours from the face colour, as the Control Panel works them
// out (apps/common/wayland95settings.cpp: Win95's c0c0c0 gives light dfdfdf,
// highlight ffffff, shadow 808080; dark faces get light edges)
static void derive_edges(struct settings *s)
{
	static const float white[4] = {1, 1, 1, 1}, black[4] = {0, 0, 0, 1};
	float maxc = s->face[0] > s->face[1] ? s->face[0] : s->face[1];
	maxc = maxc > s->face[2] ? maxc : s->face[2];
	float minc = s->face[0] < s->face[1] ? s->face[0] : s->face[1];
	minc = minc < s->face[2] ? minc : s->face[2];
	float lightness = (maxc + minc) / 2.0f; // HSL lightness
	if (lightness < 0.25f) {
		mix(s->face, white, 0.4f, s->shadow);
		mix(s->face, white, 0.6f, s->light);
		memcpy(s->highlight, white, sizeof white);
	} else {
		mix(s->face, black, 1.0f / 3.0f, s->shadow);
		mix(s->face, white, 0.49f, s->light);
		if (lightness >= 0.5f)
			memcpy(s->highlight, white, sizeof white);
		else
			mix(s->face, white, 0.75f, s->highlight);
	}
	memcpy(s->dark, black, sizeof black);
}

static void defaults(struct settings *s)
{
	// "Wayland 95 Standard"
	set_color(s->face, 0xc0c0c0);
	set_color(s->active_title, 0x000080);
	set_color(s->active_title2, 0x1084d0);
	set_color(s->active_title_text, 0xffffff);
	set_color(s->inactive_title, 0x808080);
	set_color(s->inactive_title2, 0xb5b5b5);
	set_color(s->inactive_title_text, 0xc0c0c0);
	snprintf(s->font, sizeof s->font, "Liberation Sans");
	s->layout[0] = 0;
	s->repeat_delay = 600;
	s->repeat_rate = 25;
	s->double_click_ms = 400;
	s->tap_to_click = true;
	s->natural_scroll = false;
	s->disable_while_typing = true;
	s->middle_emulation = false;
	s->pointer_speed = 0;
}

static const char *config_dir(char *buf, size_t size)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg)
		snprintf(buf, size, "%s/wayland95", xdg);
	else
		snprintf(buf, size, "%s/.config/wayland95", getenv("HOME") ? getenv("HOME") : "/tmp");
	return buf;
}

static bool is_true(const char *v)
{
	return strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
}

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static void load(struct server *server)
{
	struct settings *s = &server->settings;
	defaults(s);
	char dir[512], path[600];
	snprintf(path, sizeof path, "%s/settings.ini", config_dir(dir, sizeof dir));
	FILE *f = fopen(path, "r");
	if (f) {
		char line[512], group[64] = "";
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (line[0] == '[') {
				snprintf(group, sizeof group, "%.*s", (int)strcspn(line + 1, "]"), line + 1);
				continue;
			}
			char *eq = strchr(line, '=');
			if (!eq)
				continue;
			*eq = 0;
			const char *key = line, *value = eq + 1;
			// QSettings may quote strings
			char unq[256];
			size_t vl = strlen(value);
			if (vl >= 2 && value[0] == '"' && value[vl - 1] == '"') {
				snprintf(unq, sizeof unq, "%.*s", (int)vl - 2, value + 1);
				value = unq;
			}
			if (strcmp(group, "Appearance") == 0) {
				if (strcmp(key, "face") == 0) parse_color(value, s->face);
				else if (strcmp(key, "activeTitle") == 0) parse_color(value, s->active_title);
				else if (strcmp(key, "activeTitle2") == 0) parse_color(value, s->active_title2);
				else if (strcmp(key, "activeTitleText") == 0) parse_color(value, s->active_title_text);
				else if (strcmp(key, "inactiveTitle") == 0) parse_color(value, s->inactive_title);
				else if (strcmp(key, "inactiveTitle2") == 0) parse_color(value, s->inactive_title2);
				else if (strcmp(key, "inactiveTitleText") == 0) parse_color(value, s->inactive_title_text);
			} else if (strcmp(group, "Keyboard") == 0) {
				if (strcmp(key, "Layout") == 0) snprintf(s->layout, sizeof s->layout, "%.63s", value);
				else if (strcmp(key, "RepeatDelay") == 0) s->repeat_delay = clampi(atoi(value), 100, 2000);
				else if (strcmp(key, "RepeatRate") == 0) s->repeat_rate = clampi(atoi(value), 1, 60);
			} else if (strcmp(group, "Mouse") == 0) {
				if (strcmp(key, "DoubleClickInterval") == 0) s->double_click_ms = clampi(atoi(value), 100, 2000);
			} else if (strcmp(group, "Touchpad") == 0) {
				if (strcmp(key, "tapToClick") == 0) s->tap_to_click = is_true(value);
				else if (strcmp(key, "naturalScroll") == 0) s->natural_scroll = is_true(value);
				else if (strcmp(key, "disableWhileTyping") == 0) s->disable_while_typing = is_true(value);
				else if (strcmp(key, "middleEmulation") == 0) s->middle_emulation = is_true(value);
				else if (strcmp(key, "speed") == 0) s->pointer_speed = clampi(atoi(value), -10, 10) / 10.0;
			}
		}
		fclose(f);
	}
	derive_edges(s);
}

// --------------------------------------------------------------- keyboards

void settings_apply_keyboard(struct server *server, struct wlr_keyboard *keyboard)
{
	struct settings *s = &server->settings;
	// "de(nodeadkeys)": layout de, variant nodeadkeys
	char layout[64] = "", variant[64] = "";
	const char *paren = strchr(s->layout, '(');
	if (paren) {
		snprintf(layout, sizeof layout, "%.*s", (int)(paren - s->layout), s->layout);
		snprintf(variant, sizeof variant, "%.*s", (int)strcspn(paren + 1, ")"), paren + 1);
	} else {
		snprintf(layout, sizeof layout, "%s", s->layout);
	}
	struct xkb_rule_names names = {
		.layout = layout[0] ? layout : NULL,
		.variant = variant[0] ? variant : NULL,
	};
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap) {
		wlr_log(WLR_ERROR, "unknown keyboard layout \"%s\"; using the default", s->layout);
		keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	}
	wlr_keyboard_set_keymap(keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(keyboard, s->repeat_rate, s->repeat_delay);
}

static void apply_keyboards(struct server *server)
{
	// the group and its members must share the keymap
	struct physical_keyboard *pk;
	wl_list_for_each(pk, &server->physical_keyboards, link)
		settings_apply_keyboard(server, pk->wlr_keyboard);
	settings_apply_keyboard(server, &server->kbd_group->keyboard);
}

// -------------------------------------------------------------- touchpads

void settings_apply_pointer(struct server *server, struct wlr_input_device *device)
{
	if (!wlr_input_device_is_libinput(device))
		return;
	struct libinput_device *d = wlr_libinput_get_device_handle(device);
	struct settings *s = &server->settings;
	if (libinput_device_config_tap_get_finger_count(d) > 0) {
		libinput_device_config_tap_set_enabled(d, s->tap_to_click ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
		if (libinput_device_config_dwt_is_available(d))
			libinput_device_config_dwt_set_enabled(d, s->disable_while_typing ? LIBINPUT_CONFIG_DWT_ENABLED
					: LIBINPUT_CONFIG_DWT_DISABLED);
		if (libinput_device_config_scroll_has_natural_scroll(d))
			libinput_device_config_scroll_set_natural_scroll_enabled(d, s->natural_scroll);
	}
	if (libinput_device_config_middle_emulation_is_available(d))
		libinput_device_config_middle_emulation_set_enabled(d, s->middle_emulation
				? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
	if (libinput_device_config_accel_is_available(d))
		libinput_device_config_accel_set_speed(d, s->pointer_speed);
}

// ---------------------------------------------------------- applying all

static void apply_all(struct server *server)
{
	apply_keyboards(server);
	struct pointer_device *pd;
	wl_list_for_each(pd, &server->pointers, link)
		settings_apply_pointer(server, pd->device);
	// frames in the new colours
	struct view *view;
	wl_list_for_each(view, &server->views, link) {
		view->frame_w = 0; // forces a redraw
		frame_update(view);
	}
}

static int settings_changed(int fd, uint32_t mask, void *data)
{
	struct server *server = data;
	char buf[4096];
	bool ours = false;
	ssize_t n;
	while ((n = read(fd, buf, sizeof buf)) > 0) {
		for (char *p = buf; p < buf + n;) {
			struct inotify_event *e = (struct inotify_event *)p;
			if (e->len && strcmp(e->name, "settings.ini") == 0)
				ours = true;
			p += sizeof(*e) + e->len;
		}
	}
	if (ours) {
		load(server);
		apply_all(server);
		wlr_log(WLR_INFO, "settings.ini changed: applied");
	}
	return 0;
}

void settings_init(struct server *server)
{
	wl_list_init(&server->physical_keyboards);
	wl_list_init(&server->pointers);
	load(server);
	char dir[512];
	config_dir(dir, sizeof dir);
	// watch the folder (created if missing, so a first save is seen too):
	// QSettings writes a new file and moves it into place
	char path[600];
	snprintf(path, sizeof path, "%s", dir);
	for (char *p = path + 1; *p; ++p)
		if (*p == '/') { *p = 0; mkdir(path, 0700); *p = '/'; }
	mkdir(path, 0700);
	server->settings_watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (server->settings_watch >= 0 &&
			inotify_add_watch(server->settings_watch, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) >= 0) {
		server->settings_source = wl_event_loop_add_fd(server->event_loop, server->settings_watch,
				WL_EVENT_READABLE, settings_changed, server);
	} else {
		wlr_log(WLR_ERROR, "can't watch %s for settings changes", dir);
	}
}
