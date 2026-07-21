/* Northstar — GTK single-page browser window over the renderer protocol.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NORTHSTAR_GTK_PROCWINDOW_H
#define NORTHSTAR_GTK_PROCWINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Run the single-page GTK browser. Owns its own
 * GtkApplication; returns the application's exit status. When run under the
 * watchdog supervisor, session_path is where the window records its current
 * URL (NULL when unsupervised), and recover requests reopening that page
 * after a crash. */
int ns_procapp_run(const char *startup_url, const char *session_path,
                   gboolean recover, gboolean private_mode);

/* Request a fixed initial window size instead of the maximized default. */
void ns_procapp_set_window_size(int width, int height);

G_END_DECLS

#endif
