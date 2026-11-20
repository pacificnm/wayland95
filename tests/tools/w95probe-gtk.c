// w95probe-gtk: a GTK 3 window for the tests (tests/run.sh) that reports
// everything it receives, one "EVT ..." line each on stdout:
//
//   EVT ready                     shown and drawn
//   EVT press BUTTON X Y          (also release), in the window's own pixels
//   EVT scroll DX DY | up | down  the wheel
//   EVT key NAME                  a key press
//   EVT focus in|out              keyboard focus
//   EVT size W H                  its size changed (maximized, resized)
//   EVT state maximized|...       window state changes
//   EVT menu ITEM                 an item of its right-click menu was chosen
//   EVT clipboard TEXT            answer to "paste" (also: primary TEXT)
//
// and takes commands on stdin, one per line:
//   copy TEXT       put TEXT on the clipboard
//   select TEXT     put TEXT in the primary selection (middle-click paste)
//   paste           report the clipboard; "paste-primary" the primary selection
//   menu            open the right-click menu at the last press
//
//   w95probe-gtk [--title T] [--color RRGGBB] [--size WxH] [--headerbar]
// (--headerbar: its own title bar, a GtkHeaderBar, as GNOME programs have;
// --stripes: one-pixel black and white columns, to see whether it's shown
// pixel for pixel)
// Runs on Wayland, or X11 with GDK_BACKEND=x11.
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

static GtkWidget *menu;
static GdkEvent *last_press;
static GdkRGBA color = {0.2, 0.6, 0.2, 1};
static gboolean stripes;

static void out(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("EVT ", stdout);
	vprintf(fmt, ap);
	fputc('\n', stdout);
	fflush(stdout);
	va_end(ap);
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
	gdk_cairo_set_source_rgba(cr, &color);
	cairo_paint(cr);
	if (stripes) {
		int width = gtk_widget_get_allocated_width(w), height = gtk_widget_get_allocated_height(w);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_paint(cr);
		cairo_set_source_rgb(cr, 0, 0, 0);
		for (int x = 0; x < width; x += 2)
			cairo_rectangle(cr, x, 0, 1, height);
		cairo_fill(cr);
	}
	return FALSE;
}

static gboolean on_press(GtkWidget *w, GdkEventButton *e, gpointer data)
{
	if (e->type != GDK_BUTTON_PRESS)
		return FALSE; // double clicks show up as two presses too
	out("press %u %d %d", e->button, (int)e->x, (int)e->y);
	if (last_press)
		gdk_event_free(last_press);
	last_press = gdk_event_copy((GdkEvent *)e);
	if (e->button == 3)
		gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)e);
	return TRUE;
}

static gboolean on_release(GtkWidget *w, GdkEventButton *e, gpointer data)
{
	out("release %u %d %d", e->button, (int)e->x, (int)e->y);
	return TRUE;
}

static gboolean on_scroll(GtkWidget *w, GdkEventScroll *e, gpointer data)
{
	switch (e->direction) {
	case GDK_SCROLL_UP: out("scroll up"); break;
	case GDK_SCROLL_DOWN: out("scroll down"); break;
	case GDK_SCROLL_SMOOTH: out("scroll %.1f %.1f", e->delta_x, e->delta_y); break;
	default: out("scroll other"); break;
	}
	return TRUE;
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer data)
{
	out("key %s", gdk_keyval_name(e->keyval));
	return TRUE;
}

static gboolean on_focus(GtkWidget *w, GdkEventFocus *e, gpointer data)
{
	out("focus %s", e->in ? "in" : "out");
	return FALSE;
}

static gboolean on_configure(GtkWidget *w, GdkEventConfigure *e, gpointer data)
{
	static int last_w, last_h;
	if (e->width != last_w || e->height != last_h) {
		last_w = e->width;
		last_h = e->height;
		out("size %d %d", e->width, e->height);
	}
	return FALSE;
}

static gboolean on_state(GtkWidget *w, GdkEventWindowState *e, gpointer data)
{
	GdkWindowState s = e->new_window_state;
	out("state%s%s%s%s", s & GDK_WINDOW_STATE_MAXIMIZED ? " maximized" : "",
			s & GDK_WINDOW_STATE_FULLSCREEN ? " fullscreen" : "",
			s & GDK_WINDOW_STATE_FOCUSED ? " focused" : "",
			s & GDK_WINDOW_STATE_ICONIFIED ? " minimized" : "");
	return FALSE;
}

