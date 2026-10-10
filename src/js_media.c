/* Northstar — HTMLMediaElement: the load, play and pause state machine over the audio mixer and decoded video.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "js_internal.h"

#include <math.h>
#include <string.h>

#include "audio/audio.h"
#include "audiodec.h"
#include "image.h"
#include "mainctx.h"
#include "media_types.h"
#include "mse.h"
#include "net.h"
#include "progressive.h"
#include "video.h"
#include "trace.h"

#define NS_MEDIA_POLL_MS 50
#define NS_MEDIA_TIMEUPDATE_US (250 * 1000)
/* The picture follows the sound's clock without ever stepping back. A
 * drift is slewed out by moving the frame timeline a fraction of it per
 * poll, at most NS_MEDIA_AV_SLEW_US (a 5% rate change, which the eye does
 * not see). A picture more than NS_MEDIA_AV_HOLD_S ahead (the sound
 * starved or its device clock jumped) holds until the sound catches up; one
 * more than NS_MEDIA_AV_RESYNC_S behind jumps forward. */
#define NS_MEDIA_AV_RESYNC_S 0.5
#define NS_MEDIA_AV_HOLD_S 0.1
#define NS_MEDIA_AV_SLEW_US 2500
#define NS_MEDIA_AV_SLEW_GAIN 0.25
#define NS_MEDIA_AV_DEADBAND_S 0.004
#define NS_MEDIA_MSE_AUDIO_AHEAD_S 2.0
#define NS_MEDIA_MSE_ENOUGH_S 1.0
#define NS_MEDIA_MSE_FUTURE_S 0.25
#define NS_MEDIA_MSE_GAP_S 0.15
#define NS_MEDIA_CONTROLS_BAR_H 32.0
#define NS_MEDIA_CONTROLS_TIME_W 86.0

enum {
    NETWORK_EMPTY,
    NETWORK_IDLE,
    NETWORK_LOADING,
    NETWORK_NO_SOURCE,
};

enum {
    HAVE_NOTHING,
    HAVE_METADATA,
    HAVE_CURRENT_DATA,
    HAVE_FUTURE_DATA,
    HAVE_ENOUGH_DATA,
};

enum {
    MEDIA_ERR_SRC_NOT_SUPPORTED = 4,
};

typedef struct {
    ns_js     *js;
    ns_node   *el;
    char       token[24];
    char      *src;
    gboolean   video;
    int        network_state;
    int        ready_state;
    gboolean   paused;
    gboolean   ended;
    gboolean   seeking;
    gboolean   fetching;
    gboolean   autoplaying;
    gboolean   can_autoplay;
    gboolean   held_for_load;
    double     duration;
    double     position;
    double     start_position;
    double     volume;
    int        muted;
    int        error;
    char      *error_message;
    GPtrArray *play_promises;
    JSValue    pin;
    gint64     last_timeupdate_us;
    ns_mse_source    *mse;
    ns_progressive   *progressive;
    ns_audio_decoder *decoder;
    double     mse_audio_next;
    gboolean   mse_waiting;
} ns_media_player;

typedef struct {
    ns_node   *el;
    char      *type;
    GPtrArray *promises;
    char      *error_name;
    char      *message;
} ns_media_task;

static void media_poll_ensure(ns_js *js);
static void media_controls_dirty(ns_media_player *p);
static void media_mse_restart_audio(ns_media_player *p, double t);
static void media_internal_pause(ns_media_player *p, gboolean fire_events);
static gboolean media_connected(ns_js *js, const ns_node *node);

gboolean
ns_node_is_media_element(const ns_node *n)
{
    return n && n->kind == NS_NODE_ELEMENT && n->name &&
           (g_ascii_strcasecmp(n->name, "video") == 0 ||
            g_ascii_strcasecmp(n->name, "audio") == 0);
}

static ns_media_player *
media_player_find(ns_js *js, const ns_node *el)
{
    return js && js->media_players
        ? g_hash_table_lookup(js->media_players, el) : NULL;
}

static void
media_promise_pair_free(gpointer data)
{
    JSValue *pair = data;
    g_free(pair);
}

static ns_media_player *
media_player_for(ns_js *js, ns_node *el)
{
    if (!js || !ns_node_is_media_element(el)) return NULL;
    ns_media_player *p = media_player_find(js, el);
    if (p) return p;
    if (!js->media_players)
        js->media_players = g_hash_table_new(g_direct_hash, g_direct_equal);
    p = g_new0(ns_media_player, 1);
    p->js = js;
    p->el = el;
    g_snprintf(p->token, sizeof p->token, "m%u", ++js->next_audio_token);
    p->video = ns_node_is_element_named(el, "video");
    p->network_state = NETWORK_EMPTY;
    p->ready_state = HAVE_NOTHING;
    p->paused = TRUE;
    p->duration = NAN;
    p->volume = 1.0;
    p->muted = -1;
    p->play_promises = g_ptr_array_new_with_free_func(media_promise_pair_free);
    p->pin = JS_UNDEFINED;
    g_hash_table_insert(js->media_players, el, p);
    return p;
}

static void media_consider_element(ns_js *js, ns_node *el);

static ns_media_player *
media_player_this(JSContext *ctx, JSValueConst this_val)
{
    ns_node *el = (ns_node *)ns_unwrap_element(this_val);
    if (!ns_node_is_media_element(el)) return NULL;
    ns_js *js = js_from_ctx(ctx);
    if (js && !media_player_find(js, el) && media_connected(js, el))
        media_consider_element(js, el);
    return media_player_for(js, el);
}

static NsAudioContext *
media_audio_context(ns_js *js)
{
    if (!js->audio_context)
        js->audio_context = ns_audio_context_new(js->current_url);
    return js->audio_context;
}

static gboolean
media_muted(const ns_media_player *p)
{
    if (p->muted >= 0) return p->muted != 0;
    return ns_element_get_attr(p->el, "muted") != NULL;
}

static gboolean
media_looping(const ns_media_player *p)
{
    return ns_element_get_attr(p->el, "loop") != NULL;
}

static double
media_output_volume(const ns_media_player *p)
{
    return media_muted(p) ? 0.0 : p->volume;
}

static void
media_pin(ns_media_player *p, gboolean pinned)
{
    JSContext *ctx = p->js->ctx;
    if (!ctx) return;
    if (pinned && JS_IsUndefined(p->pin)) {
        p->pin = ns_make_element(ctx, p->el);
    } else if (!pinned && !JS_IsUndefined(p->pin)) {
        JS_FreeValue(ctx, p->pin);
        p->pin = JS_UNDEFINED;
    }
}

static gboolean media_tasks_flush(gpointer data);

static void
media_queue_task(ns_media_player *p, const char *type)
{
    ns_js *js = p->js;
    if (!js->media_tasks) js->media_tasks = g_ptr_array_new();
    ns_media_task *task = g_new0(ns_media_task, 1);
    task->el = p->el;
    task->type = g_strdup(type);
    g_ptr_array_add(js->media_tasks, task);
    if (!js->media_task_source)
        js->media_task_source = ns_engine_idle_add(media_tasks_flush, js);
}

