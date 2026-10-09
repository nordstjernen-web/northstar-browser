/* Northstar — a video decoder running on its own thread, a few frames ahead.
 * Copyright 2026 Gabriel Ferreira
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "videoworker.h"

#include <math.h>

static const double NS_VIDEO_WORKER_EPSILON_S = 1e-6;
enum { NS_VIDEO_WORKER_DEFAULT_PICTURES = 4 };

typedef struct {
    GBytes  *sample;
    GBytes  *config;
    double   pts;
    gboolean want_texture;
    guint    epoch;
} worker_job;

typedef struct {
    double      pts;
    ns_texture *texture;
} worker_picture;

struct ns_video_worker {
    ns_video_decoder *decoder;
    GThread          *thread;
    GMutex            lock;
    GCond             wake;
    GQueue            jobs;
    GArray           *pictures;
    guint             epoch;
    guint             flushed_epoch;
    guint             max_pictures;
    gboolean          decoding;
    double            decoding_pts;
    gboolean          decoding_wanted;
    gboolean          quit;
};

static void
job_free(worker_job *job)
{
    if (job->sample) g_bytes_unref(job->sample);
    if (job->config) g_bytes_unref(job->config);
    g_free(job);
}

static void
picture_clear(gpointer data)
{
    worker_picture *picture = data;
    ns_texture_unref(picture->texture);
}

static void
drop_jobs(ns_video_worker *worker)
{
    worker_job *job;
    while ((job = g_queue_pop_head(&worker->jobs)) != NULL) job_free(job);
}

static void
decode_job(ns_video_worker *worker, worker_job *job, gboolean flush)
{
    if (flush) ns_video_decoder_flush(worker->decoder);
    gint64 stamp = (gint64)llround(job->pts * 1e6);
    ns_texture *texture = ns_video_decoder_decode(worker->decoder, job->sample,
                                                  job->config, stamp,
                                                  job->want_texture);
    g_mutex_lock(&worker->lock);
    worker->decoding = FALSE;
    if (job->want_texture && job->epoch == worker->epoch) {
        worker_picture picture = { job->pts, texture };
        g_array_append_val(worker->pictures, picture);
    } else {
        ns_texture_unref(texture);
    }
    g_mutex_unlock(&worker->lock);
}

/* A picture is decoded only while fewer than max_pictures wait to be shown,
 * however many coded frames are queued ahead. */
static gboolean
worker_may_decode(ns_video_worker *worker)
{
    const worker_job *job = g_queue_peek_head(&worker->jobs);
    if (!job) return FALSE;
    return !job->want_texture || job->epoch != worker->epoch ||
           worker->pictures->len < worker->max_pictures;
}

static gpointer
worker_main(gpointer data)
{
    ns_video_worker *worker = data;
    g_mutex_lock(&worker->lock);
    while (!worker->quit) {
        if (!worker_may_decode(worker)) {
            g_cond_wait(&worker->wake, &worker->lock);
            continue;
        }
        worker_job *job = g_queue_pop_head(&worker->jobs);
        if (job->epoch != worker->epoch) {
            job_free(job);
            continue;
        }
        gboolean flush = worker->flushed_epoch != worker->epoch;
        worker->flushed_epoch = worker->epoch;
        worker->decoding = TRUE;
        worker->decoding_pts = job->pts;
        worker->decoding_wanted = job->want_texture;
        g_mutex_unlock(&worker->lock);
        decode_job(worker, job, flush);
        job_free(job);
        g_mutex_lock(&worker->lock);
    }
    g_mutex_unlock(&worker->lock);
    return NULL;
}

ns_video_worker *
ns_video_worker_new(ns_video_decoder *decoder)
{
    if (!decoder) return NULL;
    ns_video_worker *worker = g_new0(ns_video_worker, 1);
    worker->decoder = decoder;
    g_mutex_init(&worker->lock);
    g_cond_init(&worker->wake);
    g_queue_init(&worker->jobs);
    worker->max_pictures = NS_VIDEO_WORKER_DEFAULT_PICTURES;
    worker->pictures = g_array_new(FALSE, FALSE, sizeof(worker_picture));
    g_array_set_clear_func(worker->pictures, picture_clear);
    worker->thread = g_thread_new("ns-video-decode", worker_main, worker);
    return worker;
}

