/* Northstar — one page host session: the open page, its back/forward cache
   and the last rendered frame.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "page_session.h"
#include "libnorthstar.h"
#include "mainctx.h"
#include "net.h"
#include "trace.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NS_BFCACHE_MAX 4

struct ns_page_session {
    unsigned char *fb;
    int            max_w;
    int            max_h;
    ns_browser    *cur;
    ns_browser    *bf[NS_BFCACHE_MAX];
    int            bf_n;
    int            tick_budget_ms;
    int            frame_valid;
    long           frame_sx;
    long           frame_sy;
    int            frame_w;
    int            frame_h;
    double         frame_scale;
    char          *post_url;
    char          *post_body;
    size_t         post_len;
    char          *post_ct;
    GSource       *wake;
    ns_page_session_wake_cb wake_cb;
    gpointer       wake_data;
    gboolean       rendering;
    gboolean       wake_posted;
};

typedef struct {
    GSource          source;
    ns_page_session *session;
} session_wake_source;

static gboolean
session_wake_due(ns_page_session *s)
{
    return s->cur && s->wake_cb && !s->rendering && !s->wake_posted &&
           ns_browser_needs_frame(s->cur);
}

static gboolean
session_wake_prepare(GSource *source, gint *timeout)
{
    ns_page_session *s = ((session_wake_source *)source)->session;
    *timeout = -1;
    if (session_wake_due(s))
        return TRUE;
    gint64 due = s->cur && !s->wake_posted ? ns_browser_next_wake_us(s->cur)
                                           : 0;
    if (due > 0) {
        gint64 wait_ms = (due - g_get_monotonic_time() + 999) / 1000;
        *timeout = (gint)CLAMP(wait_ms, 0, G_MAXINT);
    }
    return FALSE;
}

static gboolean
session_wake_check(GSource *source)
{
    return session_wake_due(((session_wake_source *)source)->session);
}

static gboolean
session_wake_dispatch(GSource *source, GSourceFunc callback,
                      gpointer user_data)
{
    (void)callback;
    (void)user_data;
    ns_page_session *s = ((session_wake_source *)source)->session;
    if (session_wake_due(s)) {
        s->wake_posted = TRUE;
        s->wake_cb(s->wake_data);
    }
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs session_wake_funcs = {
    .prepare = session_wake_prepare,
    .check = session_wake_check,
    .dispatch = session_wake_dispatch,
};

void
ns_page_session_set_wake(ns_page_session *s, ns_page_session_wake_cb cb,
                         gpointer user_data)
{
    if (!s)
        return;
    s->wake_cb = cb;
    s->wake_data = user_data;
    s->wake_posted = FALSE;
    if (s->wake || !cb)
        return;
    s->wake = g_source_new(&session_wake_funcs, sizeof(session_wake_source));
    ((session_wake_source *)s->wake)->session = s;
    g_source_attach(s->wake, ns_engine_context());
}

void
ns_page_session_set_frame_time(ns_page_session *s, gint64 frame_time_us)
{
    if (s && s->cur)
        ns_browser_set_frame_time(s->cur, frame_time_us);
}

static void
session_bfcache_park_or_close(ns_page_session *s, ns_browser *b)
{
    if (!b)
        return;
    if (!ns_browser_bfcache_eligible(b)) {
        ns_browser_close(b);
        return;
    }
    ns_browser_bfcache_park(b);
    if (s->bf_n >= NS_BFCACHE_MAX) {
        ns_browser_close(s->bf[0]);
        for (int i = 1; i < s->bf_n; i++)
            s->bf[i - 1] = s->bf[i];
        s->bf_n--;
    }
    s->bf[s->bf_n++] = b;
}

static ns_browser *
session_bfcache_take(ns_page_session *s, const char *url)
{
    if (!url)
        return NULL;
    for (int i = s->bf_n - 1; i >= 0; i--) {
        char *u = ns_browser_url(s->bf[i]);
        int match = u && strcmp(u, url) == 0;
        free(u);
        if (!match)
            continue;
        ns_browser *b = s->bf[i];
        for (int j = i + 1; j < s->bf_n; j++)
            s->bf[j - 1] = s->bf[j];
        s->bf_n--;
        return b;
    }
    return NULL;
}

static void
session_stash_post(ns_page_session *s, const char *href)
{
    if (!s->cur)
        return;
    size_t len = 0;
    char *ct = NULL;
    char *pb = ns_browser_take_post(s->cur, &len, &ct);
    if (!pb)
        return;
    free(s->post_url);
    free(s->post_body);
    free(s->post_ct);
    s->post_url = (href && *href) ? strdup(href) : NULL;
    s->post_body = pb;
    s->post_len = len;
    s->post_ct = ct;
}

static void
session_clear_post(ns_page_session *s)
{
    free(s->post_url);
    free(s->post_body);
    free(s->post_ct);
    s->post_url = NULL;
    s->post_body = NULL;
    s->post_ct = NULL;
    s->post_len = 0;
}

static int
clamp(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static char *
empty_to_null(char *s)
{
    if (s && !*s) {
        free(s);
        return NULL;
    }
    return s;
}

static void
scrub_line_breaks(char *s)
{
    if (!s) return;
    for (char *p = s; *p; p++)
        if (*p == '\r' || *p == '\n') *p = ' ';
}

static void
engine_init_once(void)
{
    static gsize inited;
    if (g_once_init_enter(&inited)) {
        ns_browser_init();
        g_once_init_leave(&inited, 1);
    }
}

ns_page_session *
ns_page_session_new(int max_width, int max_height)
{
    if (max_width <= 0 || max_height <= 0)
        return NULL;
    engine_init_once();
    ns_page_session *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->fb = malloc((size_t)max_width * 4u * (size_t)max_height);
    if (!s->fb) {
        free(s);
        return NULL;
    }
    s->max_w = max_width;
    s->max_h = max_height;
    s->tick_budget_ms = 16;
    s->frame_scale = 1.0;
    return s;
}

void
ns_page_session_free(ns_page_session *s)
{
    if (!s)
        return;
    if (s->wake) {
        g_source_destroy(s->wake);
        g_source_unref(s->wake);
        s->wake = NULL;
    }
    session_clear_post(s);
    for (int i = 0; i < s->bf_n; i++)
        ns_browser_close(s->bf[i]);
    if (s->cur) {
        ns_browser *cur = s->cur;
        s->cur = NULL;
        ns_browser_close(cur);
    }
    free(s->fb);
    free(s);
}

static void
fill_page_info(ns_page_session *s, const char *fallback_url,
               ns_page_info *out)
{
    memset(out, 0, sizeof *out);
    if (!s->cur) {
        out->url = fallback_url ? strdup(fallback_url) : NULL;
        return;
    }
    const char *ip = NULL;
    out->ok = 1;
    ns_browser_page_size(s->cur, &out->page_width, &out->page_height);
    out->title = ns_browser_title(s->cur);
    out->url = ns_browser_url(s->cur);
    if (!out->url && fallback_url)
        out->url = strdup(fallback_url);
    out->nav = empty_to_null(ns_browser_take_pending_nav(s->cur));
    out->security = ns_browser_security(s->cur, &ip);
    out->remote_ip = ip && *ip ? strdup(ip) : NULL;
}

void
ns_page_info_clear(ns_page_info *info)
{
    if (!info)
        return;
    free(info->title);
    free(info->url);
    free(info->nav);
    free(info->remote_ip);
    memset(info, 0, sizeof *info);
}

int
ns_page_session_open(ns_page_session *s, const char *url, int width,
                     int height, int settle_ms, int history,
                     int user_activated, ns_page_info *out)
{
    if (!s || !out)
        return -1;
    int vw = clamp(width, 1, s->max_w);
    int vh = clamp(height, 1, s->max_h);
    ns_browser *restored = (history && url) ? session_bfcache_take(s, url)
                                            : NULL;
    char *referrer = (!history && s->cur) ? ns_browser_url(s->cur) : NULL;
    if (s->cur) {
        ns_browser *previous = s->cur;
        s->cur = NULL;
        session_bfcache_park_or_close(s, previous);
    }
    if (!restored)
        ns_browser_set_next_navigation(
            referrer && url && ns_url_same_origin(referrer, url) ? referrer
                                                                 : NULL,
            user_activated != 0);
    free(referrer);
    s->frame_valid = 0;
    ns_net_log_clear();
    if (restored) {
        ns_browser_bfcache_restore(restored, vw, (double)vh);
        s->cur = restored;
    } else if (url && s->post_body && s->post_url &&
               strcmp(url, s->post_url) == 0) {
        s->cur = ns_browser_open_post_viewport(url, vw, vh, settle_ms,
                                               s->post_body, s->post_len,
                                               s->post_ct);
    } else {
        s->cur = url ? ns_browser_open_viewport(url, vw, vh, settle_ms)
                     : NULL;
    }
    session_clear_post(s);
    s->wake_posted = FALSE;
    fill_page_info(s, url, out);
    return s->cur ? 0 : -1;
}

int
ns_page_session_set_viewport(ns_page_session *s, int width, int height,
                             ns_page_info *out)
{
    if (out)
        memset(out, 0, sizeof *out);
    if (!s || !s->cur)
        return -1;
    int vw = clamp(width, 1, s->max_w);
    int vh = clamp(height, 1, s->max_h);
    s->frame_valid = 0;
    if (ns_browser_set_viewport(s->cur, vw, vh) != 0)
        return -1;
    if (out) {
        out->ok = 1;
        ns_browser_page_size(s->cur, &out->page_width, &out->page_height);
    }
    return 0;
}

void
ns_page_frame_clear(ns_page_frame *frame)
{
    if (!frame)
        return;
    free(frame->nav);
    free(frame->camera);
    free(frame->download);
    memset(frame, 0, sizeof *frame);
}

int
ns_page_session_render(ns_page_session *s, int width, int height,
                       int scroll_x, int scroll_y, double scale,
                       int caret_active, ns_page_frame *out)
{
    if (!s || !out)
        return -1;
    memset(out, 0, sizeof *out);
    out->scroll_x = -1;
    out->scroll_y = -1;
    if (!s->cur)
        return -1;
    if (!(scale > 0))
        scale = 1.0;
    int vw = clamp(width, 1, s->max_w);
    int vh = clamp(height, 1, s->max_h);
    int stride = vw * 4;
    long sx = scroll_x, sy = scroll_y;
    s->rendering = TRUE;
    gint64 frame_start = ns_trace_now();
    gint64 phase_start = frame_start;
    int ticked = s->frame_valid ? ns_browser_tick(s->cur, s->tick_budget_ms)
                                : 0;
    ns_trace_complete("frame", "tick", phase_start, NULL);
    int requested_scroll_y = -1;
    if (ns_browser_take_pending_scroll_y(s->cur, &requested_scroll_y))
        sy = requested_scroll_y;
    int page_w = 0, page_h = 0;
    ns_browser_page_size(s->cur, &page_w, &page_h);
    if (requested_scroll_y >= 0) {
        int max_scroll_y = page_h - (int)ceil((double)vh / scale);
        if (max_scroll_y < 0) max_scroll_y = 0;
        if (requested_scroll_y > max_scroll_y)
            requested_scroll_y = max_scroll_y;
        sy = requested_scroll_y;
    }
    int requested_scroll_x = -1;
    int snap_x = (int)sx, snap_y = (int)sy;
    if (ns_browser_snap_document(s->cur, (double)vw / scale,
                                 (double)vh / scale, (int)s->frame_sx,
                                 (int)s->frame_sy, &snap_x, &snap_y)) {
        if (snap_x != (int)sx) {
            sx = snap_x;
            requested_scroll_x = snap_x;
        }
        if (snap_y != (int)sy) {
            sy = snap_y;
            requested_scroll_y = snap_y;
        }
    }
    int caret_changed = ns_browser_set_caret_blink_active(s->cur,
                                                          caret_active != 0);
    int unchanged = s->frame_valid && ticked == 0 && !caret_changed &&
                    sx == s->frame_sx && sy == s->frame_sy &&
                    vw == s->frame_w && vh == s->frame_h &&
                    scale == s->frame_scale;
    if (!unchanged) {
        phase_start = ns_trace_now();
        int painted = ns_browser_render_argb32(s->cur, (int)sx, (int)sy, vw, vh,
                                               scale, s->fb, stride);
        ns_trace_complete("frame", "paint", phase_start, NULL);
        if (painted == 0) {
            s->frame_valid = 1;
            s->frame_sx = sx;
            s->frame_sy = sy;
            s->frame_w = vw;
            s->frame_h = vh;
            s->frame_scale = scale;
        } else {
            memset(s->fb, 0xff, (size_t)stride * (size_t)vh);
            s->frame_valid = 0;
        }
    }
    out->ok = 1;
    out->width = vw;
    out->height = vh;
    out->stride = stride;
    out->page_w = page_w;
    out->page_h = page_h;
    out->scroll_y = requested_scroll_y;
    out->scroll_x = requested_scroll_x;
    out->unchanged = unchanged;
    out->pixels = s->fb;
    out->nav = empty_to_null(ns_browser_take_pending_nav(s->cur));
    scrub_line_breaks(out->nav);
    if (out->nav)
        session_stash_post(s, out->nav);
    out->camera = empty_to_null(ns_browser_take_pending_camera(s->cur));
    scrub_line_breaks(out->camera);
    out->download = empty_to_null(ns_browser_take_pending_download(s->cur));
    scrub_line_breaks(out->download);
    out->animating = ns_browser_continuous(s->cur) ? 1 : 0;
    out->caret_blinking = ns_browser_caret_blinking(s->cur) ? 1 : 0;
    out->clipboard = ns_browser_has_pending_clipboard(s->cur) ? 1 : 0;
    ns_trace_complete("frame", unchanged ? "frame (unchanged)" : "frame",
                      frame_start, NULL);
    s->rendering = FALSE;
    s->wake_posted = FALSE;
    return 0;
}

char *
ns_page_session_link_at(ns_page_session *s, int x, int y, char **out_cursor)
{
    if (out_cursor)
        *out_cursor = NULL;
    if (!s || !s->cur)
        return NULL;
    if (out_cursor)
        *out_cursor = empty_to_null(ns_browser_cursor_at(s->cur, x, y));
    return empty_to_null(ns_browser_link_at(s->cur, x, y));
}

int
ns_page_session_hover(ns_page_session *s, int x, int y, char **out_href,
                      char **out_cursor)
{
    if (out_href)
        *out_href = NULL;
    if (out_cursor)
        *out_cursor = NULL;
    if (!s || !s->cur)
        return -1;
    int changed = ns_browser_hover(s->cur, x, y);
    if (changed > 0)
        s->frame_valid = 0;
    char *href = ns_page_session_link_at(s, x, y, out_cursor);
    if (out_href)
        *out_href = href;
    else
        free(href);
    return changed > 0 ? 1 : 0;
}

char *
ns_page_session_click(ns_page_session *s, int x, int y, int mods)
{
    if (!s || !s->cur)
        return NULL;
    s->frame_valid = 0;
    char *href = ns_browser_press(s->cur, x, y, mods);
    session_stash_post(s, href);
    return empty_to_null(href);
}

char *
ns_page_session_release(ns_page_session *s, int *out_changed)
{
    if (out_changed)
        *out_changed = 0;
    if (!s || !s->cur)
        return NULL;
    s->frame_valid = 0;
    int changed = 0;
    char *href = ns_browser_release_click(s->cur, &changed);
    session_stash_post(s, href);
    if (out_changed)
        *out_changed = changed > 0 ? 1 : 0;
    return empty_to_null(href);
}

char *
ns_page_session_select(ns_page_session *s, int kind, int x, int y)
{
    if (!s || !s->cur)
        return NULL;
    s->frame_valid = 0;
    return empty_to_null(ns_browser_select(s->cur, kind, x, y));
}

char *
ns_page_session_key(ns_page_session *s, int kind, const char *key,
                    const char *code, int keycode, int mods,
                    int *out_prevented)
{
    if (out_prevented)
        *out_prevented = 0;
    if (!s || !s->cur)
        return NULL;
    s->frame_valid = 0;
    int prevented = 0;
    char *href = ns_browser_key_full(s->cur, kind, key ? key : "",
                                     code ? code : "", keycode, mods,
                                     &prevented);
    session_stash_post(s, href);
    if (out_prevented)
        *out_prevented = prevented;
    return empty_to_null(href);
}

int
ns_page_session_contextmenu(ns_page_session *s, int x, int y)
{
    if (!s || !s->cur)
        return 0;
    return ns_browser_contextmenu(s->cur, x, y);
}

int
ns_page_session_scroll(ns_page_session *s, int x, int y, int dx, int dy)
{
    if (!s || !s->cur)
        return 0;
    int consumed = ns_browser_scroll_at(s->cur, x, y, dx, dy);
    if (consumed)
        s->frame_valid = 0;
    return consumed ? 1 : 0;
}

int
ns_page_session_scrollbar(ns_page_session *s, int kind, int x, int y)
{
    if (!s || !s->cur)
        return 0;
    if (kind == 2) {
        ns_browser_scrollbar_release(s->cur);
        return 1;
    }
    int hit = kind == 0 ? ns_browser_scrollbar_press(s->cur, x, y)
                        : ns_browser_scrollbar_drag(s->cur, x, y);
    if (hit)
        s->frame_valid = 0;
    return hit ? 1 : 0;
}

int
ns_page_session_drop_files(ns_page_session *s, int x, int y,
                           const char *const *paths, int n_paths)
{
    if (!s || !s->cur || !paths || n_paths <= 0)
        return 0;
    int changed = ns_browser_drop_files(s->cur, x, y, paths, n_paths);
    if (changed > 0)
        s->frame_valid = 0;
    return changed > 0 ? 1 : 0;
}

int
ns_page_session_find(ns_page_session *s, const char *query,
                     int case_sensitive, int direction, int from_y,
                     int *out_total, int *out_current, int *out_scroll_y)
{
    if (out_total) *out_total = 0;
    if (out_current) *out_current = 0;
    if (out_scroll_y) *out_scroll_y = 0;
    if (!s || !s->cur)
        return -1;
    s->frame_valid = 0;
    int total = 0, current = 0, scroll_y = 0;
    ns_browser_find(s->cur, query ? query : "", case_sensitive, direction,
                    from_y, &total, &current, &scroll_y);
    if (out_total) *out_total = total;
    if (out_current) *out_current = current;
    if (out_scroll_y) *out_scroll_y = scroll_y;
    return 0;
}

char *
ns_page_session_clipboard(ns_page_session *s)
{
    if (!s || !s->cur)
        return NULL;
    return ns_browser_take_pending_clipboard(s->cur);
}

char *
ns_page_session_media_at(ns_page_session *s, int x, int y, int *out_is_video,
                         int *out_stream)
{
    if (out_is_video) *out_is_video = 0;
    if (out_stream) *out_stream = 0;
    if (!s || !s->cur)
        return NULL;
    int is_video = 0, stream = 0;
    char *url = ns_browser_media_at(s->cur, x, y, &is_video, &stream);
    if (out_is_video) *out_is_video = is_video;
    if (out_stream) *out_stream = stream;
    return empty_to_null(url);
}

void
ns_page_session_resolve_camera(ns_page_session *s, const char *origin,
                               int allow)
{
    if (!s || !s->cur || !origin)
        return;
    ns_browser_resolve_camera(s->cur, origin, allow);
    s->frame_valid = 0;
}

char *
ns_page_session_eval(ns_page_session *s, const char *src)
{
    if (!s || !s->cur)
        return NULL;
    s->frame_valid = 0;
    return ns_browser_eval(s->cur, src ? src : "");
}

char *
ns_page_session_dump(ns_page_session *s, const char *kind)
{
    if (!kind)
        return NULL;
    if (strcmp(kind, "network") == 0)
        return ns_net_log_dump();
    if (!s || !s->cur)
        return NULL;
    if (strcmp(kind, "dom") == 0)
        return ns_browser_dump_dom(s->cur);
    if (strcmp(kind, "layout") == 0)
        return ns_browser_dump_layout(s->cur);
    if (strcmp(kind, "text") == 0)
        return ns_browser_render_text(s->cur);
    if (strcmp(kind, "performance") == 0)
        return ns_browser_dump_performance(s->cur);
    return NULL;
}

char *
ns_page_session_console(ns_page_session *s)
{
    if (!s || !s->cur)
        return NULL;
    return ns_browser_console_drain(s->cur);
}

int
ns_page_session_export(ns_page_session *s, const char *path)
{
    if (!s || !s->cur || !path)
        return -1;
    s->frame_valid = 0;
    return ns_browser_render_image(s->cur, path);
}

GPtrArray *
ns_page_session_print(ns_page_session *s, ns_print_setup *out_setup)
{
    if (!s || !s->cur || !out_setup)
        return NULL;
    s->frame_valid = 0;
    return ns_browser_print_pages(s->cur, out_setup);
}