static JSValue
media_dom_error(JSContext *ctx, const char *name, const char *message)
{
    JSValue err = JS_NewError(ctx);
    JS_DefinePropertyValueStr(ctx, err, "name", JS_NewString(ctx, name),
        JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, message),
        JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue dx = JS_GetPropertyStr(ctx, global, "DOMException");
    if (JS_IsObject(dx)) {
        JSValue proto = JS_GetPropertyStr(ctx, dx, "prototype");
        if (JS_IsObject(proto)) JS_SetPrototype(ctx, err, proto);
        JS_FreeValue(ctx, proto);
    }
    JS_FreeValue(ctx, dx);
    JS_FreeValue(ctx, global);
    return err;
}

static void
media_free_promises(JSContext *ctx, GPtrArray *promises)
{
    if (!promises) return;
    for (guint i = 0; ctx && i < promises->len; i++) {
        JSValue *pair = g_ptr_array_index(promises, i);
        JS_FreeValue(ctx, pair[0]);
        JS_FreeValue(ctx, pair[1]);
    }
    g_ptr_array_free(promises, TRUE);
}

static void
media_settle_promises(JSContext *ctx, GPtrArray *promises,
                      const char *error_name, const char *message)
{
    for (guint i = 0; ctx && i < promises->len; i++) {
        JSValue *pair = g_ptr_array_index(promises, i);
        JSValue arg = error_name ? media_dom_error(ctx, error_name, message)
                                 : JS_UNDEFINED;
        JSValue r = JS_Call(ctx, pair[error_name ? 1 : 0], JS_UNDEFINED, 1,
                            &arg);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, arg);
    }
    media_free_promises(ctx, promises);
}

static void
media_task_free(ns_js *js, ns_media_task *task)
{
    media_free_promises(js->ctx, task->promises);
    g_free(task->type);
    g_free(task->error_name);
    g_free(task->message);
    g_free(task);
}

static void
media_task_run(ns_js *js, ns_media_task *task)
{
    if (task->promises) {
        GPtrArray *promises = task->promises;
        task->promises = NULL;
        media_settle_promises(js->ctx, promises, task->error_name,
                              task->message);
        return;
    }
    if (!task->el) return;
    ns_media_player *p = media_player_find(js, task->el);
    if (strcmp(task->type, "!detached") == 0) {
        if (p && !media_connected(js, p->el)) media_internal_pause(p, TRUE);
        return;
    }
    ns_js_dispatch_event(js, task->el, task->type, NULL);
}

static gboolean
media_tasks_flush(gpointer data)
{
    ns_js *js = data;
    js->media_task_source = 0;
    if (js->halted) return G_SOURCE_REMOVE;
    if (js->in_pump) {
        js->media_task_source = ns_engine_timeout_add(4, media_tasks_flush, js);
        return G_SOURCE_REMOVE;
    }
    GPtrArray *tasks = js->media_tasks;
    js->media_tasks = NULL;
    js->media_tasks_flushing = tasks;
    for (guint i = 0; tasks && i < tasks->len; i++) {
        ns_media_task *task = g_ptr_array_index(tasks, i);
        media_task_run(js, task);
        media_task_free(js, task);
    }
    js->media_tasks_flushing = NULL;
    if (tasks) g_ptr_array_free(tasks, TRUE);
    ns_drain_microtasks(js);
    return G_SOURCE_REMOVE;
}

static void
media_queue_settle(ns_media_player *p, const char *error_name,
                   const char *message)
{
    if (p->play_promises->len == 0) return;
    media_queue_task(p, "");
    ns_media_task *task = g_ptr_array_index(p->js->media_tasks,
                                            p->js->media_tasks->len - 1);
    task->promises = p->play_promises;
    task->error_name = g_strdup(error_name);
    task->message = g_strdup(message);
    p->play_promises = g_ptr_array_new_with_free_func(media_promise_pair_free);
}

static void
media_reject_play(ns_media_player *p, const char *name, const char *message)
{
    media_queue_settle(p, name, message);
}

static void
media_notify_playing(ns_media_player *p)
{
    media_queue_task(p, "playing");
    media_queue_settle(p, NULL, NULL);
}

char *
ns_media_resolve_src(JSContext *ctx, ns_node *node)
{
    const char *src = ns_media_select_source(node);
    if (!src) return NULL;
    ns_js *js = js_from_ctx(ctx);
    char *abs = (js && js->current_url) ? ns_url_resolve(js->current_url, src)
                                        : g_strdup(src);
    if (!abs) return NULL;
    ns_fetch_verdict verdict = ns_fetch_policy_check(
        js ? ns_js_fetch_policy(js, node) : NULL, NS_FETCH_DEST_MEDIA,
        js ? js->current_url : NULL, abs, NULL);
    if (ns_fetch_verdict_blocks(verdict)) {
        char *message = ns_fetch_verdict_message(verdict, NS_FETCH_DEST_MEDIA,
                                                 abs);
        if (js && js->log_cb) js->log_cb(message, js->log_user_data);
        g_free(message);
        g_free(abs);
        return NULL;
    }
    return abs;
}

static ns_image *
media_video_frames(ns_media_player *p)
{
    ns_js *js = p->js;
    if (!p->video || !p->src || !js->image_cache) return NULL;
    ns_image *img = ns_image_cache_peek(js->image_cache, p->src);
    return ns_image_is_animation(img) ? img : NULL;
}

static gboolean
media_video_failed(ns_media_player *p)
{
    ns_js *js = p->js;
    if (!p->video || !p->src || !js->image_cache) return FALSE;
    ns_image *img = ns_image_cache_peek(js->image_cache, p->src);
    return img && (img->failed ||
                   (img->loaded && !ns_image_is_animation(img)));
}

ns_image *
ns_media_animation_for(JSContext *ctx, JSValueConst this_val)
{
    ns_node *el = (ns_node *)ns_unwrap_element(this_val);
    ns_media_player *p = media_player_find(js_from_ctx(ctx), el);
    return p ? media_video_frames(p) : NULL;
}

static void
media_close_backend(ns_media_player *p)
{
    if (p->fetching && p->js->audio_context)
        ns_audio_context_close(p->js->audio_context, p->token);
    p->fetching = FALSE;
    ns_image *frames = media_video_frames(p);
    if (frames) ns_image_anim_set_paused(frames, TRUE, g_get_monotonic_time());
    if (p->mse) {
        ns_mse_source_unref(p->mse);
        p->mse = NULL;
    }
    g_clear_pointer(&p->progressive, ns_progressive_free);
    ns_audio_decoder_free(p->decoder);
    p->decoder = NULL;
    p->mse_waiting = FALSE;
}

static void
media_mse_attach(ns_media_player *p, ns_mse_source *source)
{
    p->mse = ns_mse_source_ref(source);
    p->mse_audio_next = 0.0;
    p->mse_waiting = FALSE;
    if (p->video && p->js->image_cache)
        ns_image_cache_insert_stream(p->js->image_cache, p->src,
                                     ns_video_stream_new_mse(source));
}

static gboolean
media_start_progressive(ns_media_player *p)
{
    ns_js *js = p->js;
    if (!p->video || !js->image_cache) return FALSE;
    ns_image *known = ns_image_cache_peek(js->image_cache, p->src);
    if (known && known->video && !ns_video_stream_is_mse(known->video))
        return FALSE;
    p->progressive = ns_progressive_new(p->src, js->current_url,
                                        ns_js_fetch_policy(js, p->el));
    media_mse_attach(p, ns_progressive_source(p->progressive));
    return TRUE;
}

