/* Northstar — GTK view backed by the internal renderer protocol.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NORTHSTAR_GTK_PROCVIEW_H
#define NORTHSTAR_GTK_PROCVIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct NsProcView NsProcView;

const char *ns_app_self_exe(void);

/* Re-present a popover menu once its sections have been built, so it takes
   the height its items need instead of scrolling the last one. Call once,
   at construction. */
void ns_popover_menu_fit(GtkWidget *popover);

typedef enum {
    NS_PROC_EVT_TITLE,
    NS_PROC_EVT_URL,
    NS_PROC_EVT_STATUS,
    NS_PROC_EVT_HISTORY,
    NS_PROC_EVT_LOADING,
    NS_PROC_EVT_DOWNLOAD
} NsProcEvent;

typedef void (*NsProcNotify)(NsProcView *view, NsProcEvent evt,
                             const char *text, gpointer user_data);

/* Resolved path to the renderer executable (NS_RENDERER, else discovered next
 * to the running binary). Newly allocated; free with g_free. */
char *ns_proc_renderer_path(void);

NsProcView *ns_proc_view_new(void);
GtkWidget  *ns_proc_view_widget(NsProcView *view);
void        ns_proc_view_set_notify(NsProcView *view, NsProcNotify cb,
                                    gpointer user_data);

/* Mark the renderer session as private/incognito before the first load. */
void        ns_proc_view_set_private(NsProcView *view, gboolean private_mode);
gboolean    ns_proc_view_is_private(NsProcView *view);

void ns_proc_view_load(NsProcView *view, const char *url);
void ns_proc_view_back(NsProcView *view);
void ns_proc_view_forward(NsProcView *view);
void ns_proc_view_reload(NsProcView *view);
void ns_proc_view_stop(NsProcView *view);
void ns_proc_view_toggle_console(NsProcView *view);

gboolean    ns_proc_view_can_back(NsProcView *view);
gboolean    ns_proc_view_can_forward(NsProcView *view);
const char *ns_proc_view_url(NsProcView *view);
const char *ns_proc_view_title(NsProcView *view);
gboolean    ns_proc_view_is_loading(NsProcView *view);

/* Connection security of the current page (ns_security in net.h:
   0 none, 1 secure, 2 invalid, 3 plain) and the server IP (owned by the view,
   or NULL). */
int         ns_proc_view_security(NsProcView *view);
const char *ns_proc_view_remote_ip(NsProcView *view);

/* Task-manager support for the renderer session. */
int         ns_proc_view_renderer_pid(NsProcView *view);
void        ns_proc_view_end_task(NsProcView *view);

/* Ask the user for a destination and write the current page there. */
void   ns_proc_view_save_pdf(NsProcView *view);
void   ns_proc_view_save_image(NsProcView *view);

void   ns_proc_view_zoom_in(NsProcView *view);
void   ns_proc_view_zoom_out(NsProcView *view);
void   ns_proc_view_zoom_reset(NsProcView *view);
void   ns_proc_view_focus(NsProcView *view);
void   ns_proc_view_find_open(NsProcView *view);


G_END_DECLS

#endif