static void on_item(GtkMenuItem *item, gpointer data)
{
	out("menu %s", gtk_menu_item_get_label(item));
}

static void on_text(GtkClipboard *cb, const gchar *text, gpointer data)
{
	out("%s %s", (const char *)data, text ? text : "(empty)");
}

static gboolean on_stdin(GIOChannel *ch, GIOCondition cond, gpointer data)
{
	gchar *line = NULL;
	gsize len;
	if (g_io_channel_read_line(ch, &line, &len, NULL, NULL) != G_IO_STATUS_NORMAL || !line) {
		gtk_main_quit();
		return FALSE;
	}
	g_strchomp(line);
	if (g_str_has_prefix(line, "copy ")) {
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), line + 5, -1);
		out("copied");
	} else if (g_str_has_prefix(line, "select ")) {
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), line + 7, -1);
		out("selected");
	} else if (strcmp(line, "paste") == 0) {
		gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), on_text, "clipboard");
	} else if (strcmp(line, "paste-primary") == 0) {
		gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), on_text, "primary");
	} else if (strcmp(line, "menu") == 0 && last_press) {
		gtk_menu_popup_at_pointer(GTK_MENU(menu), last_press);
	}
	g_free(line);
	return TRUE;
}

static gboolean report_ready(gpointer data)
{
	out("ready");
	return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[])
{
	const char *title = "w95probe-gtk";
	int width = 400, height = 300;
	gboolean headerbar = FALSE;
	gtk_init(&argc, &argv);
	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--headerbar") == 0)
			headerbar = TRUE;
		else if (strcmp(argv[i], "--stripes") == 0)
			stripes = TRUE;
		else if (i + 1 >= argc)
			break;
		else if (strcmp(argv[i], "--title") == 0)
			title = argv[++i];
		else if (strcmp(argv[i], "--color") == 0)
			gdk_rgba_parse(&color, g_strdup_printf("#%s", argv[++i]));
		else if (strcmp(argv[i], "--size") == 0)
			sscanf(argv[++i], "%dx%d", &width, &height);
	}

	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), title);
	gtk_window_set_default_size(GTK_WINDOW(window), width, height);
	if (headerbar) {
		GtkWidget *bar = gtk_header_bar_new();
		gtk_header_bar_set_title(GTK_HEADER_BAR(bar), title);
		gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(bar), TRUE);
		gtk_window_set_titlebar(GTK_WINDOW(window), bar);
	}
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(window, "configure-event", G_CALLBACK(on_configure), NULL);
	g_signal_connect(window, "window-state-event", G_CALLBACK(on_state), NULL);
	g_signal_connect(window, "focus-in-event", G_CALLBACK(on_focus), NULL);
	g_signal_connect(window, "focus-out-event", G_CALLBACK(on_focus), NULL);
	g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), NULL);

	GtkWidget *area = gtk_drawing_area_new();
	gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK);
	g_signal_connect(area, "draw", G_CALLBACK(on_draw), NULL);
	g_signal_connect(area, "button-press-event", G_CALLBACK(on_press), NULL);
	g_signal_connect(area, "button-release-event", G_CALLBACK(on_release), NULL);
	g_signal_connect(area, "scroll-event", G_CALLBACK(on_scroll), NULL);
	gtk_container_add(GTK_CONTAINER(window), area);

	menu = gtk_menu_new();
	const char *items[] = {"First", "Second", "Third", "Fourth", "Fifth", "Sixth"};
	for (size_t i = 0; i < G_N_ELEMENTS(items); ++i) {
		GtkWidget *item = gtk_menu_item_new_with_label(items[i]);
		g_signal_connect(item, "activate", G_CALLBACK(on_item), NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	gtk_widget_show_all(menu);

	GIOChannel *in = g_io_channel_unix_new(0);
	g_io_add_watch(in, G_IO_IN | G_IO_HUP, on_stdin, NULL);

	gtk_widget_show_all(window);
	g_signal_connect_swapped(window, "map-event", G_CALLBACK(report_ready), NULL);
	gtk_main();
	return 0;
}