static void
media_fail_source(ns_media_player *p, int code, const char *message)
{
    p->error = code;
    g_free(p->error_message);
    p->error_message = g_strdup(message);
    p->network_state = NETWORK_NO_SOURCE;
    p->fetching = FALSE;
    media_pin(p, FALSE);
    media_queue_task(p, "error");
    media_reject_play(p, "NotSupportedError",
                      "Failed to load because no supported source was found.");
}

static void
media_start_fetch(ns_media_player *p)
{
    if (p->video || p->fetching || !p->src) return;
    ns_js *js = p->js;
    NsAudioContext *audio = media_audio_context(js);
    if (g_str_has_prefix(p->src, "blob:")) {
        GBytes *bytes = ns_net_resolve_blob(p->src, NULL);
        if (!bytes) {
            media_fail_source(p, MEDIA_ERR_SRC_NOT_SUPPORTED,
                              "blob URL not found");
            return;
        }
        ns_audio_context_open_bytes(audio, p->token, bytes, FALSE);
        g_bytes_unref(bytes);
    } else {
        ns_audio_context_open(audio, p->token, p->src);
    }
    ns_audio_context_set_volume(audio, p->token, media_output_volume(p));
    ns_audio_context_set_loop(audio, p->token, media_looping(p));
    p->fetching = TRUE;
    p->network_state = NETWORK_LOADING;
    media_poll_ensure(js);
}

static gboolean
media_autoplay_allowed(const ns_media_player *p)
{
    return (p->video && !p->fetching) || media_muted(p) ||
           p->js->user_ever_activated;
}

static void
media_open_video_audio(ns_media_player *p, ns_image *frames)
{
    if (p->fetching || !ns_image_video_has_audio(frames)) return;
    GBytes *bytes = ns_image_video_bytes(frames);
    if (!bytes) return;
    NsAudioContext *audio = media_audio_context(p->js);
    ns_audio_context_open_bytes(audio, p->token, bytes, FALSE);
    g_bytes_unref(bytes);
    ns_audio_context_set_volume(audio, p->token, media_output_volume(p));
    ns_audio_context_set_loop(audio, p->token, media_looping(p));
    p->fetching = TRUE;
}

static void
media_close_video_audio(ns_media_player *p)
{
    if (!p->fetching) return;
    if (p->js->audio_context)
        ns_audio_context_close(p->js->audio_context, p->token);
    p->fetching = FALSE;
}

static gboolean
media_video_audio_ready(ns_media_player *p, NsAudioStatus *status)
{
    if (!p->fetching || !p->js->audio_context) return FALSE;
    if (!ns_audio_context_status(p->js->audio_context, p->token, status))
        return FALSE;
    if (status->state == NS_AUDIO_PLAYER_FAILED) {
        media_close_video_audio(p);
        return FALSE;
    }
    return status->state == NS_AUDIO_PLAYER_READY;
}

static void
media_load(ns_media_player *p)
{
    media_reject_play(p, "AbortError",
                      "The play() request was interrupted by a new load "
                      "request.");
    media_close_backend(p);
    if (p->network_state != NETWORK_EMPTY) {
        media_queue_task(p, "emptied");
        if (!isnan(p->duration)) media_queue_task(p, "durationchange");
    }
    p->network_state = NETWORK_EMPTY;
    p->ready_state = HAVE_NOTHING;
    p->paused = TRUE;
    p->ended = FALSE;
    p->seeking = FALSE;
    p->autoplaying = FALSE;
    p->can_autoplay = TRUE;
    p->held_for_load = FALSE;
    p->position = 0.0;
    p->start_position = 0.0;
    p->duration = NAN;
    p->error = 0;
    g_clear_pointer(&p->error_message, g_free);
    g_clear_pointer(&p->src, g_free);
    media_pin(p, FALSE);

    gboolean has_source = ns_element_get_attr(p->el, "src") != NULL;
    for (const ns_node *c = p->el->first_child; !has_source && c;
         c = c->next_sibling)
        has_source = ns_node_is_element_named(c, "source");
    if (!has_source) return;

    p->network_state = NETWORK_LOADING;
    media_queue_task(p, "loadstart");
    JSContext *ctx = p->js->ctx;
    p->src = ctx ? ns_media_resolve_src(ctx, p->el) : NULL;
    if (!p->src) {
        media_fail_source(p, MEDIA_ERR_SRC_NOT_SUPPORTED,
                          "MEDIA_ELEMENT_ERROR: Format error");
        return;
    }
    ns_mse_source *source = ns_js_mse_source_for_url(p->js, p->src);
    if (source || media_start_progressive(p)) {
        if (source) media_mse_attach(p, source);
        media_poll_ensure(p->js);
        return;
    }
    gboolean eager = p->video ||
        ns_element_get_attr(p->el, "autoplay") != NULL ||
        g_strcmp0(ns_element_get_attr(p->el, "preload"), "auto") == 0;
    if (eager) {
        media_start_fetch(p);
        media_poll_ensure(p->js);
    } else {
        p->network_state = NETWORK_IDLE;
        media_queue_task(p, "suspend");
    }
}

/* Playback holds until the document has fired its load event, so a page
 * that starts a video while it is still laying itself out does not spend
 * the first seconds of playback competing with that work. */
static gboolean
media_page_loaded(const ns_media_player *p)
{
    return p->js->ready_state >= 2;
}

static void
media_backend_play(ns_media_player *p)
{
    p->held_for_load = !media_page_loaded(p);
    if (p->held_for_load) {
        media_poll_ensure(p->js);
        return;
    }
    if (p->mse && p->ready_state < HAVE_FUTURE_DATA) return;
    if (p->video) {
        ns_image *frames = media_video_frames(p);
        if (!frames) return;
        gint64 now = g_get_monotonic_time();
        if (ns_image_anim_ended(frames, now)) ns_image_anim_seek(frames, 0, now);
        if (p->fetching && p->js->audio_context) {
            ns_audio_context_seek(p->js->audio_context, p->token,
                                  ns_image_anim_position(frames, now));
            ns_audio_context_play(p->js->audio_context, p->token);
            NsAudioStatus status;
            if (!media_video_audio_ready(p, &status) && p->fetching) {
                media_poll_ensure(p->js);
                return;
            }
        }
        ns_image_anim_set_paused(frames, FALSE, now);
        ns_js_request_repaint(p->js);
        return;
    }
    if (p->js->audio_context)
        ns_audio_context_play(p->js->audio_context, p->token);
}

static void
media_backend_pause(ns_media_player *p)
{
    if (p->video) {
        ns_image *frames = media_video_frames(p);
        if (frames)
            ns_image_anim_set_paused(frames, TRUE, g_get_monotonic_time());
        if (p->fetching && p->js->audio_context)
            ns_audio_context_pause(p->js->audio_context, p->token);
        return;
    }
    if (p->fetching && p->js->audio_context)
        ns_audio_context_pause(p->js->audio_context, p->token);
}

static void
media_backend_seek(ns_media_player *p, double t)
{
    if (p->video) {
        ns_image *frames = media_video_frames(p);
        if (frames) {
            ns_image_anim_seek(frames, t, g_get_monotonic_time());
            ns_js_request_repaint(p->js);
        }
        if (p->mse) media_mse_restart_audio(p, t);
        else if (p->fetching && p->js->audio_context)
            ns_audio_context_seek(p->js->audio_context, p->token, t);
        return;
    }
    if (p->mse) {
        media_mse_restart_audio(p, t);
        return;
    }
    if (p->fetching && p->js->audio_context)
        ns_audio_context_seek(p->js->audio_context, p->token, t);
}

