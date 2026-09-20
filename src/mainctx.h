/* Northstar — the GLib main context the page engine runs on.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_MAINCTX_H
#define NS_MAINCTX_H

#include <glib.h>

void          ns_engine_context_set(GMainContext *ctx);
GMainContext *ns_engine_context(void);
guint         ns_engine_timeout_add(guint ms, GSourceFunc fn, gpointer data);
guint         ns_engine_idle_add(GSourceFunc fn, gpointer data);
void          ns_engine_source_remove(guint id);

#endif
