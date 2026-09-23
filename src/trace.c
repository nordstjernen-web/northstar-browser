/* Northstar — writes a Chrome trace-event JSON array that Perfetto can open.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "trace.h"

#include <stdio.h>
#include <glib/gstdio.h>

static FILE   *g_trace_file;
static GMutex  g_trace_lock;
static gint    g_trace_next_tid;
static GPrivate g_trace_tid;
static gint64  g_trace_origin_us;

gboolean
ns_trace_enabled(void)
{
    return g_atomic_pointer_get(&g_trace_file) != NULL;
}

gint64
ns_trace_now(void)
{
    return ns_trace_enabled() ? g_get_monotonic_time() : 0;
}

static int
trace_tid(void)
{
    int tid = GPOINTER_TO_INT(g_private_get(&g_trace_tid));
    if (tid == 0) {
        tid = g_atomic_int_add(&g_trace_next_tid, 1) + 1;
        g_private_set(&g_trace_tid, GINT_TO_POINTER(tid));
    }
    return tid;
}

static void
trace_append_json_string(GString *out, const char *s)
{
    g_string_append_c(out, '"');
    for (const unsigned char *p = (const unsigned char *)s; p && *p; p++) {
        if (*p == '"' || *p == '\\')
            g_string_append_printf(out, "\\%c", *p);
        else if (*p < 0x20)
            g_string_append_printf(out, "\\u%04x", *p);
        else
            g_string_append_c(out, (char)*p);
    }
    g_string_append_c(out, '"');
}

static void
trace_write(GString *event)
{
    g_mutex_lock(&g_trace_lock);
    if (g_trace_file) {
        fputs(event->str, g_trace_file);
        fputs(",\n", g_trace_file);
        fflush(g_trace_file);
    }
    g_mutex_unlock(&g_trace_lock);
}

gboolean
ns_trace_open(const char *path)
{
    if (!path || !*path) return FALSE;
    FILE *f = g_fopen(path, "w");
    if (!f) return FALSE;
    g_trace_origin_us = g_get_monotonic_time();
    fputs("[\n", f);
    g_mutex_lock(&g_trace_lock);
    g_atomic_pointer_set(&g_trace_file, f);
    g_mutex_unlock(&g_trace_lock);
    ns_trace_thread_name("main");
    return TRUE;
}

void
ns_trace_close(void)
{
    g_mutex_lock(&g_trace_lock);
    FILE *f = g_trace_file;
    g_atomic_pointer_set(&g_trace_file, NULL);
    g_mutex_unlock(&g_trace_lock);
    if (!f) return;
    fputs("{}]\n", f);
    fclose(f);
}

void
ns_trace_thread_name(const char *name)
{
    if (!ns_trace_enabled() || !name) return;
    GString *e = g_string_new("{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,");
    g_string_append_printf(e, "\"tid\":%d,\"args\":{\"name\":", trace_tid());
    trace_append_json_string(e, name);
    g_string_append(e, "}}");
    trace_write(e);
    g_string_free(e, TRUE);
}

void
ns_trace_complete(const char *category, const char *name, gint64 start_us,
                  const char *detail)
{
    if (!ns_trace_enabled() || !name || start_us <= 0) return;
    gint64 end_us = g_get_monotonic_time();
    GString *e = g_string_new("{\"name\":");
    trace_append_json_string(e, name);
    g_string_append(e, ",\"cat\":");
    trace_append_json_string(e, category ? category : "engine");
    g_string_append_printf(e,
        ",\"ph\":\"X\",\"ts\":%" G_GINT64_FORMAT ",\"dur\":%" G_GINT64_FORMAT
        ",\"pid\":1,\"tid\":%d",
        start_us - g_trace_origin_us, MAX(end_us - start_us, (gint64)0),
        trace_tid());
    if (detail && *detail) {
        g_string_append(e, ",\"args\":{\"detail\":");
        trace_append_json_string(e, detail);
        g_string_append_c(e, '}');
    }
    g_string_append_c(e, '}');
    trace_write(e);
    g_string_free(e, TRUE);
}