static double
media_current_time(ns_media_player *p)
{
    if (p->ready_state < HAVE_METADATA) return p->start_position;
    if (p->video) {
        ns_image *frames = media_video_frames(p);
        if (frames) p->position = ns_image_anim_position(frames,
                                                         g_get_monotonic_time());
    } else if (p->fetching && p->js->audio_context) {
        NsAudioStatus status;
        if (ns_audio_context_status(p->js->audio_context, p->token, &status) &&
            status.state == NS_AUDIO_PLAYER_READY)
            p->position = status.position;
    }
    return p->position;
}

static void
media_internal_play(ns_media_player *p)
{
    if (p->ended && !media_looping(p)) {
        p->position = 0.0;
        media_backend_seek(p, 0.0);
    }
    p->ended = FALSE;
    media_start_fetch(p);
    if (p->paused) {
        p->paused = FALSE;
        media_pin(p, TRUE);
        media_queue_task(p, "play");
        if (p->ready_state <= HAVE_CURRENT_DATA)
            media_queue_task(p, "waiting");
        else
            media_notify_playing(p);
    } else if (p->ready_state >= HAVE_FUTURE_DATA) {
        media_queue_settle(p, NULL, NULL);
    }
    if (p->ready_state >= HAVE_METADATA) media_backend_play(p);
    p->last_timeupdate_us = g_get_monotonic_time();
    media_poll_ensure(p->js);
    media_controls_dirty(p);
}

static void
media_internal_pause(ns_media_player *p, gboolean fire_events)
{
    p->autoplaying = FALSE;
    if (p->paused) return;
    p->paused = TRUE;
    media_current_time(p);
    media_backend_pause(p);
    media_pin(p, FALSE);
    media_controls_dirty(p);
    if (fire_events) {
        media_queue_task(p, "timeupdate");
        media_queue_task(p, "pause");
    }
    media_reject_play(p, "AbortError",
                      "The play() request was interrupted by a call to "
                      "pause().");
}

static void
media_became_ready(ns_media_player *p, double duration, int width, int height)
{
    (void)width;
    (void)height;
    p->duration = duration > 0 ? duration : NAN;
    p->ready_state = HAVE_ENOUGH_DATA;
    p->network_state = NETWORK_IDLE;
    media_queue_task(p, "durationchange");
    media_queue_task(p, "loadedmetadata");
    media_queue_task(p, "loadeddata");
    media_queue_task(p, "canplay");
    media_queue_task(p, "canplaythrough");
    if (p->video) {
        ns_image *frames = media_video_frames(p);
        if (frames) {
            gint64 now = g_get_monotonic_time();
            ns_image_anim_set_loop(frames, media_looping(p), now);
            ns_image_anim_set_paused(frames, TRUE, now);
            ns_image_anim_seek(frames, p->start_position, now);
            media_open_video_audio(p, frames);
        }
    } else if (p->start_position > 0 && p->js->audio_context) {
        ns_audio_context_seek(p->js->audio_context, p->token,
                              p->start_position);
    }
    p->position = p->start_position;
    if (!p->paused) {
        media_backend_play(p);
        media_notify_playing(p);
    } else if (p->can_autoplay &&
               ns_element_get_attr(p->el, "autoplay") &&
               media_autoplay_allowed(p)) {
        p->autoplaying = TRUE;
        media_internal_play(p);
    }
}

static void
media_reached_end(ns_media_player *p)
{
    if (p->video && p->fetching && p->js->audio_context)
        ns_audio_context_pause(p->js->audio_context, p->token);
    p->position = isnan(p->duration) ? p->position : p->duration;
    p->ended = TRUE;
    media_controls_dirty(p);
    media_queue_task(p, "timeupdate");
    if (!p->paused) {
        p->paused = TRUE;
        media_pin(p, FALSE);
        media_queue_task(p, "pause");
    }
    media_queue_task(p, "ended");
}

static void
media_mse_restart_audio(ns_media_player *p, double t)
{
    if (!p->mse || !p->fetching || !p->decoder || !p->js->audio_context) return;
    ns_mse_buffer *audio = ns_mse_source_track_buffer(p->mse, NS_MSE_TRACK_AUDIO);
    gssize index = ns_mse_buffer_frame_at(audio, t);
    if (index < 0) index = ns_mse_buffer_frame_from(audio, t);
    const ns_mse_frame *frame = index >= 0
        ? ns_mse_buffer_frame(audio, (guint)index) : NULL;
    if (frame && frame->pts > t + NS_MEDIA_MSE_GAP_S) {
        index = -1;
        frame = NULL;
    }
    double start = frame ? frame->pts : t;
    ns_audio_decoder_reset(p->decoder, index == 0);
    ns_audio_context_stream_restart(p->js->audio_context, p->token, start);
    p->mse_audio_next = start;
}

static void
media_mse_open_audio(ns_media_player *p)
{
    if (p->fetching) return;
    ns_mse_buffer *audio = ns_mse_source_track_buffer(p->mse, NS_MSE_TRACK_AUDIO);
    const ns_mp4_track *track = ns_mse_buffer_track(audio);
    if (!track) return;
    ns_audio_decoder *decoder = ns_audio_decoder_new(track->codec,
                                                     track->codec_config,
                                                     track->channels,
                                                     track->sample_rate);
    if (!decoder) return;
    NsAudioContext *context = media_audio_context(p->js);
    if (!ns_audio_context_open_stream(context, p->token,
                                      ns_audio_decoder_sample_rate(decoder),
                                      ns_audio_decoder_channels(decoder))) {
        ns_audio_decoder_free(decoder);
        return;
    }
    p->decoder = decoder;
    p->fetching = TRUE;
    ns_audio_context_set_volume(context, p->token, media_output_volume(p));
    media_mse_restart_audio(p, media_current_time(p));
    if (!p->paused && !p->mse_waiting && media_page_loaded(p))
        ns_audio_context_play(context, p->token);
}

static void
media_mse_feed_audio(ns_media_player *p, double position)
{
    if (!p->fetching || !p->decoder) return;
    gint64 trace_start = ns_trace_now();
    double trace_from = p->mse_audio_next;
    ns_mse_buffer *audio = ns_mse_source_track_buffer(p->mse, NS_MSE_TRACK_AUDIO);
    int channels = ns_audio_decoder_channels(p->decoder);
    float pcm[NS_AUDIO_DECODER_MAX_FRAMES * 2];
    while (p->mse_audio_next < position + NS_MEDIA_MSE_AUDIO_AHEAD_S) {
        gssize index = ns_mse_buffer_frame_at(audio, p->mse_audio_next + 1e-4);
        if (index < 0) break;
        const ns_mse_frame *frame = ns_mse_buffer_frame(audio, (guint)index);
        if (frame->pts + frame->duration <= p->mse_audio_next + 1e-6) {
            frame = ns_mse_buffer_frame(audio, (guint)index + 1);
            if (!frame || frame->pts > p->mse_audio_next + NS_MEDIA_MSE_GAP_S) break;
        }
        gsize len = 0;
        const guint8 *data = g_bytes_get_data(frame->data, &len);
        int frames = ns_audio_decoder_decode(p->decoder, data, len, pcm,
                                             NS_AUDIO_DECODER_MAX_FRAMES);
        if (frames > 0)
            ns_audio_context_stream_push(p->js->audio_context, p->token, pcm,
                                         frames, channels);
        p->mse_audio_next = frame->pts + frame->duration;
    }
    if (trace_start && p->mse_audio_next > trace_from)
        ns_trace_completef("media", "audio feed", trace_start,
                           "%.3f-%.3f s decoded", trace_from,
                           p->mse_audio_next);
    guint n = ns_mse_buffer_n_frames(audio);
    const ns_mse_frame *last = n ? ns_mse_buffer_frame(audio, n - 1) : NULL;
    ns_audio_context_stream_end(p->js->audio_context, p->token,
        ns_mse_source_ended(p->mse) && last &&
        p->mse_audio_next >= last->pts + last->duration - 1e-6);
}

