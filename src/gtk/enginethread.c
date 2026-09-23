/* Northstar — the thread the page engine runs on, with its own main context.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "enginethread.h"
#include "mainctx.h"
#include "trace.h"

typedef struct {
    NsEngineJob fn;
    gpointer    data;
} Job;

static GMainContext *g_ctx;
static GAsyncQueue  *g_jobs;

static gboolean
jobs_ready(void)
{
    return g_main_depth() == 0 && g_async_queue_length(g_jobs) > 0;
}

static gboolean
jobs_prepare(GSource *source, gint *timeout)
{
    (void)source;
    *timeout = -1;
    return jobs_ready();
}

static gboolean
jobs_check(GSource *source)
{
    (void)source;
    return jobs_ready();
}

static gboolean
jobs_dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
    (void)source;
    (void)callback;
    (void)user_data;
    Job *job;
    while ((job = g_async_queue_try_pop(g_jobs))) {
        job->fn(job->data);
        g_free(job);
    }
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs jobs_funcs = {
    .prepare = jobs_prepare,
    .check = jobs_check,
    .dispatch = jobs_dispatch,
};

static gpointer
engine_main(gpointer data)
{
    (void)data;
    g_main_context_push_thread_default(g_ctx);
    ns_engine_context_set(g_ctx);
    ns_trace_thread_name("ns-engine");
    GSource *src = g_source_new(&jobs_funcs, sizeof(GSource));
    g_source_attach(src, g_ctx);
    g_source_unref(src);
    GMainLoop *loop = g_main_loop_new(g_ctx, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    g_main_context_pop_thread_default(g_ctx);
    return NULL;
}

static void
engine_start(void)
{
    static gsize started;
    if (g_once_init_enter(&started)) {
        g_ctx = g_main_context_new();
        g_jobs = g_async_queue_new();
        g_thread_unref(g_thread_new("ns-engine", engine_main, NULL));
        g_once_init_leave(&started, 1);
    }
}

void
ns_engine_thread_post(NsEngineJob fn, gpointer data)
{
    engine_start();
    Job *job = g_new0(Job, 1);
    job->fn = fn;
    job->data = data;
    g_async_queue_push(g_jobs, job);
    g_main_context_wakeup(g_ctx);
}
