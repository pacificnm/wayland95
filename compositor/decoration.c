// The Win95 window frame, for programs that leave decorations to the
// desktop: a raised 3-pixel border, the title bar (gradient, bold title,
// minimize/maximize/close buttons) and an invisible grab zone just outside
// the border for resizing. Drawn with cairo and pango into one buffer in
// the scene, behind the program's surfaces, and redrawn only when its size,
// title, state or the colour scheme changes.
//
// Layout, in frame coordinates (0,0 is the frame's outer corner; the
// window's content starts at FRAME_BORDER, FRAME_TOP):
//
//   ┌───────────────────────────────────────────┐ bevel: highlight, light /
//   │ ▓▓▓ Title text...          [_][□]  [X] ▓▓ │ shadow, dark (as Bevel.qml)
//   ├───────────────────────────────────────────┤
//   │                 content                   │
//   └───────────────────────────────────────────┘
#include <cairo.h>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include "server.h"

#define BUTTON_W 16
#define BUTTON_H 14
#define BUTTON_GAP 2
#define BUTTON_RIGHT 3 // from the title bar's right end
#define CORNER 16      // resizing along an edge this close to a corner takes both

// ------------------------------------------------- a cairo image as a buffer

struct frame_buffer {
	struct wlr_buffer base;
	cairo_surface_t *surface;
};

static void fb_destroy(struct wlr_buffer *wlr_buffer)
{
	struct frame_buffer *fb = wl_container_of(wlr_buffer, fb, base);
	cairo_surface_destroy(fb->surface);
	free(fb);
}

static bool fb_begin(struct wlr_buffer *wlr_buffer, uint32_t flags, void **data, uint32_t *format, size_t *stride)
{
	struct frame_buffer *fb = wl_container_of(wlr_buffer, fb, base);
	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
		return false;
	*data = cairo_image_surface_get_data(fb->surface);
	*format = DRM_FORMAT_ARGB8888; // cairo's ARGB32: premultiplied, native endian, as wl_shm's
	*stride = cairo_image_surface_get_stride(fb->surface);
	return true;
}

static void fb_end(struct wlr_buffer *wlr_buffer)
{
}

static const struct wlr_buffer_impl fb_impl = {
	.destroy = fb_destroy,
	.begin_data_ptr_access = fb_begin,
	.end_data_ptr_access = fb_end,
};

// ------------------------------------------------------------------ layout

static void frame_size(struct view *view, int *fw, int *fh)
{
	*fw = view->frame_w + 2 * FRAME_BORDER;
	*fh = view->frame_h + FRAME_TOP + FRAME_BORDER;
}

// the button's box in frame coordinates
static struct wlr_box button_box(int fw, enum frame_part part)
{
	int x = fw - FRAME_BORDER - BUTTON_RIGHT - BUTTON_W; // close
	if (part == PART_MAXIMIZE)
		x -= BUTTON_W + BUTTON_GAP;
	else if (part == PART_MINIMIZE)
		x -= 2 * (BUTTON_W + BUTTON_GAP);
	return (struct wlr_box){x, FRAME_BORDER + (FRAME_TITLE - BUTTON_H) / 2, BUTTON_W, BUTTON_H};
}

void view_frame_insets(struct view *view, int *left, int *top, int *right, int *bottom)
{
	bool on = view->ssd && !view->fullscreen;
	*left = *right = *bottom = on ? FRAME_BORDER : 0;
	*top = on ? FRAME_TOP : 0;
}

// ----------------------------------------------------------------- drawing

static void fill(cairo_t *cr, const float c[4], double x, double y, double w, double h)
{
	cairo_set_source_rgba(cr, c[0], c[1], c[2], c[3]);
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);
}

// the two-pixel Win95 bevel, as Bevel.qml draws it
static void bevel(cairo_t *cr, const struct settings *s, int x, int y, int w, int h, bool raised)
{
	const float *tl0 = raised ? s->highlight : s->shadow, *tl1 = raised ? s->light : s->dark;
	const float *br1 = raised ? s->shadow : s->light, *br0 = raised ? s->dark : s->highlight;
	fill(cr, s->face, x + 2, y + 2, w - 4, h - 4);
	fill(cr, tl0, x, y, w, 1);
	fill(cr, tl0, x, y, 1, h);
	fill(cr, tl1, x + 1, y + 1, w - 2, 1);
	fill(cr, tl1, x + 1, y + 1, 1, h - 2);
	fill(cr, br1, x + 1, y + h - 2, w - 2, 1);
	fill(cr, br1, x + w - 2, y + 1, 1, h - 2);
	fill(cr, br0, x, y + h - 1, w, 1);
	fill(cr, br0, x + w - 1, y, 1, h);
}