static void
media_mse_stall(ns_media_player *p, gboolean stall)
{
    ns_image *frames = media_video_frames(p);
    gint64 now = g_get_monotonic_time();
    p->mse_waiting = stall;
    if (frames) ns_image_anim_set_paused(frames, stall, now);
    if (p->fetching && p->js->audio_context) {
        if (stall) ns_audio_context_pause(p->js->audio_context, p->token);
        else ns_audio_context_play(p->js->audio_context, p->token);
    }
    ns_js_request_repaint(p->js);
}

static void
media_mse_set_ready_state(ns_media_player *p, int state)
{
    int old = p->ready_state;
    if (state == old) return;
    p->ready_state = state;
    if (old < HAVE_CURRENT_DATA && state >= HAVE_CURRENT_DATA)
        media_queue_task(p, "loadeddata");
    if (old < HAVE_FUTURE_DATA && state >= HAVE_FUTURE_DATA) {
        media_queue_task(p, "canplay");
        if (!p->paused) {
            if (p->mse_waiting) media_mse_stall(p, FALSE);
            media_notify_playing(p);
        }
    }
    if (old < HAVE_ENOUGH_DATA && state >= HAVE_ENOUGH_DATA) {
        media_queue_task(p, "canplaythrough");
        if (p->paused && p->can_autoplay &&
            ns_element_get_attr(p->el, "autoplay") &&
            media_autoplay_allowed(p)) {
            p->autoplaying = TRUE;
            media_internal_play(p);
        }
    }
}

static void
media_mse_poll(ns_media_player *p)
{
    ns_mse_source *source = p->mse;
    double duration = ns_mse_source_duration(source);
    if (!(isnan(duration) && isnan(p->duration)) && duration != p->duration) {
        p->duration = duration;
        media_queue_task(p, "durationchange");
    }
    ns_mse_buffer *video = ns_mse_source_track_buffer(source, NS_MSE_TRACK_VIDEO);
    ns_mse_buffer *audio = ns_mse_source_track_buffer(source, NS_MSE_TRACK_AUDIO);
    if (p->ready_state < HAVE_METADATA) {
        if (!ns_mse_buffer_track(video) && !ns_mse_buffer_track(audio)) return;
        p->ready_state = HAVE_METADATA;
        p->network_state = NETWORK_IDLE;
        media_queue_task(p, "loadedmetadata");
        ns_image *frames = media_video_frames(p);
        if (frames) {
            gint64 now = g_get_monotonic_time();
            ns_image_anim_set_paused(frames, TRUE, now);
            ns_image_anim_seek(frames, p->start_position, now);
        }
        p->position = p->start_position;
    }
    media_mse_open_audio(p);
    double t = media_current_time(p);
    media_mse_feed_audio(p, t);
    GArray *ranges = ns_mse_source_buffered(source);
    double end = -1.0;
    for (guint i = 0; i + 1 < ranges->len; i += 2) {
        double start = g_array_index(ranges, double, i);
        double stop = g_array_index(ranges, double, i + 1);
        if (t >= start - NS_MEDIA_MSE_GAP_S && t < stop) {
            end = stop;
            break;
        }
    }
    g_array_free(ranges, TRUE);
    gboolean ended = ns_mse_source_ended(source);
    double ahead = end >= 0 ? end - t : -1.0;
    int state = ahead >= NS_MEDIA_MSE_ENOUGH_S || (ended && end >= 0)
        ? HAVE_ENOUGH_DATA
        : ahead >= NS_MEDIA_MSE_FUTURE_S ? HAVE_FUTURE_DATA
        : ahead >= 0 ? HAVE_CURRENT_DATA : HAVE_METADATA;
    if (!media_page_loaded(p) && state > HAVE_CURRENT_DATA)
        state = HAVE_CURRENT_DATA;
    media_mse_set_ready_state(p, state);
    if (p->paused) return;
    if (ended && !isnan(p->duration) && t >= p->duration - 0.08) {
        media_reached_end(p);
        return;
    }
    if (state < HAVE_FUTURE_DATA && !ended) {
        if (!p->mse_waiting) {
            media_mse_stall(p, TRUE);
            media_queue_task(p, "waiting");
        }
    } else if (p->mse_waiting) {
        media_mse_stall(p, FALSE);
        media_notify_playing(p);
    }
}

static gboolean
media_progressive_poll(ns_media_player *p)
{
    ns_progressive_state state = ns_progressive_get_state(p->progressive);
    if (state == NS_PROGRESSIVE_PROBING || state == NS_PROGRESSIVE_READY) {
        ns_progressive_update(p->progressive, media_current_time(p), !p->paused);
        return TRUE;
    }
    GBytes *body = ns_progressive_take_body(p->progressive);
    media_close_backend(p);
    gsize len = 0;
    const guchar *data = body ? g_bytes_get_data(body, &len) : NULL;
    ns_video_stream *stream = data ? ns_video_stream_new(data, len) : NULL;
    if (body) g_bytes_unref(body);
    if (stream)
        ns_image_cache_insert_stream(p->js->image_cache, p->src, stream);
    else
        media_fail_source(p, MEDIA_ERR_SRC_NOT_SUPPORTED,
                          "MEDIA_ELEMENT_ERROR: Format error");
    return FALSE;
}

static void
media_video_follow_audio(ns_media_player *p, ns_image *frames, gint64 now)
{
    NsAudioStatus status;
    gboolean ready = media_video_audio_ready(p, &status);
    if (!ready) {
        if (!p->fetching && frames->anim_paused) {
            ns_image_anim_set_paused(frames, FALSE, now);
            ns_js_request_repaint(p->js);
        }
        return;
    }
    if (frames->anim_paused) {
        ns_image_anim_set_paused(frames, FALSE, now);
        double ahead = ns_image_anim_position(frames, now) - status.position;
        if (ahead < -NS_MEDIA_AV_RESYNC_S)
            ns_image_anim_seek(frames, status.position, now);
        else
            ns_image_anim_shift(frames, (gint64)(ahead * 1e6));
        ns_js_request_repaint(p->js);
        return;
    }
    double drift = ns_image_anim_position(frames, now) - status.position;
    if (!status.playing) {
        if (drift > NS_MEDIA_AV_HOLD_S)
            ns_image_anim_shift(frames, (gint64)(drift * 1e6));
        return;
    }
    if (drift < -NS_MEDIA_AV_RESYNC_S) {
        ns_image_anim_seek(frames, status.position, now);
    } else if (drift > NS_MEDIA_AV_HOLD_S) {
        ns_image_anim_shift(frames, (gint64)(drift * 1e6));
    } else if (fabs(drift) > NS_MEDIA_AV_DEADBAND_S) {
        double step = CLAMP(drift * NS_MEDIA_AV_SLEW_GAIN * 1e6,
                            -NS_MEDIA_AV_SLEW_US, NS_MEDIA_AV_SLEW_US);
        ns_image_anim_shift(frames, (gint64)step);
    }
}

