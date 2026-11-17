// xdg-dialog-v1: programs say which of their windows are dialogs, and
// which of those are modal (their owner takes no input while they're up).
// A modal dialog gets the keyboard whenever its owner is brought forward
// (view_focus), as in Windows 95: clicking a window that's waiting on a
// dialog brings the dialog up, ready to answer.
#include "server.h"

#include <stdlib.h>

#include "xdg-dialog-v1-protocol.h"

struct dialog {
	struct wl_resource *resource;
	struct wlr_xdg_toplevel *toplevel;
	struct wl_listener toplevel_destroy;
};

static void set_modal(struct dialog *dialog, bool modal)
{
	struct view *view = view_from_xdg_toplevel(dialog->toplevel);
	if (view)
		view->modal = modal;
}

static void dialog_forget_toplevel(struct dialog *dialog)
{
	if (!dialog->toplevel)
		return;
	wl_list_remove(&dialog->toplevel_destroy.link);
	dialog->toplevel = NULL;
}

static void handle_toplevel_destroy(struct wl_listener *listener, void *data)
{
	struct dialog *dialog = wl_container_of(listener, dialog, toplevel_destroy);
	dialog_forget_toplevel(dialog);
}

static void dialog_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}
static void dialog_set_modal(struct wl_client *client, struct wl_resource *resource)
{
	set_modal(wl_resource_get_user_data(resource), true);
}
static void dialog_unset_modal(struct wl_client *client, struct wl_resource *resource)
{
	set_modal(wl_resource_get_user_data(resource), false);
}
static const struct xdg_dialog_v1_interface dialog_impl = {
	.destroy = dialog_destroy,
	.set_modal = dialog_set_modal,
	.unset_modal = dialog_unset_modal,
};

static void dialog_resource_destroy(struct wl_resource *resource)
{
	struct dialog *dialog = wl_resource_get_user_data(resource);
	set_modal(dialog, false); // (no longer a dialog: an ordinary window)
	dialog_forget_toplevel(dialog);
	free(dialog);
}

static void wm_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void wm_get_xdg_dialog(struct wl_client *client, struct wl_resource *resource, uint32_t id,
		struct wl_resource *toplevel_resource)
{
	struct wlr_xdg_toplevel *toplevel = wlr_xdg_toplevel_from_resource(toplevel_resource);
	struct dialog *dialog = calloc(1, sizeof(*dialog));
	if (!dialog) {
		wl_client_post_no_memory(client);
		return;
	}
	dialog->resource = wl_resource_create(client, &xdg_dialog_v1_interface, wl_resource_get_version(resource), id);
	if (!dialog->resource) {
		free(dialog);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(dialog->resource, &dialog_impl, dialog, dialog_resource_destroy);
	if (toplevel) {
		dialog->toplevel = toplevel;
		dialog->toplevel_destroy.notify = handle_toplevel_destroy;
		wl_signal_add(&toplevel->events.destroy, &dialog->toplevel_destroy);
	}
}

static const struct xdg_wm_dialog_v1_interface wm_impl = {
	.destroy = wm_destroy,
	.get_xdg_dialog = wm_get_xdg_dialog,
};

static void wm_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client, &xdg_wm_dialog_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &wm_impl, data, NULL);
}

void dialog_init(struct server *server)
{
	wl_global_create(server->display, &xdg_wm_dialog_v1_interface, 1, server, wm_bind);
}