static void draw_glyph(cairo_t *cr, const struct settings *s, enum frame_part part, bool maximized,
		int bx, int by)
{
	const float *ink = s->dark;
	// Win95 inverts dark faces' ink (high contrast schemes)
	float lum = (s->face[0] + s->face[1] + s->face[2]) / 3.0f;
	static const float white[4] = {1, 1, 1, 1};
	if (lum < 0.25f)
		ink = white;
	switch (part) {
	case PART_CLOSE: {
		// an 8x7 X, two pixels thick
		static const char *rows[] = {
			"XX....XX", ".XX..XX.", "..XXXX..", "...XX...", "..XXXX..", ".XX..XX.", "XX....XX",
		};
		for (int r = 0; r < 7; ++r)
			for (int c = 0; c < 8; ++c)
				if (rows[r][c] == 'X')
					fill(cr, ink, bx + 4 + c, by + 3 + r, 1, 1);
		break;
	}
	case PART_MAXIMIZE:
		if (!maximized) {
			// a window: 9x9 outline with a thick top
			int x = bx + 3, y = by + 2;
			fill(cr, ink, x, y, 9, 2);
			fill(cr, ink, x, y, 1, 9);
			fill(cr, ink, x + 8, y, 1, 9);
			fill(cr, ink, x, y + 8, 9, 1);
		} else {
			// restore: two overlapping windows
			int x = bx + 5, y = by + 2;
			fill(cr, ink, x, y, 6, 2);
			fill(cr, ink, x + 5, y, 1, 6);
			fill(cr, ink, x, y, 1, 3);
			fill(cr, ink, x + 3, y + 5, 3, 1);
			x = bx + 3;
			y = by + 5;
			fill(cr, ink, x, y, 6, 2);
			fill(cr, ink, x, y, 1, 6);
			fill(cr, ink, x + 5, y, 1, 6);
			fill(cr, ink, x, y + 5, 6, 1);
		}
		break;
	case PART_MINIMIZE:
		fill(cr, ink, bx + 4, by + 9, 6, 2);
		break;
	default:
		break;
	}
}

static void draw_button(cairo_t *cr, const struct settings *s, int fw, enum frame_part part,
		bool pressed, bool maximized)
{
	struct wlr_box b = button_box(fw, part);
	bevel(cr, s, b.x, b.y, b.width, b.height, !pressed);
	int off = pressed ? 1 : 0;
	draw_glyph(cr, s, part, maximized, b.x + off, b.y + off);
}