static gboolean
media_poll_player(ns_media_player *p, gint64 now)
{
    if (p->network_state == NETWORK_NO_SOURCE ||
        p->network_state == NETWORK_EMPTY)
        return FALSE;
    if (p->held_for_load) {
        if (!media_page_loaded(p)) return TRUE;
        p->held_for_load = FALSE;
        if (!p->paused) media_backend_play(p);
    }
    if (p->progressive && !media_progressive_poll(p))
        return p->network_state != NETWORK_NO_SOURCE;
    if (p->mse) {
        media_mse_poll(p);
        if (!p->mse) return FALSE;
        ns_image *frames = media_video_frames(p);
        if (!p->paused && frames && !p->mse_waiting)
            media_video_follow_audio(p, frames, now);
        p->position = media_current_time(p);
        if (!p->paused && now - p->last_timeupdate_us >= NS_MEDIA_TIMEUPDATE_US) {
            p->last_timeupdate_us = now;
            media_queue_task(p, "timeupdate");
            media_controls_dirty(p);
        }
        return TRUE;
    }
    if (p->video) {
        if (p->ready_state < HAVE_METADATA) {
            ns_image *frames = media_video_frames(p);
            if (frames) {
                media_became_ready(p, ns_image_anim_duration(frames),
                                   frames->natural_width,
                                   frames->natural_height);
            } else if (media_video_failed(p)) {
                media_fail_source(p, MEDIA_ERR_SRC_NOT_SUPPORTED,
                                  "MEDIA_ELEMENT_ERROR: Format error");
                return FALSE;
            }
            return p->ready_state < HAVE_METADATA || !p->paused;
        }
        ns_image *frames = media_video_frames(p);
        if (!p->paused && frames) {
            media_video_follow_audio(p, frames, now);
            p->position = ns_image_anim_position(frames, now);
            if (ns_image_anim_ended(frames, now)) {
                media_reached_end(p);
                return FALSE;
            }
        }
    } else {
        if (!p->fetching || !p->js->audio_context)
            return p->network_state == NETWORK_LOADING;
        NsAudioStatus status;
        gboolean known = ns_audio_context_status(p->js->audio_context,
                                                 p->token, &status);
        if (known && status.state == NS_AUDIO_PLAYER_FAILED) {
            media_fail_source(p, MEDIA_ERR_SRC_NOT_SUPPORTED,
                status.error == NS_AUDIO_ERROR_DECODE
                    ? "MEDIA_ELEMENT_ERROR: Format error"
                    : "MEDIA_ELEMENT_ERROR: Network error");
            return FALSE;
        }
        if (p->ready_state < HAVE_METADATA) {
            if (known && status.state == NS_AUDIO_PLAYER_READY)
                media_became_ready(p, status.duration, 0, 0);
            return TRUE;
        }
        if (known && status.state == NS_AUDIO_PLAYER_READY &&
            status.duration > 0 && status.duration != p->duration) {
            p->duration = status.duration;
            media_queue_task(p, "durationchange");
        }
        if (!p->paused && known && status.state == NS_AUDIO_PLAYER_READY) {
            p->position = status.position;
            if (status.ended && !media_looping(p)) {
                media_reached_end(p);
                return FALSE;
            }
        }
    }
    if (p->paused) return FALSE;
    if (now - p->last_timeupdate_us >= NS_MEDIA_TIMEUPDATE_US) {
        p->last_timeupdate_us = now;
        media_queue_task(p, "timeupdate");
        media_controls_dirty(p);
    }
    return TRUE;
}

static gboolean
media_poll(gpointer data)
{
    ns_js *js = data;
    if (js->halted || !js->media_players) {
        js->media_poll_source = 0;
        return G_SOURCE_REMOVE;
    }
    gint64 now = g_get_monotonic_time();
    gboolean active = FALSE;
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, js->media_players);
    while (g_hash_table_iter_next(&it, NULL, &value))
        if (media_poll_player(value, now)) active = TRUE;
    if (active) return G_SOURCE_CONTINUE;
    js->media_poll_source = 0;
    return G_SOURCE_REMOVE;
}

static void
media_poll_ensure(ns_js *js)
{
    if (!js->media_poll_source)
        js->media_poll_source =
            ns_engine_timeout_add(NS_MEDIA_POLL_MS, media_poll, js);
}

static void
media_player_free(ns_media_player *p)
{
    media_close_backend(p);
    media_pin(p, FALSE);
    media_free_promises(p->js->ctx, p->play_promises);
    g_free(p->src);
    g_free(p->error_message);
    g_free(p);
}

static void
media_forget_tasks(ns_js *js, const ns_node *el)
{
    for (guint i = 0; js->media_tasks && i < js->media_tasks->len; i++) {
        ns_media_task *task = g_ptr_array_index(js->media_tasks, i);
        if (task->el == el) task->el = NULL;
    }
    for (guint i = 0; js->media_tasks_flushing &&
                      i < js->media_tasks_flushing->len; i++) {
        ns_media_task *task = g_ptr_array_index(js->media_tasks_flushing, i);
        if (task->el == el) task->el = NULL;
    }
}

void
ns_media_node_released(ns_js *js, ns_node *n)
{
    ns_media_player *p = media_player_find(js, n);
    if (!p) return;
    g_hash_table_remove(js->media_players, n);
    media_forget_tasks(js, n);
    media_player_free(p);
}

void
ns_media_teardown(ns_js *js)
{
    if (!js) return;
    ns_js_mse_teardown(js);
    if (js->media_poll_source) {
        ns_engine_source_remove(js->media_poll_source);
        js->media_poll_source = 0;
    }
    if (js->media_task_source) {
        ns_engine_source_remove(js->media_task_source);
        js->media_task_source = 0;
    }
    if (js->media_tasks) {
        for (guint i = 0; i < js->media_tasks->len; i++)
            media_task_free(js, g_ptr_array_index(js->media_tasks, i));
        g_ptr_array_free(js->media_tasks, TRUE);
        js->media_tasks = NULL;
    }
    if (js->media_players) {
        GHashTableIter it;
        gpointer value;
        g_hash_table_iter_init(&it, js->media_players);
        while (g_hash_table_iter_next(&it, NULL, &value))
            media_player_free(value);
        g_hash_table_destroy(js->media_players);
        js->media_players = NULL;
    }
    if (js->audio_context) {
        ns_audio_context_destroy(js->audio_context);
        js->audio_context = NULL;
    }
}

void
ns_js_suspend_media(ns_js *js)
{
    if (!js || !js->media_players) return;
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, js->media_players);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        ns_media_player *p = value;
        p->can_autoplay = FALSE;
        media_internal_pause(p, FALSE);
    }
}

static gboolean
media_connected(ns_js *js, const ns_node *node)
{
    for (const ns_node *p = node; p; p = p->parent)
        if (p == js->current_doc) return TRUE;
    return FALSE;
}

static void
media_consider_element(ns_js *js, ns_node *el)
{
    ns_media_player *p = media_player_find(js, el);
    if (p && p->network_state != NETWORK_EMPTY) return;
    gboolean has_source = ns_element_get_attr(el, "src") != NULL;
    for (const ns_node *c = el->first_child; !has_source && c;
         c = c->next_sibling)
        has_source = ns_node_is_element_named(c, "source");
    if (!has_source) return;
    media_load(media_player_for(js, el));
}

