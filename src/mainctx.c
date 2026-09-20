/* Northstar — the GLib main context the page engine runs on.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mainctx.h"

static GMainContext *g_engine_ctx;

void
ns_engine_context_set(GMainContext *ctx)
{
    g_engine_ctx = ctx;
}

GMainContext *
ns_engine_context(void)
{
    GMainContext *ctx = g_main_context_get_thread_default();
    return ctx ? ctx : g_engine_ctx;
}

static guint
attach(GSource *src, GSourceFunc fn, gpointer data)
{
    g_source_set_callback(src, fn, data, NULL);
    guint id = g_source_attach(src, ns_engine_context());
    g_source_unref(src);
    return id;
}

guint
ns_engine_timeout_add(guint ms, GSourceFunc fn, gpointer data)
{
    return attach(g_timeout_source_new(ms), fn, data);
}

guint
ns_engine_idle_add(GSourceFunc fn, gpointer data)
{
    return attach(g_idle_source_new(), fn, data);
}

void
ns_engine_source_remove(guint id)
{
    if (!id) return;
    GSource *src = g_main_context_find_source_by_id(ns_engine_context(), id);
    if (src) g_source_destroy(src);
}