static void draw(struct view *view, cairo_t *cr, int fw, int fh, const char *title)
{
	const struct settings *s = &view->server->settings;
	bool active = view->frame_active;

	// clear (the inside stays transparent: the program draws there)
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	// the border: bevel, then the face ring inside it
	const float *tl0 = s->highlight, *tl1 = s->light, *br1 = s->shadow, *br0 = s->dark;
	fill(cr, tl0, 0, 0, fw, 1);
	fill(cr, tl0, 0, 0, 1, fh);
	fill(cr, tl1, 1, 1, fw - 2, 1);
	fill(cr, tl1, 1, 1, 1, fh - 2);
	fill(cr, br1, 1, fh - 2, fw - 2, 1);
	fill(cr, br1, fw - 2, 1, 1, fh - 2);
	fill(cr, br0, 0, fh - 1, fw, 1);
	fill(cr, br0, fw - 1, 0, 1, fh);
	fill(cr, s->face, 2, 2, fw - 4, 1);                      // top
	fill(cr, s->face, 2, fh - 3, fw - 4, 1);                 // bottom
	fill(cr, s->face, 2, 2, 1, fh - 4);                      // left
	fill(cr, s->face, fw - 3, 2, 1, fh - 4);                 // right

	// the title bar: a gradient from left to right
	int tx = FRAME_BORDER, ty = FRAME_BORDER, tw = fw - 2 * FRAME_BORDER, th = FRAME_TITLE;
	const float *c1 = active ? s->active_title : s->inactive_title;
	const float *c2 = active ? s->active_title2 : s->inactive_title2;
	cairo_pattern_t *grad = cairo_pattern_create_linear(tx, 0, tx + tw, 0);
	cairo_pattern_add_color_stop_rgb(grad, 0, c1[0], c1[1], c1[2]);
	cairo_pattern_add_color_stop_rgb(grad, 1, c2[0], c2[1], c2[2]);
	cairo_set_source(cr, grad);
	cairo_rectangle(cr, tx, ty, tw, th);
	cairo_fill(cr);
	cairo_pattern_destroy(grad);

	// the buttons
	enum frame_part pressed = view->frame_pressed;
	draw_button(cr, s, fw, PART_MINIMIZE, pressed == PART_MINIMIZE, false);
	draw_button(cr, s, fw, PART_MAXIMIZE, pressed == PART_MAXIMIZE, view->maximized);
	draw_button(cr, s, fw, PART_CLOSE, pressed == PART_CLOSE, false);

	// the title, bold, cut short with "..." before the buttons
	int text_x = tx + 4;
	int text_w = button_box(fw, PART_MINIMIZE).x - 4 - text_x;
	if (title && *title && text_w > 8) {
		cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
		PangoLayout *layout = pango_cairo_create_layout(cr);
		PangoFontDescription *font = pango_font_description_new();
		pango_font_description_set_family(font, s->font);
		pango_font_description_set_weight(font, PANGO_WEIGHT_BOLD);
		pango_font_description_set_absolute_size(font, 11 * PANGO_SCALE);
		pango_layout_set_font_description(layout, font);
		pango_layout_set_text(layout, title, -1);
		pango_layout_set_width(layout, text_w * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		pango_layout_set_single_paragraph_mode(layout, TRUE);
		int lw, lh;
		pango_layout_get_pixel_size(layout, &lw, &lh);
		const float *tc = active ? s->active_title_text : s->inactive_title_text;
		cairo_set_source_rgb(cr, tc[0], tc[1], tc[2]);
		cairo_move_to(cr, text_x, ty + (th - lh) / 2);
		pango_cairo_show_layout(cr, layout);
		pango_font_description_free(font);
		g_object_unref(layout);
	}
}

// presses inside the frame's hole belong to whatever is behind it
static bool frame_accepts_input(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	if (!buffer->buffer)
		return false;
	int fw = buffer->buffer->width, fh = buffer->buffer->height;
	bool inside = *sx >= FRAME_BORDER && *sx < fw - FRAME_BORDER && *sy >= FRAME_TOP && *sy < fh - FRAME_BORDER;
	return !inside;
}

// ------------------------------------------------------------ create, update

void frame_destroy(struct view *view)
{
	if (view->frame_tree)
		wlr_scene_node_destroy(&view->frame_tree->node);
	view->frame_tree = NULL;
	view->frame = NULL;
	view->resize_zone = NULL;
	free(view->frame_title);
	view->frame_title = NULL;
	view->frame_w = view->frame_h = 0;
}

void frame_update(struct view *view)
{
#ifdef WAYLAND95_TEST_BREAK_SSD // tests/selftest.sh: proves the frame tests catch it
	view->ssd = false;
#endif
	if (!view->ssd || !view->scene_tree) {
		if (view->frame_tree)
			frame_destroy(view);
		return;
	}
	if (!view->frame_tree) {
		view->frame_tree = wlr_scene_tree_create(view->scene_tree);
		wlr_scene_node_lower_to_bottom(&view->frame_tree->node); // behind the program
		wlr_scene_node_set_position(&view->frame_tree->node, -FRAME_BORDER, -FRAME_TOP);
		static const float clear[4] = {0, 0, 0, 0};
		view->resize_zone = wlr_scene_rect_create(view->frame_tree, 1, 1, clear);
		wlr_scene_node_set_position(&view->resize_zone->node, -FRAME_RESIZE_MARGIN, -FRAME_RESIZE_MARGIN);
		view->frame = wlr_scene_buffer_create(view->frame_tree, NULL);
		view->frame->point_accepts_input = frame_accepts_input;
		view->frame_w = view->frame_h = 0;
	}
	wlr_scene_node_set_enabled(&view->frame_tree->node, !view->fullscreen);
	if (view->fullscreen)
		return;

	struct wlr_box geo;
	view_geometry(view, &geo);
	if (geo.width <= 0 || geo.height <= 0)
		return;
	const char *title = view_title(view);
	bool same = geo.width == view->frame_w && geo.height == view->frame_h
			&& view->frame_drawn_active == view->frame_active
			&& view->frame_drawn_pressed == view->frame_pressed
			&& view->frame_maximized == view->maximized
			&& view->frame_title && strcmp(view->frame_title, title) == 0;
	if (same)
		return;
	view->frame_w = geo.width;
	view->frame_h = geo.height;
	view->frame_drawn_active = view->frame_active;
	view->frame_drawn_pressed = view->frame_pressed;
	view->frame_maximized = view->maximized;
	free(view->frame_title);
	view->frame_title = strdup(title);

	int fw, fh;
	frame_size(view, &fw, &fh);
	// no resizing from the edges of a maximized window
	wlr_scene_node_set_enabled(&view->resize_zone->node, !view->maximized);
	wlr_scene_rect_set_size(view->resize_zone, fw + 2 * FRAME_RESIZE_MARGIN, fh + 2 * FRAME_RESIZE_MARGIN);

	struct frame_buffer *fb = calloc(1, sizeof(*fb));
	fb->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, fw, fh);
	if (cairo_surface_status(fb->surface) != CAIRO_STATUS_SUCCESS) {
		wlr_log(WLR_ERROR, "can't draw a %dx%d frame", fw, fh);
		cairo_surface_destroy(fb->surface);
		free(fb);
		return;
	}
	wlr_buffer_init(&fb->base, &fb_impl, fw, fh);
	cairo_t *cr = cairo_create(fb->surface);
	draw(view, cr, fw, fh, title);
	cairo_destroy(cr);
	cairo_surface_flush(fb->surface);
	wlr_scene_buffer_set_buffer(view->frame, &fb->base);
	wlr_buffer_drop(&fb->base); // the scene holds it now
}