static void
media_walk_connected(ns_js *js, ns_node *root, int depth)
{
    if (!root || depth >= 512) return;
    if (ns_node_is_media_element(root)) media_consider_element(js, root);
    for (ns_node *c = root->first_child; c; c = c->next_sibling)
        media_walk_connected(js, c, depth + 1);
}

void
ns_media_subtree_connected(ns_js *js, ns_node *root)
{
    if (!js || !root || !media_connected(js, root)) return;
    media_walk_connected(js, root, 0);
}

void
ns_media_scan(ns_js *js, ns_node *doc, const char *base_url)
{
    if (!js || !doc) return;
    if (!js->current_doc) {
        js->current_doc = doc;
        if (!js->current_url) js->current_url = g_strdup(base_url);
    }
    if (js->current_doc == doc) media_walk_connected(js, doc, 0);
}

void
ns_media_subtree_disconnected(ns_js *js, ns_node *root)
{
    if (!js || !root || !js->media_players) return;
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, js->media_players);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        ns_media_player *p = value;
        for (const ns_node *n = p->el; n; n = n->parent) {
            if (n != root) continue;
            if (!p->paused) media_queue_task(p, "!detached");
            break;
        }
    }
}

void
ns_media_attr_changed(ns_js *js, ns_node *el, const char *name)
{
    if (!js || !ns_node_is_media_element(el) || !name) return;
    ns_media_player *p = media_player_find(js, el);
    if (g_ascii_strcasecmp(name, "src") == 0) {
        if (!p && ns_element_get_attr(el, "src") == NULL) return;
        media_load(media_player_for(js, el));
        return;
    }
    if (!p) return;
    if (g_ascii_strcasecmp(name, "loop") == 0) {
        if (p->fetching && js->audio_context)
            ns_audio_context_set_loop(js->audio_context, p->token,
                                      media_looping(p));
        ns_image *frames = media_video_frames(p);
        if (frames)
            ns_image_anim_set_loop(frames, media_looping(p),
                                   g_get_monotonic_time());
    } else if (g_ascii_strcasecmp(name, "muted") == 0 && p->muted < 0) {
        if (p->fetching && js->audio_context)
            ns_audio_context_set_volume(js->audio_context, p->token,
                                        media_output_volume(p));
    }
}

void
ns_media_blob_updated(ns_js *js, const char *url)
{
    if (!js || !url || !js->media_players || !js->audio_context) return;
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, js->media_players);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        ns_media_player *p = value;
        if (!p->fetching || !p->src || strcmp(p->src, url) != 0) continue;
        /* A Media Source Extensions stream (or a progressive download) feeds
         * its own streaming player: the blob behind its URL is the polyfill's
         * copy of the appended bytes, not a file to decode again. Reopening
         * the player from it dropped the stream's sound for good. */
        if (p->mse || p->progressive) continue;
        GBytes *bytes = ns_net_resolve_blob(url, NULL);
        if (!bytes) continue;
        ns_audio_context_open_bytes(js->audio_context, p->token, bytes, TRUE);
        g_bytes_unref(bytes);
    }
}

JSValue
ns_media_play(JSContext *ctx, JSValueConst this_val,
              int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p) return ns_returns_resolved_undefined(ctx, this_val, 0, NULL);
    if (p->network_state == NETWORK_EMPTY) media_load(p);
    if (p->error == MEDIA_ERR_SRC_NOT_SUPPORTED)
        return ns_promise_reject_dom(ctx, "NotSupportedError",
            "Failed to load because no supported source was found.");
    JSValue *pair = g_new0(JSValue, 2);
    JSValue promise = JS_NewPromiseCapability(ctx, pair);
    if (JS_IsException(promise)) {
        g_free(pair);
        return promise;
    }
    g_ptr_array_add(p->play_promises, pair);
    p->can_autoplay = FALSE;
    media_internal_play(p);
    return promise;
}

JSValue
ns_media_pause(JSContext *ctx, JSValueConst this_val,
               int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p) return JS_UNDEFINED;
    if (p->network_state == NETWORK_EMPTY) media_load(p);
    p->can_autoplay = FALSE;
    media_internal_pause(p, TRUE);
    return JS_UNDEFINED;
}

JSValue
ns_media_load(JSContext *ctx, JSValueConst this_val,
              int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    ns_media_player *p = media_player_this(ctx, this_val);
    if (p) media_load(p);
    return JS_UNDEFINED;
}

static void
media_seek_to(ns_media_player *p, double t)
{
    if (!isnan(p->duration) && t > p->duration) t = p->duration;
    p->position = t;
    p->ended = FALSE;
    p->seeking = TRUE;
    media_queue_task(p, "seeking");
    media_backend_seek(p, t);
    p->seeking = FALSE;
    media_queue_task(p, "timeupdate");
    media_queue_task(p, "seeked");
    media_controls_dirty(p);
}

static void
media_apply_muted(ns_media_player *p, gboolean muted)
{
    p->muted = muted ? 1 : 0;
    if (p->fetching && p->js->audio_context)
        ns_audio_context_set_volume(p->js->audio_context, p->token,
                                    media_output_volume(p));
    media_queue_task(p, "volumechange");
    media_controls_dirty(p);
}

JSValue
ns_media_get_current_time(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewFloat64(ctx, p ? media_current_time(p) : 0.0);
}

JSValue
ns_media_set_current_time(JSContext *ctx, JSValueConst this_val,
                          JSValueConst val)
{
    double t = 0.0;
    if (JS_ToFloat64(ctx, &t, val)) return JS_EXCEPTION;
    if (isnan(t) || t < 0.0) t = 0.0;
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p) return JS_UNDEFINED;
    if (p->ready_state < HAVE_METADATA) {
        p->start_position = t;
        return JS_UNDEFINED;
    }
    media_seek_to(p, t);
    return JS_UNDEFINED;
}

JSValue
ns_media_fast_seek(JSContext *ctx, JSValueConst this_val,
                   int argc, JSValueConst *argv)
{
    if (argc < 1) return JS_UNDEFINED;
    return ns_media_set_current_time(ctx, this_val, argv[0]);
}

JSValue
ns_media_get_duration(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewFloat64(ctx, p ? p->duration : NAN);
}

JSValue
ns_media_get_paused(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewBool(ctx, p ? p->paused : TRUE);
}

JSValue
ns_media_get_ended(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewBool(ctx, p && p->ended);
}

JSValue
ns_media_get_seeking(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewBool(ctx, p && p->seeking);
}

JSValue
ns_media_get_readyState(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewInt32(ctx, p ? p->ready_state : HAVE_NOTHING);
}

JSValue
ns_media_get_networkState(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewInt32(ctx, p ? p->network_state : NETWORK_EMPTY);
}

JSValue
ns_media_get_error(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p) return JS_UNDEFINED;
    if (!p->error) return JS_NULL;
    JSValue err = JS_NewObject(ctx);
    ns_obj_adopt_global_proto(ctx, err, "MediaError");
    JS_SetPropertyStr(ctx, err, "_nd_code", JS_NewInt32(ctx, p->error));
    JS_SetPropertyStr(ctx, err, "_nd_message",
                      JS_NewString(ctx, p->error_message ? p->error_message
                                                         : ""));
    return err;
}

JSValue
ns_media_get_volume(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewFloat64(ctx, p ? p->volume : 1.0);
}