void
ns_video_worker_free(ns_video_worker *worker)
{
    if (!worker) return;
    g_mutex_lock(&worker->lock);
    worker->quit = TRUE;
    drop_jobs(worker);
    g_cond_signal(&worker->wake);
    g_mutex_unlock(&worker->lock);
    g_thread_join(worker->thread);
    ns_video_decoder_free(worker->decoder);
    g_array_free(worker->pictures, TRUE);
    g_cond_clear(&worker->wake);
    g_mutex_clear(&worker->lock);
    g_free(worker);
}

void
ns_video_worker_restart(ns_video_worker *worker)
{
    if (!worker) return;
    g_mutex_lock(&worker->lock);
    worker->epoch++;
    drop_jobs(worker);
    g_array_set_size(worker->pictures, 0);
    g_cond_signal(&worker->wake);
    g_mutex_unlock(&worker->lock);
}

void
ns_video_worker_push(ns_video_worker *worker, GBytes *sample, GBytes *config,
                     double pts, gboolean want_texture)
{
    if (!worker || !sample) return;
    worker_job *job = g_new0(worker_job, 1);
    job->sample = g_bytes_ref(sample);
    job->config = config ? g_bytes_ref(config) : NULL;
    job->pts = pts;
    job->want_texture = want_texture;
    g_mutex_lock(&worker->lock);
    job->epoch = worker->epoch;
    g_queue_push_tail(&worker->jobs, job);
    g_cond_signal(&worker->wake);
    g_mutex_unlock(&worker->lock);
}

void
ns_video_worker_set_max_pictures(ns_video_worker *worker, guint max_pictures)
{
    if (!worker || max_pictures < 1) return;
    g_mutex_lock(&worker->lock);
    if (worker->max_pictures != max_pictures) {
        worker->max_pictures = max_pictures;
        g_cond_signal(&worker->wake);
    }
    g_mutex_unlock(&worker->lock);
}

gboolean
ns_video_worker_expects(ns_video_worker *worker, double pts)
{
    if (!worker) return FALSE;
    g_mutex_lock(&worker->lock);
    gboolean found = worker->decoding && worker->decoding_wanted &&
                     fabs(worker->decoding_pts - pts) < NS_VIDEO_WORKER_EPSILON_S;
    for (guint i = 0; !found && i < worker->pictures->len; i++)
        found = fabs(g_array_index(worker->pictures, worker_picture, i).pts - pts) <
                NS_VIDEO_WORKER_EPSILON_S;
    for (GList *l = worker->jobs.head; !found && l; l = l->next) {
        const worker_job *job = l->data;
        found = job->want_texture &&
                fabs(job->pts - pts) < NS_VIDEO_WORKER_EPSILON_S;
    }
    g_mutex_unlock(&worker->lock);
    return found;
}

ns_texture *
ns_video_worker_take(ns_video_worker *worker, double pts, double *taken_pts)
{
    if (!worker) return NULL;
    g_mutex_lock(&worker->lock);
    gint best = -1;
    for (guint i = 0; i < worker->pictures->len; i++) {
        const worker_picture *picture =
            &g_array_index(worker->pictures, worker_picture, i);
        if (picture->texture && picture->pts <= pts + NS_VIDEO_WORKER_EPSILON_S &&
            (best < 0 ||
             picture->pts > g_array_index(worker->pictures, worker_picture, best).pts))
            best = (gint)i;
    }
    ns_texture *texture = NULL;
    if (best >= 0) {
        worker_picture *picture = &g_array_index(worker->pictures, worker_picture, best);
        texture = picture->texture;
        picture->texture = NULL;
        if (taken_pts) *taken_pts = picture->pts;
    }
    for (guint i = 0; i < worker->pictures->len;) {
        if (g_array_index(worker->pictures, worker_picture, i).pts <=
            pts + NS_VIDEO_WORKER_EPSILON_S)
            g_array_remove_index(worker->pictures, i);
        else
            i++;
    }
    g_cond_signal(&worker->wake);
    g_mutex_unlock(&worker->lock);
    return texture;
}