void frame_set_pressed(struct view *view, enum frame_part part)
{
	if (view->frame_pressed == part)
		return;
	view->frame_pressed = part;
	frame_update(view);
}

// ------------------------------------------------------------ hit testing

enum frame_part frame_part_at(struct view *view, double lx, double ly, uint32_t *edges)
{
	*edges = WLR_EDGE_NONE;
	if (!view->ssd || view->fullscreen || view->frame_w <= 0)
		return PART_NONE;
	int fw, fh;
	frame_size(view, &fw, &fh);
	// frame coordinates
	double fx = lx - (view->x - FRAME_BORDER), fy = ly - (view->y - FRAME_TOP);
	int m = view->maximized ? 0 : FRAME_RESIZE_MARGIN;
	if (fx < -m || fy < -m || fx >= fw + m || fy >= fh + m)
		return PART_NONE;

	// the title bar and its buttons
	if (fx >= FRAME_BORDER && fx < fw - FRAME_BORDER && fy >= FRAME_BORDER && fy < FRAME_BORDER + FRAME_TITLE) {
		static const enum frame_part buttons[] = {PART_CLOSE, PART_MAXIMIZE, PART_MINIMIZE};
		for (int i = 0; i < 3; ++i) {
			struct wlr_box b = button_box(fw, buttons[i]);
			if (fx >= b.x && fx < b.x + b.width && fy >= b.y && fy < b.y + b.height)
				return buttons[i];
		}
		return PART_TITLE;
	}
	if (view->maximized)
		return PART_TITLE; // the border of a maximized window: nothing to resize

	bool left = fx < FRAME_BORDER, right = fx >= fw - FRAME_BORDER;
	bool top = fy < FRAME_BORDER, bottom = fy >= fh - FRAME_BORDER;
	if (!left && !right && !top && !bottom)
		return PART_NONE; // inside (the program's hole)
	// near a corner, an edge resizes both ways
	if (top || bottom) {
		left = left || fx < CORNER;
		right = right || fx >= fw - CORNER;
	}
	if (left || right) {
		top = top || fy < CORNER;
		bottom = bottom || fy >= fh - CORNER;
	}
	*edges = (left ? WLR_EDGE_LEFT : 0) | (right ? WLR_EDGE_RIGHT : 0)
			| (top ? WLR_EDGE_TOP : 0) | (bottom ? WLR_EDGE_BOTTOM : 0);
	return PART_EDGE;
}

const char *frame_cursor(enum frame_part part, uint32_t edges)
{
	if (part != PART_EDGE)
		return "default";
	switch (edges) {
	case WLR_EDGE_TOP: return "n-resize";
	case WLR_EDGE_BOTTOM: return "s-resize";
	case WLR_EDGE_LEFT: return "w-resize";
	case WLR_EDGE_RIGHT: return "e-resize";
	case WLR_EDGE_TOP | WLR_EDGE_LEFT: return "nw-resize";
	case WLR_EDGE_TOP | WLR_EDGE_RIGHT: return "ne-resize";
	case WLR_EDGE_BOTTOM | WLR_EDGE_LEFT: return "sw-resize";
	case WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT: return "se-resize";
	default: return "default";
	}
}