JSValue
ns_media_set_volume(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    double vol = 1.0;
    if (JS_ToFloat64(ctx, &vol, val)) return JS_EXCEPTION;
    if (isnan(vol) || vol < 0.0 || vol > 1.0)
        return ns_throw_dom_exception(ctx, "IndexSizeError", 1,
            "The volume provided is outside the range [0, 1].");
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p || p->volume == vol) return JS_UNDEFINED;
    p->volume = vol;
    if (p->fetching && p->js->audio_context)
        ns_audio_context_set_volume(p->js->audio_context, p->token,
                                    media_output_volume(p));
    media_queue_task(p, "volumechange");
    return JS_UNDEFINED;
}

JSValue
ns_media_get_muted(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return JS_NewBool(ctx, p && media_muted(p));
}

JSValue
ns_media_set_muted(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    if (!p) return JS_UNDEFINED;
    gboolean muted = JS_ToBool(ctx, val) > 0;
    if (media_muted(p) == muted && p->muted >= 0) return JS_UNDEFINED;
    media_apply_muted(p, muted);
    if (!muted && !p->paused && p->autoplaying &&
        !p->js->user_ever_activated)
        media_internal_pause(p, TRUE);
    return JS_UNDEFINED;
}

JSValue
ns_media_get_seekable_ranges(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    if (p && p->mse) {
        GArray *ranges = ns_mse_source_seekable(p->mse);
        JSValue result = ns_media_time_ranges_from(ctx, (const double *)(void *)ranges->data,
                                                   ranges->len / 2);
        g_array_free(ranges, TRUE);
        return result;
    }
    double end = p && !isnan(p->duration) ? p->duration : 0.0;
    return ns_media_time_ranges_for(ctx, end);
}

JSValue
ns_media_get_buffered_ranges(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    if (p && p->mse) {
        GArray *ranges = ns_mse_source_buffered(p->mse);
        JSValue result = ns_media_time_ranges_from(ctx, (const double *)(void *)ranges->data,
                                                   ranges->len / 2);
        g_array_free(ranges, TRUE);
        return result;
    }
    double end = p && p->ready_state >= HAVE_METADATA && !isnan(p->duration)
        ? p->duration : 0.0;
    return ns_media_time_ranges_for(ctx, end);
}

JSValue
ns_media_get_played_ranges(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return ns_media_time_ranges_for(ctx, p ? media_current_time(p) : 0.0);
}

static const ns_mp4_track *
media_mse_video_track(ns_media_player *p)
{
    if (!p || !p->mse) return NULL;
    return ns_mse_buffer_track(ns_mse_source_track_buffer(p->mse, NS_MSE_TRACK_VIDEO));
}

JSValue
ns_media_get_video_width(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    ns_image *frames = p ? media_video_frames(p) : NULL;
    int width = frames ? frames->natural_width : 0;
    const ns_mp4_track *track = media_mse_video_track(p);
    if (width <= 0 && track) width = track->width;
    return JS_NewInt32(ctx, width);
}

JSValue
ns_media_get_video_height(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    ns_image *frames = p ? media_video_frames(p) : NULL;
    int height = frames ? frames->natural_height : 0;
    const ns_mp4_track *track = media_mse_video_track(p);
    if (height <= 0 && track) height = track->height;
    return JS_NewInt32(ctx, height);
}

double
ns_media_position(JSContext *ctx, JSValueConst this_val)
{
    ns_media_player *p = media_player_this(ctx, this_val);
    return p ? media_current_time(p) : 0.0;
}

gboolean
ns_media_frame_counts(JSContext *ctx, JSValueConst this_val,
                      guint *presented, guint *dropped)
{
    *presented = *dropped = 0;
    ns_media_player *p = media_player_this(ctx, this_val);
    ns_image *frames = p ? media_video_frames(p) : NULL;
    return frames && frames->video &&
           ns_video_stream_frame_counts(frames->video, presented, dropped);
}

static void
media_controls_dirty(ns_media_player *p)
{
    if (ns_element_get_attr(p->el, "controls"))
        ns_js_request_repaint_node(p->js, p->el);
}

gboolean
ns_media_controls_layout(const ns_node *el, double x, double y,
                         double w, double h, ns_media_controls_rect *out)
{
    if (!ns_node_is_media_element(el) ||
        !ns_element_get_attr(el, "controls") || !(w >= 40.0) || !(h >= 16.0))
        return FALSE;
    double bar_h = MIN(h, NS_MEDIA_CONTROLS_BAR_H);
    out->x = x;
    out->y = y + h - bar_h;
    out->w = w;
    out->h = bar_h;
    out->play_x = x;
    out->play_w = bar_h;
    out->mute_w = bar_h;
    out->mute_x = x + w - bar_h;
    out->time_w = w >= 2 * bar_h + NS_MEDIA_CONTROLS_TIME_W + 60.0
        ? NS_MEDIA_CONTROLS_TIME_W : 0.0;
    out->time_x = out->mute_x - out->time_w;
    out->seek_x0 = x + bar_h + 6.0;
    out->seek_x1 = MAX(out->seek_x0, out->time_x - 8.0);
    return TRUE;
}

ns_media_control
ns_media_controls_hit(const ns_media_controls_rect *r, double px, double py)
{
    if (!r || px < r->x || px > r->x + r->w || py < r->y ||
        py > r->y + r->h)
        return NS_MEDIA_CONTROL_NONE;
    if (px < r->play_x + r->play_w) return NS_MEDIA_CONTROL_PLAY;
    if (px >= r->mute_x) return NS_MEDIA_CONTROL_MUTE;
    if (px >= r->seek_x0 - 6.0 && px <= r->seek_x1 + 6.0)
        return NS_MEDIA_CONTROL_SEEK;
    return NS_MEDIA_CONTROL_NONE;
}

void
ns_media_controls_get(ns_js *js, const ns_node *el,
                      ns_media_controls_state *out)
{
    memset(out, 0, sizeof *out);
    out->paused = TRUE;
    out->duration = NAN;
    out->audible = !ns_node_is_element_named(el, "video");
    ns_media_player *p = media_player_find(js, el);
    if (!p) return;
    out->paused = p->paused;
    out->muted = media_muted(p);
    out->audible = !p->video || p->fetching;
    out->position = media_current_time(p);
    out->duration = p->duration;
}

void
ns_media_controls_activate(ns_js *js, const ns_node *el,
                           ns_media_control control, double fraction)
{
    if (!js || !ns_node_is_media_element(el)) return;
    ns_media_player *p = media_player_find(js, el);
    if (!p && control == NS_MEDIA_CONTROL_PLAY) {
        p = media_player_for(js, (ns_node *)el);
        if (p && p->network_state == NETWORK_EMPTY) media_load(p);
    }
    if (!p) return;
    switch (control) {
    case NS_MEDIA_CONTROL_PLAY:
        p->can_autoplay = FALSE;
        if (p->paused || p->ended) media_internal_play(p);
        else media_internal_pause(p, TRUE);
        break;
    case NS_MEDIA_CONTROL_SEEK:
        if (p->ready_state >= HAVE_METADATA && !isnan(p->duration))
            media_seek_to(p, CLAMP(fraction, 0.0, 1.0) * p->duration);
        break;
    case NS_MEDIA_CONTROL_MUTE:
        media_apply_muted(p, !media_muted(p));
        break;
    default:
        break;
    }
}
