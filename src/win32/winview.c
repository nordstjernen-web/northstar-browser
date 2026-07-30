/* winview.c - Native Win32 page view backed by the renderer protocol. */

#include "winview.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>
#include <cairo.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "appmain.h"
#include "audio/audio.h"
#include "i18n.h"
#include "net.h"
#include "proc_limits.h"
#include "rproc_http.h"
#include "security.h"

#define NS_WINVIEW_CLASS L"NorthstarWinView"
#define NS_DEVTOOLS_CLASS L"NorthstarDeveloperTools"
#define NS_TOUNICODE_KEEP_STATE 0x4
#define NS_WM_RESULT (WM_APP + 41)
#define NS_TIMER_ANIM 1
#define NS_TIMER_CONSOLE 2

enum {
    NS_FIND_EDIT = 100,
    NS_FIND_LABEL,
    NS_FIND_PREV,
    NS_FIND_NEXT,
    NS_FIND_CLOSE,
    NS_PERM_LABEL,
    NS_PERM_ALLOW,
    NS_PERM_DENY,
    NS_DEV_TAB,
    NS_DEV_OUTPUT,
    NS_DEV_INPUT,
    NS_DEV_INSPECT,
    NS_DEV_REFRESH,
    NS_DEV_CLEAR
};

enum {
    NS_CTX_BACK = 400,
    NS_CTX_FORWARD,
    NS_CTX_RELOAD,
    NS_CTX_COPY_URL,
    NS_CTX_OPEN_LINK,
    NS_CTX_COPY_LINK,
    NS_CTX_COPY_SELECTION,
    NS_CTX_SELECT_ALL,
    NS_CTX_SAVE_PDF,
    NS_CTX_SAVE_PNG
};

typedef enum NsRequestType {
    NS_REQ_LOAD,
    NS_REQ_RENDER,
    NS_REQ_CONTEXT,
    NS_REQ_CLICK,
    NS_REQ_VIEWPORT,
    NS_REQ_KEY,
    NS_REQ_SELECT,
    NS_REQ_HOVER,
    NS_REQ_RELEASE,
    NS_REQ_FIND,
    NS_REQ_EXPORT,
    NS_REQ_CONSOLE,
    NS_REQ_EVAL,
    NS_REQ_DUMP,
    NS_REQ_DROP_FILES,
    NS_REQ_SCROLL,
    NS_REQ_SCROLLBAR,
    NS_REQ_CAMERA,
    NS_REQ_QUIT
} NsRequestType;

typedef enum NsResultType {
    NS_RES_PAGE,
    NS_RES_FRAME,
    NS_RES_CONTEXT,
    NS_RES_CLICK,
    NS_RES_VIEWPORT,
    NS_RES_KEY,
    NS_RES_SELECT,
    NS_RES_COPY,
    NS_RES_HOVER,
    NS_RES_RELEASE,
    NS_RES_FIND,
    NS_RES_EXPORT,
    NS_RES_CONSOLE,
    NS_RES_EVAL,
    NS_RES_DUMP,
    NS_RES_DROP_FILES,
    NS_RES_SCROLL,
    NS_RES_SCROLLBAR
} NsResultType;

enum {
    NS_DEV_CONSOLE,
    NS_DEV_NETWORK,
    NS_DEV_PERFORMANCE,
    NS_DEV_LAYOUT,
    NS_DEV_ELEMENTS,
    NS_DEV_COUNT
};

typedef struct NsRequest {
    NsRequestType type;
    int seq;
    char *url;
    int viewport_width;
    int viewport_height;
    int width;
    int height;
    int scroll_x;
    int scroll_y;
    double scale;
    int x;
    int y;
    int dx;
    int dy;
    int mods;
    int kind;
    int keycode;
    char *key;
    char *code;
    char *query;
    int find_direction;
    int find_from_y;
    char *export_destination;
    char *paths;
    int dump_tab;
    gboolean inspect;
    gboolean history;
    gboolean user_activated;
    gboolean caret_active;
} NsRequest;

typedef struct NsResult {
    NsResultType type;
    int seq;
    gboolean ok;
    int page_width;
    int page_height;
    char *title;
    char *url;
    char *nav;
    int security;
    char *remote_ip;
    char *camera;
    char *download;
    char *audio;
    unsigned char *pixels;
    int width;
    int height;
    int stride;
    char *text;
    char *cursor;
    int kind;
    int prevented;
    gboolean animating;
    gboolean caret_blinking;
    gboolean frame_unchanged;
    int requested_scroll_y;
    int find_total;
    int find_current;
    int find_scroll_y;
    int dump_tab;
    gboolean inspect;
    int fallback_x;
    int fallback_y;
} NsResult;

struct NsWinView {
    HWND hwnd;
    HINSTANCE instance;
    HWND find_edit;
    HWND find_label;
    HWND find_prev;
    HWND find_next;
    HWND find_close;
    HWND perm_label;
    HWND perm_allow;
    HWND perm_deny;
    HWND devtools;
    HWND dev_tab;
    HWND dev_output;
    HWND dev_input;
    HWND dev_inspect;
    HWND dev_refresh;
    HWND dev_clear;
    HFONT ui_font;
    GThread *thread;
    GAsyncQueue *queue;
    ns_rproc_http *proc;
    GMutex proc_lock;
    gint private_mode;
    gint closed;
    NsAudioContext *audio_context;
    NsWinViewNotify notify;
    void *notify_data;
    char *current_url;
    char *current_title;
    char *remote_ip;
    int security;
    int page_width;
    int page_height;
    int scroll_x;
    int scroll_y;
    gboolean opened;
    unsigned char *frame_pixels;
    int frame_width;
    int frame_height;
    int frame_stride;
    gboolean render_inflight;
    gboolean render_pending;
    int render_restarts;
    gboolean hover_inflight;
    gboolean hover_pending;
    int hover_x;
    int hover_y;
    gboolean has_selection;
    gboolean dragging;
    gboolean drag_anchored;
    gboolean scrollbar_probe;
    gboolean scrollbar_dragging;
    gboolean scrollbar_last_valid;
    int drag_start_x;
    int drag_start_y;
    int drag_last_x;
    int drag_last_y;
    int pointer_x;
    int pointer_y;
    char *context_link;
    POINT context_point;
    gboolean find_visible;
    gboolean permission_visible;
    gboolean permission_pending;
    char *permission_origin;
    GPtrArray *history;
    int history_index;
    gboolean pending_record;
    char *deferred_url;
    gboolean deferred_record;
    gboolean deferred_history;
    gboolean deferred_activated;
    int javascript_redirects;
    double scale;
    gboolean loading;
    int load_seq;
    int render_seq;
    int context_seq;
    int click_seq;
    int viewport_seq;
    int key_seq;
    int select_seq;
    int hover_seq;
    int find_seq;
    int last_viewport_width;
    int last_viewport_height;
    gboolean page_animating;
    gboolean caret_blinking;
    char *dev_text[NS_DEV_COUNT];
    gboolean devtools_open;
    WCHAR pending_high_surrogate;
};

static LRESULT CALLBACK ns_winview_window_proc(HWND hwnd, UINT message,
                                                WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK ns_devtools_window_proc(HWND hwnd, UINT message,
                                                WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK ns_edit_subclass_proc(HWND hwnd, UINT message,
                                              WPARAM wparam, LPARAM lparam,
                                              UINT_PTR id, DWORD_PTR data);

static wchar_t *
ns_utf8_to_wide(const char *text)
{
    if (!text)
        return g_new0(wchar_t, 1);
    glong items = 0;
    gunichar2 *wide = g_utf8_to_utf16(text, -1, NULL, &items, NULL);
    if (!wide)
        return g_new0(wchar_t, 1);
    return (wchar_t *)wide;
}

static char *
ns_wide_to_utf8(const wchar_t *text)
{
    if (!text)
        return g_strdup("");
    return g_utf16_to_utf8((const gunichar2 *)text, -1, NULL, NULL, NULL);
}

static char *
ns_window_text_utf8(HWND hwnd)
{
    int length = GetWindowTextLengthW(hwnd);
    wchar_t *wide = g_new(wchar_t, (gsize)length + 1);
    GetWindowTextW(hwnd, wide, length + 1);
    char *text = ns_wide_to_utf8(wide);
    g_free(wide);
    return text ? text : g_strdup("");
}

static void
ns_set_window_text_utf8(HWND hwnd, const char *text)
{
    wchar_t *wide = ns_utf8_to_wide(text ? text : "");
    SetWindowTextW(hwnd, wide);
    g_free(wide);
}

static HFONT
ns_create_ui_font(UINT dpi)
{
    NONCLIENTMETRICSW metrics = { .cbSize = sizeof metrics };
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof metrics,
                                   &metrics, 0, dpi ? dpi : 96)) {
        HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
        if (font)
            return font;
    }
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static void
ns_destroy_ui_font(HFONT font)
{
    if (font && font != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(font);
}

static void
ns_set_control_font(NsWinView *view, HWND control)
{
    SendMessageW(control, WM_SETFONT, (WPARAM)view->ui_font, TRUE);
}

static int
ns_settle_ms(void)
{
    const char *value = g_getenv(NS_PROC_SETTLE_ENV);
    if (value && *value) {
        int parsed = atoi(value);
        if (parsed >= 0 && parsed <= 10000)
            return parsed;
    }
    return NS_PROC_SETTLE_MS;
}

static void
ns_request_free(NsRequest *request)
{
    if (!request)
        return;
    g_free(request->url);
    g_free(request->key);
    g_free(request->code);
    g_free(request->query);
    g_free(request->export_destination);
    g_free(request->paths);
    g_free(request);
}

static char *
ns_take_cstr(char *owned)
{
    if (!owned)
        return NULL;
    char *copy = g_strdup(owned);
    free(owned);
    return copy;
}

static void
ns_result_free(NsResult *result)
{
    if (!result)
        return;
    g_free(result->title);
    g_free(result->url);
    g_free(result->nav);
    g_free(result->remote_ip);
    g_free(result->camera);
    g_free(result->download);
    g_free(result->audio);
    g_free(result->pixels);
    g_free(result->text);
    g_free(result->cursor);
    g_free(result);
}

static void
ns_notify(NsWinView *view, NsWinViewEvent event, const char *text)
{
    if (view->notify)
        view->notify(view, event, text, view->notify_data);
}

static gboolean
ns_view_closed(NsWinView *view)
{
    return g_atomic_int_get(&view->closed) != 0;
}

static void
ns_push_request(NsWinView *view, NsRequest *request)
{
    g_async_queue_push(view->queue, request);
}

static ns_rproc_http *
ns_swap_proc(NsWinView *view, ns_rproc_http *replacement)
{
    g_mutex_lock(&view->proc_lock);
    ns_rproc_http *old = view->proc;
    view->proc = replacement;
    g_mutex_unlock(&view->proc_lock);
    return old;
}

static char *
ns_renderer_path(void)
{
    const char *configured = g_getenv(NS_PROC_RENDERER_ENV);
    if (configured && *configured)
        return g_strdup(configured);
    const char *exe = ns_app_self_exe();
    if (exe) {
        char *dir = g_path_get_dirname(exe);
        char *name = g_strconcat(NS_PROC_RENDERER_NAME, ".exe", NULL);
        char *candidate = g_build_filename(dir, name, NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE)) {
            g_free(name);
            g_free(dir);
            return candidate;
        }
        g_free(candidate);
        g_free(name);
        g_free(dir);
    }
    return g_strconcat(NS_PROC_RENDERER_NAME, ".exe", NULL);
}

static gboolean
ns_audio_blob_command(NsWinView *view, const char *line)
{
    gboolean reload = FALSE;
    const char *cursor = NULL;
    if (g_str_has_prefix(line, "open "))
        cursor = line + 5;
    else if (g_str_has_prefix(line, "reload ")) {
        cursor = line + 7;
        reload = TRUE;
    } else {
        return FALSE;
    }
    while (*cursor == ' ')
        cursor++;
    const char *token_end = strchr(cursor, ' ');
    if (!token_end)
        return FALSE;
    char *token = g_strndup(cursor, token_end - cursor);
    const char *url = token_end + 1;
    while (*url == ' ')
        url++;
    if (!g_str_has_prefix(url, "blob:")) {
        g_free(token);
        return FALSE;
    }
    GBytes *bytes = ns_net_resolve_blob(url, NULL);
    if (bytes) {
        ns_audio_context_dispatch_blob(view->audio_context, token, bytes,
                                       reload);
        g_bytes_unref(bytes);
    }
    g_free(token);
    return TRUE;
}

static void
ns_audio_pump(NsWinView *view, const char *commands)
{
    if (!commands || !*commands)
        return;
    if (!view->audio_context)
        view->audio_context = ns_audio_context_new();
    char **lines = g_strsplit(commands, "\x1f", -1);
    for (int i = 0; lines[i]; i++) {
        if (!*lines[i])
            continue;
        if (!ns_audio_blob_command(view, lines[i]))
            ns_audio_context_dispatch(view->audio_context, lines[i]);
    }
    g_strfreev(lines);
}

static void
ns_post_result(NsWinView *view, NsResult *result)
{
    if (!PostMessageW(view->hwnd, NS_WM_RESULT, 0, (LPARAM)result))
        ns_result_free(result);
}

static gpointer
ns_worker_main(gpointer data)
{
    NsWinView *view = data;
    char *renderer = ns_renderer_path();
    for (;;) {
        NsRequest *request = g_async_queue_pop(view->queue);
        if (request->type == NS_REQ_QUIT) {
            ns_request_free(request);
            break;
        }
        if (!view->proc && !ns_view_closed(view))
            ns_swap_proc(view, ns_rproc_http_spawn_shm_ex(
                renderer, NS_PROC_MAX_WIDTH, NS_PROC_MAX_HEIGHT,
                g_atomic_int_get(&view->private_mode) != 0));

        if (request->type == NS_REQ_LOAD) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_PAGE;
            result->seq = request->seq;
            ns_rproc_http_page page = {0};
            int rc = view->proc ? ns_rproc_http_open_ex(
                view->proc, request->url, request->viewport_width,
                request->viewport_height, ns_settle_ms(), request->history,
                request->user_activated, &page) : -1;
            if (rc != 0 && view->proc && !ns_view_closed(view)) {
                ns_rproc_http_close(ns_swap_proc(view, NULL));
                ns_swap_proc(view, ns_rproc_http_spawn_shm_ex(
                    renderer, NS_PROC_MAX_WIDTH, NS_PROC_MAX_HEIGHT,
                    g_atomic_int_get(&view->private_mode) != 0));
                rc = view->proc ? ns_rproc_http_open_ex(
                    view->proc, request->url, request->viewport_width,
                    request->viewport_height, ns_settle_ms(),
                    request->history, request->user_activated, &page) : -1;
            }
            if (rc == 0 && page.ok) {
                result->ok = TRUE;
                result->page_width = page.page_width;
                result->page_height = page.page_height;
                result->title = g_strdup(page.title ? page.title : "");
                result->url = g_strdup(page.url ? page.url : request->url);
                result->nav = g_strdup(page.nav);
                result->security = page.security;
                result->remote_ip = g_strdup(page.remote_ip);
            }
            if (rc == 0)
                ns_rproc_http_page_clear(&page);
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_RENDER) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_FRAME;
            result->seq = request->seq;
            ns_rproc_http_frame frame = {0};
            gboolean rendered = view->proc &&
                ns_rproc_http_render(view->proc, request->width,
                    request->height, request->scroll_x, request->scroll_y,
                    request->scale, request->caret_active, &frame) == 0 &&
                frame.ok;
            if (rendered) {
                result->ok = TRUE;
                result->animating = frame.animating != 0;
                result->caret_blinking = frame.caret_blinking != 0;
                result->page_width = frame.page_w;
                result->page_height = frame.page_h;
                result->requested_scroll_y = frame.scroll_y;
                result->frame_unchanged = frame.unchanged != 0;
                if (!frame.unchanged && frame.pixels && frame.width > 0 &&
                    frame.width <= NS_PROC_MAX_WIDTH && frame.height > 0 &&
                    frame.height <= NS_PROC_MAX_HEIGHT &&
                    frame.stride >= frame.width * 4 &&
                    frame.stride <= NS_PROC_MAX_WIDTH * 4 &&
                    (gsize)frame.stride <=
                        G_MAXSIZE / (gsize)frame.height) {
                    gsize bytes = (gsize)frame.stride * (gsize)frame.height;
                    result->pixels = g_memdup2(frame.pixels, bytes);
                    result->width = frame.width;
                    result->height = frame.height;
                    result->stride = frame.stride;
                }
                result->nav = ns_take_cstr(frame.nav);
                result->camera = ns_take_cstr(frame.camera);
                result->download = ns_take_cstr(frame.download);
                result->audio = ns_take_cstr(frame.audio);
                frame.nav = NULL;
                frame.camera = NULL;
                frame.download = NULL;
                frame.audio = NULL;
            } else if (view->proc) {
                ns_rproc_http_close(ns_swap_proc(view, NULL));
            }
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_CONTEXT) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_CONTEXT;
            result->seq = request->seq;
            result->kind = request->kind;
            if (view->proc) {
                if (request->kind == 0)
                    ns_rproc_http_contextmenu(view->proc, request->x,
                                              request->y,
                                              &result->prevented);
                if (!result->prevented)
                    result->text = ns_take_cstr(ns_rproc_http_link_at(
                        view->proc, request->x, request->y));
            }
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_CLICK) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_CLICK;
            result->seq = request->seq;
            result->text = view->proc ? ns_take_cstr(ns_rproc_http_click(
                view->proc, request->x, request->y, request->mods)) : NULL;
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_VIEWPORT) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_VIEWPORT;
            result->seq = request->seq;
            ns_rproc_http_page page = {0};
            if (view->proc && ns_rproc_http_set_viewport(
                    view->proc, request->viewport_width,
                    request->viewport_height, &page) == 0) {
                result->ok = page.ok != 0;
                result->page_width = page.page_width;
                result->page_height = page.page_height;
                ns_rproc_http_page_clear(&page);
            }
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_KEY) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_KEY;
            result->seq = request->seq;
            result->kind = request->kind;
            result->fallback_x = request->dx;
            result->fallback_y = request->dy;
            result->text = view->proc ? ns_take_cstr(ns_rproc_http_key_full(
                view->proc, request->kind, request->key, request->code,
                request->keycode, request->mods, &result->prevented)) : NULL;
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_SELECT) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = request->kind == 4 ? NS_RES_COPY : NS_RES_SELECT;
            result->seq = request->seq;
            result->text = view->proc ? ns_take_cstr(ns_rproc_http_select(
                view->proc, request->kind, request->x, request->y)) : NULL;
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_HOVER) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_HOVER;
            result->seq = request->seq;
            if (view->proc) {
                char *href = NULL;
                char *cursor = NULL;
                result->ok = ns_rproc_http_hover_full(
                    view->proc, request->x, request->y, &href,
                    &cursor) == 1;
                result->text = ns_take_cstr(href);
                result->cursor = ns_take_cstr(cursor);
            }
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_RELEASE) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_RELEASE;
            result->text = view->proc ? ns_take_cstr(
                ns_rproc_http_release_full(view->proc, &result->ok)) : NULL;
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_FIND) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_FIND;
            result->seq = request->seq;
            if (view->proc)
                ns_rproc_http_find(view->proc, request->query, 0,
                    request->find_direction, request->find_from_y,
                    &result->find_total, &result->find_current,
                    &result->find_scroll_y);
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_EXPORT) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_EXPORT;
            if (view->proc && request->url && request->export_destination &&
                ns_rproc_http_export(view->proc, request->url) == 0) {
                GFile *source = g_file_new_for_path(request->url);
                GFile *destination =
                    g_file_new_for_path(request->export_destination);
                result->ok = g_file_copy(source, destination,
                    G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
                g_object_unref(source);
                g_object_unref(destination);
            }
            if (request->url)
                g_unlink(request->url);
            result->url = g_strdup(request->export_destination);
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_CONSOLE) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_CONSOLE;
            result->text = view->proc ? ns_take_cstr(
                ns_rproc_http_console_poll(view->proc)) : NULL;
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_EVAL) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_EVAL;
            result->dump_tab = request->dump_tab;
            result->inspect = request->inspect;
            result->text = view->proc ? ns_take_cstr(
                ns_rproc_http_eval(view->proc, request->query)) : NULL;
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_DUMP) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_DUMP;
            result->dump_tab = request->dump_tab;
            result->text = view->proc ? ns_take_cstr(
                ns_rproc_http_dump(view->proc, request->query)) : NULL;
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_DROP_FILES) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_DROP_FILES;
            if (view->proc && request->paths && *request->paths) {
                char **paths = g_strsplit(request->paths, "\n", -1);
                int count = (int)g_strv_length(paths);
                result->ok = count > 0 && ns_rproc_http_drop_files(
                    view->proc, request->x, request->y,
                    (const char *const *)paths, count) == 1;
                g_strfreev(paths);
            }
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_SCROLL) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_SCROLL;
            result->fallback_x = request->dx;
            result->fallback_y = request->dy;
            result->ok = view->proc && ns_rproc_http_scroll(
                view->proc, request->x, request->y, request->scroll_x,
                request->scroll_y);
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_SCROLLBAR) {
            NsResult *result = g_new0(NsResult, 1);
            result->type = NS_RES_SCROLLBAR;
            result->kind = request->kind;
            result->ok = view->proc && ns_rproc_http_scrollbar(
                view->proc, request->kind, request->x, request->y);
            ns_post_result(view, result);
        } else if (request->type == NS_REQ_CAMERA) {
            if (view->proc)
                ns_rproc_http_resolve_camera(view->proc, request->url,
                                             request->mods);
        }
        ns_request_free(request);
    }
    if (view->proc)
        ns_rproc_http_close(ns_swap_proc(view, NULL));
    g_free(renderer);
    return NULL;
}

static double
ns_view_scale(const NsWinView *view)
{
    return view->scale > 0.0 ? view->scale : 1.0;
}

static int
ns_view_px(const NsWinView *view, int value)
{
    UINT dpi = GetDpiForWindow(view->hwnd);
    return MulDiv(value, dpi ? (int)dpi : 96, 96);
}

static void
ns_apply_dpi_change(HWND window, LPARAM lparam)
{
    RECT *suggested = (RECT *)lparam;
    SetWindowPos(window, NULL, suggested->left, suggested->top,
                 suggested->right - suggested->left,
                 suggested->bottom - suggested->top,
                 SWP_NOACTIVATE | SWP_NOZORDER);
}

static int
ns_view_content_top(const NsWinView *view)
{
    return (view->permission_visible ? ns_view_px(view, 38) : 0) +
           (view->find_visible ? ns_view_px(view, 36) : 0);
}

static void
ns_viewport_size(NsWinView *view, int *width, int *height)
{
    RECT rect = {0};
    GetClientRect(view->hwnd, &rect);
    int top = ns_view_content_top(view);
    *width = rect.right > 0 ? rect.right : 1;
    *height = rect.bottom - top > 0 ? rect.bottom - top : 1;
}

static void
ns_layout_bars(NsWinView *view)
{
    RECT rect = {0};
    GetClientRect(view->hwnd, &rect);
    int y = 0;
    if (view->permission_visible) {
        int button_width = ns_view_px(view, 92);
        int padding = ns_view_px(view, 10);
        int inset = ns_view_px(view, 5);
        int height = ns_view_px(view, 28);
        MoveWindow(view->perm_label, padding, y + inset,
                   MAX(1, rect.right - button_width * 2 -
                       ns_view_px(view, 34)), height, TRUE);
        MoveWindow(view->perm_allow,
                   MAX(padding, rect.right - button_width * 2 -
                       ns_view_px(view, 18)), y + inset,
                   button_width, height, TRUE);
        MoveWindow(view->perm_deny,
                   MAX(padding, rect.right - button_width - padding),
                   y + inset, button_width, height, TRUE);
        y += ns_view_px(view, 38);
    }
    if (view->find_visible) {
        int margin = ns_view_px(view, 8);
        int bar_width = MIN(ns_view_px(view, 470),
                            MAX(ns_view_px(view, 280),
                                rect.right - margin * 2));
        int left = MAX(margin, rect.right - bar_width - margin);
        int top = y + ns_view_px(view, 4);
        int height = ns_view_px(view, 27);
        MoveWindow(view->find_edit, left, top,
                   bar_width - ns_view_px(view, 190), height, TRUE);
        MoveWindow(view->find_label,
                   left + bar_width - ns_view_px(view, 184), top,
                   ns_view_px(view, 54), height, TRUE);
        MoveWindow(view->find_prev,
                   left + bar_width - ns_view_px(view, 126), top,
                   ns_view_px(view, 38), height, TRUE);
        MoveWindow(view->find_next,
                   left + bar_width - ns_view_px(view, 86), top,
                   ns_view_px(view, 38), height, TRUE);
        MoveWindow(view->find_close,
                   left + bar_width - ns_view_px(view, 46), top,
                   ns_view_px(view, 38), height, TRUE);
    }
}

static void
ns_show_find_controls(NsWinView *view, gboolean show)
{
    int command = show ? SW_SHOW : SW_HIDE;
    ShowWindow(view->find_edit, command);
    ShowWindow(view->find_label, command);
    ShowWindow(view->find_prev, command);
    ShowWindow(view->find_next, command);
    ShowWindow(view->find_close, command);
}

static void
ns_show_permission_controls(NsWinView *view, gboolean show)
{
    int command = show ? SW_SHOW : SW_HIDE;
    ShowWindow(view->perm_label, command);
    ShowWindow(view->perm_allow, command);
    ShowWindow(view->perm_deny, command);
}

static void
ns_configure_scrollbars(NsWinView *view)
{
    int viewport_width = 1;
    int viewport_height = 1;
    ns_viewport_size(view, &viewport_width, &viewport_height);
    double scale = ns_view_scale(view);
    int visible_width = MAX(1, (int)(viewport_width / scale));
    int visible_height = MAX(1, (int)(viewport_height / scale));
    int max_x = MAX(0, view->page_width - visible_width);
    int max_y = MAX(0, view->page_height - visible_height);
    view->scroll_x = CLAMP(view->scroll_x, 0, max_x);
    view->scroll_y = CLAMP(view->scroll_y, 0, max_y);
    SCROLLINFO horizontal = {
        .cbSize = sizeof horizontal,
        .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
        .nMin = 0,
        .nMax = MAX(view->page_width - 1, 0),
        .nPage = (UINT)visible_width,
        .nPos = view->scroll_x,
    };
    SCROLLINFO vertical = {
        .cbSize = sizeof vertical,
        .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
        .nMin = 0,
        .nMax = MAX(view->page_height - 1, 0),
        .nPage = (UINT)visible_height,
        .nPos = view->scroll_y,
    };
    SetScrollInfo(view->hwnd, SB_HORZ, &horizontal, TRUE);
    SetScrollInfo(view->hwnd, SB_VERT, &vertical, TRUE);
}

static void ns_request_render(NsWinView *view);
static void ns_do_load(NsWinView *view, const char *url, gboolean record,
                       gboolean history, gboolean user_activated);

static void
ns_scroll_to(NsWinView *view, int x, int y)
{
    int old_x = view->scroll_x;
    int old_y = view->scroll_y;
    view->scroll_x = x;
    view->scroll_y = y;
    ns_configure_scrollbars(view);
    if (view->opened &&
        (old_x != view->scroll_x || old_y != view->scroll_y))
        ns_request_render(view);
}

static void
ns_start_render(NsWinView *view)
{
    if (!view->opened || ns_view_closed(view))
        return;
    int width = 1;
    int height = 1;
    ns_viewport_size(view, &width, &height);
    view->render_inflight = TRUE;
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_RENDER;
    request->seq = ++view->render_seq;
    request->width = width;
    request->height = height;
    request->scroll_x = view->scroll_x;
    request->scroll_y = view->scroll_y;
    request->scale = ns_view_scale(view);
    request->caret_active = GetFocus() == view->hwnd;
    ns_push_request(view, request);
}

static void
ns_request_render(NsWinView *view)
{
    if (!view->opened || ns_view_closed(view))
        return;
    if (view->render_inflight) {
        view->render_pending = TRUE;
        return;
    }
    ns_start_render(view);
}

static void
ns_set_animation_timer(NsWinView *view)
{
    KillTimer(view->hwnd, NS_TIMER_ANIM);
    if (view->page_animating)
        SetTimer(view->hwnd, NS_TIMER_ANIM, 16, NULL);
    else if (view->caret_blinking)
        SetTimer(view->hwnd, NS_TIMER_ANIM, 530, NULL);
}

static void
ns_start_viewport(NsWinView *view, int width, int height)
{
    if (!view->opened || width <= 1 || height <= 1)
        return;
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_VIEWPORT;
    request->seq = ++view->viewport_seq;
    request->viewport_width = width;
    request->viewport_height = height;
    ns_push_request(view, request);
}

static void
ns_maybe_update_viewport(NsWinView *view)
{
    int width = 1;
    int height = 1;
    ns_viewport_size(view, &width, &height);
    if (!view->opened || width <= 1 || height <= 1)
        return;
    if (width == view->last_viewport_width &&
        height == view->last_viewport_height) {
        ns_configure_scrollbars(view);
        ns_request_render(view);
        return;
    }
    view->last_viewport_width = width;
    view->last_viewport_height = height;
    ns_start_viewport(view, width, height);
}

static void
ns_history_push(NsWinView *view, const char *url)
{
    if (!url || !*url)
        return;
    if (view->history_index >= 0 &&
        g_strcmp0(g_ptr_array_index(view->history, view->history_index),
                  url) == 0)
        return;
    while ((int)view->history->len > view->history_index + 1)
        g_ptr_array_remove_index(view->history, view->history->len - 1);
    g_ptr_array_add(view->history, g_strdup(url));
    view->history_index = (int)view->history->len - 1;
    ns_notify(view, NS_WINVIEW_HISTORY, NULL);
}

static void
ns_permission_resolve(NsWinView *view, gboolean allow)
{
    if (!view->permission_pending)
        return;
    view->permission_pending = FALSE;
    view->permission_visible = FALSE;
    ns_show_permission_controls(view, FALSE);
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_CAMERA;
    request->url = g_strdup(view->permission_origin);
    request->mods = allow ? 1 : 0;
    ns_push_request(view, request);
    g_clear_pointer(&view->permission_origin, g_free);
    ns_layout_bars(view);
    ns_maybe_update_viewport(view);
    InvalidateRect(view->hwnd, NULL, TRUE);
}

static void
ns_permission_show(NsWinView *view, const char *origin)
{
    if (!origin || !*origin)
        return;
    if (view->permission_pending &&
        g_strcmp0(view->permission_origin, origin) == 0)
        return;
    if (view->permission_pending)
        ns_permission_resolve(view, FALSE);
    view->permission_pending = TRUE;
    view->permission_visible = TRUE;
    g_free(view->permission_origin);
    view->permission_origin = g_strdup(origin);
    char *label = g_strdup_printf("%s — %s",
        ns_i18n("This site wants to use your camera and microphone"),
        origin);
    ns_set_window_text_utf8(view->perm_label, label);
    g_free(label);
    ns_show_permission_controls(view, TRUE);
    ns_layout_bars(view);
    ns_maybe_update_viewport(view);
}

static void
ns_find_request(NsWinView *view, int direction)
{
    if (!view->opened)
        return;
    char *query = ns_window_text_utf8(view->find_edit);
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_FIND;
    request->seq = ++view->find_seq;
    request->query = query;
    request->find_direction = direction;
    request->find_from_y = view->scroll_y;
    ns_push_request(view, request);
}

static void
ns_find_close(NsWinView *view)
{
    if (!view->find_visible)
        return;
    view->find_visible = FALSE;
    ns_show_find_controls(view, FALSE);
    ns_set_window_text_utf8(view->find_label, "");
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_FIND;
    request->seq = ++view->find_seq;
    request->query = g_strdup("");
    request->find_from_y = view->scroll_y;
    ns_push_request(view, request);
    ns_layout_bars(view);
    ns_maybe_update_viewport(view);
    SetFocus(view->hwnd);
}

static void
ns_start_hover(NsWinView *view, int x, int y)
{
    if (!view->opened)
        return;
    view->hover_inflight = TRUE;
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_HOVER;
    request->seq = ++view->hover_seq;
    request->x = x;
    request->y = y;
    ns_push_request(view, request);
}

static void
ns_request_hover(NsWinView *view, int x, int y)
{
    if (view->hover_inflight) {
        view->hover_pending = TRUE;
        view->hover_x = x;
        view->hover_y = y;
        return;
    }
    ns_start_hover(view, x, y);
}

static void
ns_start_select(NsWinView *view, int kind, int x, int y)
{
    if (!view->opened)
        return;
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_SELECT;
    request->seq = ++view->select_seq;
    request->kind = kind;
    request->x = x;
    request->y = y;
    ns_push_request(view, request);
}

static void
ns_start_scrollbar(NsWinView *view, int kind, int x, int y)
{
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_SCROLLBAR;
    request->kind = kind;
    request->x = x;
    request->y = y;
    ns_push_request(view, request);
}

static int
ns_modifiers(void)
{
    return ((GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0) |
           ((GetKeyState(VK_CONTROL) & 0x8000) ? 2 : 0) |
           ((GetKeyState(VK_MENU) & 0x8000) ? 4 : 0) |
           (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) ? 8 : 0);
}

static const char *
ns_virtual_key_name(UINT key, char *buffer, gsize size)
{
    switch (key) {
    case VK_UP: return "ArrowUp";
    case VK_DOWN: return "ArrowDown";
    case VK_LEFT: return "ArrowLeft";
    case VK_RIGHT: return "ArrowRight";
    case VK_RETURN: return "Enter";
    case VK_ESCAPE: return "Escape";
    case VK_BACK: return "Backspace";
    case VK_TAB: return "Tab";
    case VK_DELETE: return "Delete";
    case VK_INSERT: return "Insert";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_SPACE: return " ";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Control";
    case VK_MENU: return "Alt";
    default: break;
    }
    BYTE state[256];
    WCHAR wide[4] = {0};
    if (GetKeyboardState(state)) {
        int count = ToUnicodeEx(key, MapVirtualKeyW(key, MAPVK_VK_TO_VSC),
                                state, wide, 4, NS_TOUNICODE_KEEP_STATE,
                                GetKeyboardLayout(0));
        if (count == 1) {
            char *utf8 = g_utf16_to_utf8((gunichar2 *)wide, 1, NULL, NULL,
                                         NULL);
            if (utf8) {
                g_strlcpy(buffer, utf8, size);
                g_free(utf8);
                return buffer;
            }
        }
    }
    buffer[0] = '\0';
    return buffer;
}

static const char *
ns_virtual_code_name(UINT key, char *buffer, gsize size)
{
    if (key >= 'A' && key <= 'Z') {
        g_snprintf(buffer, size, "Key%c", (int)key);
        return buffer;
    }
    if (key >= '0' && key <= '9') {
        g_snprintf(buffer, size, "Digit%c", (int)key);
        return buffer;
    }
    switch (key) {
    case VK_UP: return "ArrowUp";
    case VK_DOWN: return "ArrowDown";
    case VK_LEFT: return "ArrowLeft";
    case VK_RIGHT: return "ArrowRight";
    case VK_RETURN: return "Enter";
    case VK_ESCAPE: return "Escape";
    case VK_BACK: return "Backspace";
    case VK_TAB: return "Tab";
    case VK_DELETE: return "Delete";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_SPACE: return "Space";
    default: return "";
    }
}

static void
ns_start_key(NsWinView *view, int kind, UINT key, int fallback_x,
             int fallback_y)
{
    if (!view->opened)
        return;
    char key_buffer[16] = {0};
    char code_buffer[16] = {0};
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_KEY;
    request->seq = kind == 0 ? ++view->key_seq : view->key_seq;
    request->kind = kind;
    request->keycode = (int)key;
    request->mods = ns_modifiers();
    request->key = g_strdup(ns_virtual_key_name(
        key, key_buffer, sizeof key_buffer));
    request->code = g_strdup(ns_virtual_code_name(
        key, code_buffer, sizeof code_buffer));
    request->dx = fallback_x;
    request->dy = fallback_y;
    ns_push_request(view, request);
}

static void
ns_start_text(NsWinView *view, const char *text)
{
    if (!view->opened || !text || !*text)
        return;
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_KEY;
    request->seq = ++view->key_seq;
    request->kind = 2;
    request->key = g_strdup(text);
    request->code = g_strdup("");
    ns_push_request(view, request);
}

static void
ns_clipboard_set_utf8(HWND owner, const char *text)
{
    if (!text || !*text)
        return;
    wchar_t *wide = ns_utf8_to_wide(text);
    SIZE_T bytes = (wcslen(wide) + 1) * sizeof *wide;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        g_free(wide);
        return;
    }
    void *destination = GlobalLock(memory);
    if (!destination) {
        g_free(wide);
        GlobalFree(memory);
        return;
    }
    memcpy(destination, wide, bytes);
    GlobalUnlock(memory);
    g_free(wide);
    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, memory))
        GlobalFree(memory);
    CloseClipboard();
}

static char *
ns_clipboard_get_utf8(HWND owner)
{
    if (!OpenClipboard(owner))
        return NULL;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    wchar_t *wide = data ? GlobalLock(data) : NULL;
    char *text = wide ? ns_wide_to_utf8(wide) : NULL;
    if (wide)
        GlobalUnlock(data);
    CloseClipboard();
    return text;
}

static char *
ns_export_default_name(NsWinView *view, gboolean pdf)
{
    const char *title = view->current_title && *view->current_title
                      ? view->current_title : "page";
    char *name = g_strdup_printf("%s.%s", title, pdf ? "pdf" : "png");
    for (char *p = name; *p; p++)
        if (strchr("\\/:*?\"<>|", *p))
            *p = '_';
    return name;
}

static void
ns_export_page(NsWinView *view, gboolean pdf)
{
    if (!view->opened)
        return;
    char *default_name = ns_export_default_name(view, pdf);
    wchar_t *wide_name = ns_utf8_to_wide(default_name);
    wchar_t path[32768] = {0};
    wcsncpy(path, wide_name, G_N_ELEMENTS(path) - 1);
    g_free(wide_name);
    g_free(default_name);
    const wchar_t *filter = pdf
        ? L"PDF document (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0\0"
        : L"PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog = {
        .lStructSize = sizeof dialog,
        .hwndOwner = view->hwnd,
        .lpstrFilter = filter,
        .lpstrFile = path,
        .nMaxFile = G_N_ELEMENTS(path),
        .lpstrDefExt = pdf ? L"pdf" : L"png",
        .Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                 OFN_NOCHANGEDIR,
    };
    if (!GetSaveFileNameW(&dialog))
        return;
    char *destination = ns_wide_to_utf8(path);
    if (!destination)
        return;
    char *directory = g_path_get_dirname(destination);
    ns_security_add_writable_dir(directory);
    g_free(directory);
    static int export_counter;
    char *temporary_name = g_strdup_printf(
        "northstar-export-%" G_GINT64_FORMAT "-%d.%s",
        g_get_monotonic_time(), ++export_counter, pdf ? "pdf" : "png");
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_EXPORT;
    request->url = g_build_filename(g_get_user_runtime_dir(),
                                    temporary_name, NULL);
    request->export_destination = destination;
    ns_push_request(view, request);
    g_free(temporary_name);
}

static void
ns_context_menu_append(HMENU menu, UINT flags, UINT_PTR id,
                       const char *label)
{
    wchar_t *wide = ns_utf8_to_wide(ns_i18n(label));
    AppendMenuW(menu, flags, id, wide);
    g_free(wide);
}

static void
ns_show_context_menu(NsWinView *view, const char *link)
{
    g_free(view->context_link);
    view->context_link = link && *link ? g_strdup(link) : NULL;
    HMENU menu = CreatePopupMenu();
    if (view->context_link) {
        ns_context_menu_append(menu, MF_STRING, NS_CTX_OPEN_LINK,
                               "Open Link");
        ns_context_menu_append(menu, MF_STRING, NS_CTX_COPY_LINK,
                               "Copy Link Address");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    }
    if (view->has_selection) {
        ns_context_menu_append(menu, MF_STRING, NS_CTX_COPY_SELECTION,
                               "Copy");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    }
    ns_context_menu_append(menu,
        MF_STRING | (ns_winview_can_back(view) ? 0 : MF_GRAYED),
        NS_CTX_BACK, "Back");
    ns_context_menu_append(menu,
        MF_STRING | (ns_winview_can_forward(view) ? 0 : MF_GRAYED),
        NS_CTX_FORWARD, "Forward");
    ns_context_menu_append(menu, MF_STRING, NS_CTX_RELOAD, "Reload");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    ns_context_menu_append(menu, MF_STRING, NS_CTX_SELECT_ALL,
                           "Select All");
    ns_context_menu_append(menu, MF_STRING, NS_CTX_COPY_URL,
                           "Copy Page Address");
    ns_context_menu_append(menu, MF_STRING, NS_CTX_SAVE_PDF,
                           "Save Page as PDF…");
    ns_context_menu_append(menu, MF_STRING, NS_CTX_SAVE_PNG,
                           "Save Page as Image…");
    UINT command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        view->context_point.x, view->context_point.y, 0, view->hwnd, NULL);
    DestroyMenu(menu);
    switch (command) {
    case NS_CTX_BACK:
        ns_winview_back(view);
        break;
    case NS_CTX_FORWARD:
        ns_winview_forward(view);
        break;
    case NS_CTX_RELOAD:
        ns_winview_reload(view);
        break;
    case NS_CTX_COPY_URL:
        ns_clipboard_set_utf8(view->hwnd, view->current_url);
        ns_notify(view, NS_WINVIEW_STATUS,
                  ns_i18n("Copied page address"));
        break;
    case NS_CTX_OPEN_LINK:
        ns_winview_load(view, view->context_link);
        break;
    case NS_CTX_COPY_LINK:
        ns_clipboard_set_utf8(view->hwnd, view->context_link);
        ns_notify(view, NS_WINVIEW_STATUS,
                  ns_i18n("Copied link address"));
        break;
    case NS_CTX_COPY_SELECTION:
        ns_start_select(view, 4, 0, 0);
        break;
    case NS_CTX_SELECT_ALL:
        view->has_selection = TRUE;
        ns_start_select(view, 3, 0, 0);
        break;
    case NS_CTX_SAVE_PDF:
        ns_export_page(view, TRUE);
        break;
    case NS_CTX_SAVE_PNG:
        ns_export_page(view, FALSE);
        break;
    default:
        break;
    }
}

static HCURSOR
ns_cursor_for_name(const char *name)
{
    if (!name || !*name)
        return LoadCursorW(NULL, IDC_ARROW);
    if (strcmp(name, "pointer") == 0)
        return LoadCursorW(NULL, IDC_HAND);
    if (strcmp(name, "text") == 0 || strcmp(name, "vertical-text") == 0)
        return LoadCursorW(NULL, IDC_IBEAM);
    if (strcmp(name, "wait") == 0 || strcmp(name, "progress") == 0)
        return LoadCursorW(NULL, IDC_WAIT);
    if (strcmp(name, "crosshair") == 0 ||
        strcmp(name, "cell") == 0)
        return LoadCursorW(NULL, IDC_CROSS);
    if (strstr(name, "ew-resize") || strstr(name, "col-resize"))
        return LoadCursorW(NULL, IDC_SIZEWE);
    if (strstr(name, "ns-resize") || strstr(name, "row-resize"))
        return LoadCursorW(NULL, IDC_SIZENS);
    if (strstr(name, "nwse-resize"))
        return LoadCursorW(NULL, IDC_SIZENWSE);
    if (strstr(name, "nesw-resize"))
        return LoadCursorW(NULL, IDC_SIZENESW);
    if (strcmp(name, "move") == 0 || strcmp(name, "all-scroll") == 0 ||
        strcmp(name, "grab") == 0 || strcmp(name, "grabbing") == 0)
        return LoadCursorW(NULL, IDC_SIZEALL);
    if (strcmp(name, "not-allowed") == 0 || strcmp(name, "no-drop") == 0)
        return LoadCursorW(NULL, IDC_NO);
    if (strcmp(name, "none") == 0)
        return NULL;
    return LoadCursorW(NULL, IDC_ARROW);
}

static int
ns_dev_current_tab(NsWinView *view)
{
    if (!view->dev_tab)
        return NS_DEV_CONSOLE;
    int selected = TabCtrl_GetCurSel(view->dev_tab);
    return selected >= 0 && selected < NS_DEV_COUNT
         ? selected : NS_DEV_CONSOLE;
}

static void
ns_dev_show_text(NsWinView *view, int tab)
{
    if (!view->dev_output || tab < 0 || tab >= NS_DEV_COUNT)
        return;
    ns_set_window_text_utf8(view->dev_output,
                            view->dev_text[tab] ? view->dev_text[tab] : "");
    SendMessageW(view->dev_output, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(view->dev_output, EM_SCROLLCARET, 0, 0);
    ShowWindow(view->dev_input, tab == NS_DEV_CONSOLE ? SW_SHOW : SW_HIDE);
    ShowWindow(view->dev_inspect, tab == NS_DEV_ELEMENTS ? SW_SHOW : SW_HIDE);
}

static void
ns_dev_append_console(NsWinView *view, const char *text)
{
    if (!text || !*text)
        return;
    char *combined = g_strconcat(
        view->dev_text[NS_DEV_CONSOLE]
            ? view->dev_text[NS_DEV_CONSOLE] : "", text, NULL);
    g_free(view->dev_text[NS_DEV_CONSOLE]);
    view->dev_text[NS_DEV_CONSOLE] = combined;
    if (view->devtools_open &&
        ns_dev_current_tab(view) == NS_DEV_CONSOLE)
        ns_dev_show_text(view, NS_DEV_CONSOLE);
}

static void
ns_dev_request_dump(NsWinView *view, int tab)
{
    static const char *const kinds[NS_DEV_COUNT] = {
        NULL, "network", "performance", "layout", "dom"
    };
    if (!view->opened || tab <= NS_DEV_CONSOLE || tab >= NS_DEV_COUNT)
        return;
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_DUMP;
    request->dump_tab = tab;
    request->query = g_strdup(kinds[tab]);
    ns_push_request(view, request);
}

static void
ns_dev_evaluate(NsWinView *view)
{
    char *source = ns_window_text_utf8(view->dev_input);
    if (!source || !*source || !view->opened) {
        g_free(source);
        return;
    }
    char *echo = g_strdup_printf("> %s\r\n", source);
    ns_dev_append_console(view, echo);
    g_free(echo);
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_EVAL;
    request->query = source;
    ns_push_request(view, request);
    SetWindowTextW(view->dev_input, L"");
}

static void
ns_dev_inspect(NsWinView *view)
{
    char *selector = ns_window_text_utf8(view->dev_inspect);
    if (!selector || !*selector || !view->opened) {
        g_free(selector);
        return;
    }
    char *escaped = g_strescape(selector, NULL);
    g_free(selector);
    char *source = g_strdup_printf(
        "(function(){try{var e=document.querySelector(\"%s\");"
        "if(!e)return \"\";var s=e.outerHTML;"
        "return s.length>20000?s.slice(0,20000)+"
        "\"\\n\\u2026(truncated)\":s;}"
        "catch(err){return \"Error: \"+err;}})()", escaped);
    g_free(escaped);
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_EVAL;
    request->query = source;
    request->inspect = TRUE;
    request->dump_tab = NS_DEV_ELEMENTS;
    ns_push_request(view, request);
}

static void
ns_dev_create_controls(NsWinView *view, HWND window)
{
    view->dev_tab = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)NS_DEV_TAB,
        view->instance, NULL);
    static const char *const names[NS_DEV_COUNT] = {
        "Console", "Network", "Performance", "Layout", "Elements"
    };
    for (int i = 0; i < NS_DEV_COUNT; i++) {
        wchar_t *name = ns_utf8_to_wide(ns_i18n(names[i]));
        TCITEMW item = {.mask = TCIF_TEXT, .pszText = name};
        TabCtrl_InsertItem(view->dev_tab, i, &item);
        g_free(name);
    }
    view->dev_output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)NS_DEV_OUTPUT,
        view->instance, NULL);
    view->dev_input = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)NS_DEV_INPUT,
        view->instance, NULL);
    view->dev_inspect = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_AUTOHSCROLL,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)NS_DEV_INSPECT,
        view->instance, NULL);
    wchar_t *refresh = ns_utf8_to_wide(ns_i18n("Refresh"));
    view->dev_refresh = CreateWindowExW(0, L"BUTTON", refresh,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)NS_DEV_REFRESH,
        view->instance, NULL);
    g_free(refresh);
    wchar_t *clear = ns_utf8_to_wide(ns_i18n("Clear"));
    view->dev_clear = CreateWindowExW(0, L"BUTTON", clear,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)NS_DEV_CLEAR,
        view->instance, NULL);
    g_free(clear);
    HWND controls[] = {
        view->dev_tab, view->dev_output, view->dev_input,
        view->dev_inspect, view->dev_refresh, view->dev_clear
    };
    for (gsize i = 0; i < G_N_ELEMENTS(controls); i++)
        ns_set_control_font(view, controls[i]);
    SetWindowSubclass(view->dev_input, ns_edit_subclass_proc, 1,
                      (DWORD_PTR)view);
    SetWindowSubclass(view->dev_inspect, ns_edit_subclass_proc, 2,
                      (DWORD_PTR)view);
    SetTimer(window, NS_TIMER_CONSOLE, NS_PROC_CONSOLE_POLL_MS, NULL);
}

static void
ns_dev_layout(NsWinView *view)
{
    if (!view->devtools)
        return;
    RECT rect = {0};
    GetClientRect(view->devtools, &rect);
    int width = rect.right;
    int height = rect.bottom;
    int margin = ns_view_px(view, 5);
    MoveWindow(view->dev_refresh,
               MAX(ns_view_px(view, 4), width - ns_view_px(view, 172)),
               margin, ns_view_px(view, 80), ns_view_px(view, 27), TRUE);
    MoveWindow(view->dev_clear,
               MAX(ns_view_px(view, 4), width - ns_view_px(view, 87)),
               margin, ns_view_px(view, 80), ns_view_px(view, 27), TRUE);
    MoveWindow(view->dev_tab, margin, ns_view_px(view, 37),
               MAX(1, width - margin * 2),
               MAX(1, height - ns_view_px(view, 42)), TRUE);
    RECT content = {
        0, 0, MAX(1, width - margin * 2),
        MAX(1, height - ns_view_px(view, 42))
    };
    TabCtrl_AdjustRect(view->dev_tab, FALSE, &content);
    int x = margin + content.left;
    int y = ns_view_px(view, 37) + content.top;
    int content_width = MAX(1, content.right - content.left);
    int content_height = MAX(1, content.bottom - content.top);
    int input_height = ns_view_px(view, 28);
    int tab = ns_dev_current_tab(view);
    gboolean input = tab == NS_DEV_CONSOLE || tab == NS_DEV_ELEMENTS;
    MoveWindow(view->dev_output, x, y, content_width,
               MAX(1, content_height - (input ? input_height : 0)), TRUE);
    MoveWindow(view->dev_input, x, y + content_height - input_height,
               content_width, input_height, TRUE);
    MoveWindow(view->dev_inspect, x, y + content_height - input_height,
               content_width, input_height, TRUE);
}

static void
ns_finish_loading(NsWinView *view)
{
    if (!view->loading)
        return;
    view->loading = FALSE;
    ns_notify(view, NS_WINVIEW_LOADING, "0");
}

static void
ns_handle_result(NsWinView *view, NsResult *result)
{
    if (ns_view_closed(view)) {
        ns_result_free(result);
        return;
    }
    if (result->type == NS_RES_PAGE) {
        if (result->seq != view->load_seq)
            goto done;
        if (!result->ok) {
            ns_notify(view, NS_WINVIEW_STATUS,
                      ns_i18n("Failed to load page"));
            ns_finish_loading(view);
            goto done;
        }
        if (result->nav && *result->nav) {
            if (view->javascript_redirects < NS_PROC_MAX_JS_REDIRECTS) {
                view->javascript_redirects++;
                gboolean record = view->pending_record;
                char *redirect = g_strdup(result->nav);
                ns_result_free(result);
                ns_do_load(view, redirect, record, FALSE, FALSE);
                g_free(redirect);
                return;
            }
            ns_notify(view, NS_WINVIEW_STATUS,
                      ns_i18n("Stopped after too many redirects"));
        }
        view->javascript_redirects = 0;
        g_free(view->current_url);
        view->current_url = g_strdup(result->url);
        g_free(view->current_title);
        view->current_title = g_strdup(result->title);
        view->security = result->security;
        g_free(view->remote_ip);
        view->remote_ip = g_strdup(result->remote_ip);
        view->page_width = result->page_width;
        view->page_height = result->page_height;
        view->scroll_x = 0;
        view->scroll_y = 0;
        view->opened = TRUE;
        ns_configure_scrollbars(view);
        if (view->pending_record)
            ns_history_push(view, view->current_url);
        ns_notify(view, NS_WINVIEW_URL, view->current_url);
        ns_notify(view, NS_WINVIEW_TITLE, view->current_title);
        ns_notify(view, NS_WINVIEW_STATUS, ns_i18n("Done"));
        ns_finish_loading(view);
        ns_request_render(view);
    } else if (result->type == NS_RES_FRAME) {
        gboolean current = result->seq == view->render_seq;
        if (current && result->ok) {
            view->page_animating = result->animating;
            view->caret_blinking = result->caret_blinking;
            ns_set_animation_timer(view);
            if (result->page_height > 0 &&
                result->page_height != view->page_height) {
                view->page_height = result->page_height;
                if (result->page_width > 0)
                    view->page_width = result->page_width;
                ns_configure_scrollbars(view);
            }
            if (result->requested_scroll_y >= 0)
                ns_scroll_to(view, view->scroll_x,
                             result->requested_scroll_y);
        }
        if (current && result->ok && result->pixels) {
            g_free(view->frame_pixels);
            view->frame_pixels = result->pixels;
            result->pixels = NULL;
            view->frame_width = result->width;
            view->frame_height = result->height;
            view->frame_stride = result->stride;
            view->render_restarts = 0;
            InvalidateRect(view->hwnd, NULL, FALSE);
        } else if (current && result->ok && result->frame_unchanged) {
            view->render_restarts = 0;
        }
        if (current && result->ok && result->nav && *result->nav &&
            view->javascript_redirects < NS_PROC_MAX_JS_REDIRECTS) {
            view->javascript_redirects++;
            char *redirect = g_strdup(result->nav);
            view->render_inflight = FALSE;
            ns_result_free(result);
            ns_do_load(view, redirect, FALSE, FALSE, FALSE);
            g_free(redirect);
            return;
        }
        if (current && result->ok && result->camera && *result->camera)
            ns_permission_show(view, result->camera);
        if (current && result->ok && result->download && *result->download)
            ns_notify(view, NS_WINVIEW_DOWNLOAD, result->download);
        if (current && result->ok && result->audio && *result->audio)
            ns_audio_pump(view, result->audio);
        view->render_inflight = FALSE;
        if (view->render_pending) {
            view->render_pending = FALSE;
            ns_start_render(view);
        } else if (current && !result->ok && view->current_url) {
            if (view->render_restarts < NS_PROC_MAX_RESTARTS) {
                view->render_restarts++;
                ns_notify(view, NS_WINVIEW_STATUS,
                          ns_i18n("Renderer restarted"));
                char *url = g_strdup(view->current_url);
                ns_do_load(view, url, FALSE, FALSE, FALSE);
                g_free(url);
            } else {
                ns_notify(view, NS_WINVIEW_STATUS,
                    ns_i18n("The page renderer keeps failing — reload to retry"));
                ns_finish_loading(view);
            }
        }
    } else if (result->type == NS_RES_VIEWPORT) {
        if (result->seq == view->viewport_seq && result->ok) {
            view->page_width = result->page_width;
            view->page_height = result->page_height;
            ns_configure_scrollbars(view);
            ns_request_render(view);
        }
    } else if (result->type == NS_RES_SELECT) {
        if (result->seq == view->select_seq)
            ns_request_render(view);
    } else if (result->type == NS_RES_COPY) {
        if (result->text && *result->text) {
            ns_clipboard_set_utf8(view->hwnd, result->text);
            ns_notify(view, NS_WINVIEW_STATUS,
                      ns_i18n("Copied selection"));
        }
    } else if (result->type == NS_RES_KEY) {
        if (result->seq != view->key_seq)
            goto done;
        if (result->kind == 0 && result->text && *result->text) {
            char *url = g_strdup(result->text);
            ns_notify(view, NS_WINVIEW_STATUS, url);
            ns_winview_load(view, url);
            g_free(url);
        } else {
            if (!result->prevented &&
                (result->fallback_x || result->fallback_y))
                ns_scroll_to(view, result->fallback_x, result->fallback_y);
            ns_request_render(view);
        }
    } else if (result->type == NS_RES_CLICK) {
        if (result->seq != view->click_seq)
            goto done;
        if (result->text && *result->text) {
            char *url = g_strdup(result->text);
            ns_notify(view, NS_WINVIEW_STATUS, url);
            ns_winview_load(view, url);
            g_free(url);
        } else {
            ns_request_render(view);
        }
    } else if (result->type == NS_RES_CONTEXT) {
        if (result->seq == view->context_seq) {
            if (result->kind == 1) {
                if (result->text && *result->text)
                    ns_do_load(view, result->text, TRUE, FALSE, TRUE);
            } else if (result->prevented) {
                ns_request_render(view);
            } else {
                ns_show_context_menu(view, result->text);
            }
        }
    } else if (result->type == NS_RES_HOVER) {
        if (result->seq != view->hover_seq)
            goto done;
        view->hover_inflight = FALSE;
        if (result->text && *result->text)
            ns_notify(view, NS_WINVIEW_STATUS, result->text);
        SetCursor(ns_cursor_for_name(result->cursor));
        if (result->ok)
            ns_request_render(view);
        if (view->hover_pending) {
            view->hover_pending = FALSE;
            ns_start_hover(view, view->hover_x, view->hover_y);
        }
    } else if (result->type == NS_RES_RELEASE) {
        if (result->text && *result->text) {
            char *url = g_strdup(result->text);
            ns_notify(view, NS_WINVIEW_STATUS, url);
            ns_winview_load(view, url);
            g_free(url);
        } else if (result->ok) {
            ns_request_render(view);
        }
    } else if (result->type == NS_RES_FIND) {
        if (result->seq != view->find_seq)
            goto done;
        if (result->find_total > 0) {
            char label[64];
            g_snprintf(label, sizeof label, "%d/%d", result->find_current,
                       result->find_total);
            ns_set_window_text_utf8(view->find_label, label);
            ns_scroll_to(view, view->scroll_x,
                         MAX(0, result->find_scroll_y - 40));
        } else {
            char *query = ns_window_text_utf8(view->find_edit);
            ns_set_window_text_utf8(view->find_label,
                query && *query ? ns_i18n("No results") : "");
            g_free(query);
        }
        ns_request_render(view);
    } else if (result->type == NS_RES_EXPORT) {
        ns_notify(view, NS_WINVIEW_STATUS,
            result->ok && result->url
                ? result->url : ns_i18n("Could not save page"));
    } else if (result->type == NS_RES_CONSOLE) {
        if (result->text && *result->text)
            ns_dev_append_console(view, result->text);
    } else if (result->type == NS_RES_EVAL) {
        if (result->inspect) {
            g_free(view->dev_text[NS_DEV_ELEMENTS]);
            view->dev_text[NS_DEV_ELEMENTS] = g_strdup(
                result->text && *result->text
                    ? result->text : ns_i18n("No matching element"));
            if (view->devtools_open &&
                ns_dev_current_tab(view) == NS_DEV_ELEMENTS)
                ns_dev_show_text(view, NS_DEV_ELEMENTS);
        } else {
            ns_dev_append_console(view,
                result->text && *result->text ? result->text : "undefined");
            ns_dev_append_console(view, "\r\n");
        }
        ns_request_render(view);
    } else if (result->type == NS_RES_DUMP) {
        int tab = result->dump_tab;
        if (tab > NS_DEV_CONSOLE && tab < NS_DEV_COUNT) {
            g_free(view->dev_text[tab]);
            view->dev_text[tab] = g_strdup(
                result->text && *result->text
                    ? result->text : ns_i18n("(empty)"));
            if (view->devtools_open && ns_dev_current_tab(view) == tab)
                ns_dev_show_text(view, tab);
        }
    } else if (result->type == NS_RES_DROP_FILES) {
        if (result->ok)
            ns_request_render(view);
    } else if (result->type == NS_RES_SCROLL) {
        if (result->ok)
            ns_request_render(view);
        else
            ns_scroll_to(view, view->scroll_x + result->fallback_x,
                         view->scroll_y + result->fallback_y);
    } else if (result->type == NS_RES_SCROLLBAR) {
        if (result->kind == 0) {
            gboolean probing = view->scrollbar_probe;
            view->scrollbar_probe = FALSE;
            if (result->ok) {
                ns_request_render(view);
                if (probing) {
                    view->scrollbar_dragging = TRUE;
                    if (view->scrollbar_last_valid) {
                        double scale = ns_view_scale(view);
                        ns_start_scrollbar(view, 1,
                            view->scroll_x + (int)(view->drag_last_x / scale),
                            view->scroll_y + (int)(view->drag_last_y / scale));
                    }
                }
            } else if (probing && view->scrollbar_last_valid) {
                double scale = ns_view_scale(view);
                ns_start_select(view, 0,
                    view->scroll_x + (int)(view->drag_start_x / scale),
                    view->scroll_y + (int)(view->drag_start_y / scale));
                view->drag_anchored = TRUE;
                ns_start_select(view, 1,
                    view->scroll_x + (int)(view->drag_last_x / scale),
                    view->scroll_y + (int)(view->drag_last_y / scale));
                view->has_selection = TRUE;
            }
        } else if (result->kind == 1 && result->ok) {
            ns_request_render(view);
        }
    }

done:
    ns_result_free(result);
}

static void
ns_paint_view(NsWinView *view)
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(view->hwnd, &paint);
    RECT rect = {0};
    GetClientRect(view->hwnd, &rect);
    int top = ns_view_content_top(view);
    RECT page_rect = {0, top, rect.right, rect.bottom};
    FillRect(dc, &page_rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
    if (view->frame_pixels && view->frame_width > 0 &&
        view->frame_height > 0) {
        BITMAPINFO info = {0};
        info.bmiHeader.biSize = sizeof info.bmiHeader;
        info.bmiHeader.biWidth = view->frame_width;
        info.bmiHeader.biHeight = -view->frame_height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(dc, 0, top, view->frame_width, view->frame_height,
                      0, 0, view->frame_width, view->frame_height,
                      view->frame_pixels, &info, DIB_RGB_COLORS, SRCCOPY);
    }
    if (view->permission_visible) {
        int bottom = ns_view_px(view, 38);
        RECT line = {0, bottom - 1, rect.right, bottom};
        FillRect(dc, &line, (HBRUSH)(COLOR_3DSHADOW + 1));
    }
    if (view->find_visible) {
        int y = view->permission_visible ? ns_view_px(view, 38) : 0;
        int bottom = y + ns_view_px(view, 36);
        RECT line = {0, bottom - 1, rect.right, bottom};
        FillRect(dc, &line, (HBRUSH)(COLOR_3DSHADOW + 1));
    }
    EndPaint(view->hwnd, &paint);
}

static void
ns_handle_scroll_message(NsWinView *view, int bar, WPARAM wparam)
{
    SCROLLINFO info = {
        .cbSize = sizeof info,
        .fMask = SIF_ALL,
    };
    GetScrollInfo(view->hwnd, bar, &info);
    int position = info.nPos;
    int page = MAX(60, (int)info.nPage - 60);
    switch (LOWORD(wparam)) {
    case SB_LINEUP:
        position -= 60;
        break;
    case SB_LINEDOWN:
        position += 60;
        break;
    case SB_PAGEUP:
        position -= page;
        break;
    case SB_PAGEDOWN:
        position += page;
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        position = info.nTrackPos;
        break;
    case SB_TOP:
        position = info.nMin;
        break;
    case SB_BOTTOM:
        position = info.nMax;
        break;
    default:
        return;
    }
    if (bar == SB_VERT)
        ns_scroll_to(view, view->scroll_x, position);
    else
        ns_scroll_to(view, position, view->scroll_y);
}

static void
ns_handle_wheel(NsWinView *view, WPARAM wparam, LPARAM lparam,
                gboolean horizontal)
{
    int delta = GET_WHEEL_DELTA_WPARAM(wparam);
    if (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) {
        if (delta > 0)
            ns_winview_zoom_in(view);
        else if (delta < 0)
            ns_winview_zoom_out(view);
        return;
    }
    POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    ScreenToClient(view->hwnd, &point);
    int top = ns_view_content_top(view);
    if (point.y < top)
        return;
    double scale = ns_view_scale(view);
    int fallback = -(delta / WHEEL_DELTA) * 180;
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_SCROLL;
    request->x = view->scroll_x + (int)(point.x / scale);
    request->y = view->scroll_y + (int)((point.y - top) / scale);
    request->scroll_x = horizontal ? fallback : 0;
    request->scroll_y = horizontal ? 0 : fallback;
    request->dx = horizontal ? fallback : 0;
    request->dy = horizontal ? 0 : fallback;
    ns_push_request(view, request);
}

static void
ns_handle_mouse_move(NsWinView *view, WPARAM wparam, LPARAM lparam)
{
    int x = GET_X_LPARAM(lparam);
    int y = GET_Y_LPARAM(lparam) - ns_view_content_top(view);
    view->pointer_x = x;
    view->pointer_y = y;
    if (y < 0 || !view->opened)
        return;
    TRACKMOUSEEVENT tracking = {
        .cbSize = sizeof tracking,
        .dwFlags = TME_LEAVE,
        .hwndTrack = view->hwnd,
    };
    TrackMouseEvent(&tracking);
    double scale = ns_view_scale(view);
    int page_x = view->scroll_x + (int)(x / scale);
    int page_y = view->scroll_y + (int)(y / scale);
    ns_request_hover(view, page_x, page_y);
    if (!(wparam & MK_LBUTTON) || !view->dragging)
        return;
    view->drag_last_x = x;
    view->drag_last_y = y;
    view->scrollbar_last_valid = TRUE;
    if (view->scrollbar_dragging) {
        ns_start_scrollbar(view, 1, page_x, page_y);
    } else if (!view->scrollbar_probe) {
        if (!view->drag_anchored) {
            ns_start_select(view, 0,
                view->scroll_x + (int)(view->drag_start_x / scale),
                view->scroll_y + (int)(view->drag_start_y / scale));
            view->drag_anchored = TRUE;
        }
        ns_start_select(view, 1, page_x, page_y);
        view->has_selection = TRUE;
    }
}

static void
ns_start_link_navigation(NsWinView *view, int x, int y)
{
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_CONTEXT;
    request->seq = ++view->context_seq;
    request->kind = 1;
    request->x = x;
    request->y = y;
    ns_push_request(view, request);
}

static void
ns_handle_left_down(NsWinView *view, LPARAM lparam)
{
    int x = GET_X_LPARAM(lparam);
    int y = GET_Y_LPARAM(lparam) - ns_view_content_top(view);
    if (y < 0 || !view->opened)
        return;
    SetFocus(view->hwnd);
    SetCapture(view->hwnd);
    view->dragging = TRUE;
    view->drag_anchored = FALSE;
    view->scrollbar_probe = TRUE;
    view->scrollbar_dragging = FALSE;
    view->scrollbar_last_valid = FALSE;
    view->drag_start_x = x;
    view->drag_start_y = y;
    view->has_selection = FALSE;
    double scale = ns_view_scale(view);
    int page_x = view->scroll_x + (int)(x / scale);
    int page_y = view->scroll_y + (int)(y / scale);
    if (ns_modifiers() & 2) {
        view->dragging = FALSE;
        ReleaseCapture();
        ns_start_link_navigation(view, page_x, page_y);
        return;
    }
    NsRequest *click = g_new0(NsRequest, 1);
    click->type = NS_REQ_CLICK;
    click->seq = ++view->click_seq;
    click->x = page_x;
    click->y = page_y;
    click->mods = ns_modifiers();
    ns_push_request(view, click);
    ns_start_scrollbar(view, 0, page_x, page_y);
}

static void
ns_handle_left_up(NsWinView *view, LPARAM lparam)
{
    int x = GET_X_LPARAM(lparam);
    int y = GET_Y_LPARAM(lparam) - ns_view_content_top(view);
    if (view->dragging)
        ReleaseCapture();
    view->dragging = FALSE;
    if (view->scrollbar_dragging || view->scrollbar_probe)
        ns_start_scrollbar(view, 2, 0, 0);
    view->scrollbar_dragging = FALSE;
    view->scrollbar_probe = FALSE;
    view->scrollbar_last_valid = FALSE;
    if (y < 0 || !view->opened)
        return;
    double scale = ns_view_scale(view);
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_RELEASE;
    request->x = view->scroll_x + (int)(x / scale);
    request->y = view->scroll_y + (int)(y / scale);
    ns_push_request(view, request);
}

static void
ns_handle_context(NsWinView *view, LPARAM lparam)
{
    POINT point;
    if (lparam == (LPARAM)-1) {
        point.x = view->pointer_x;
        point.y = view->pointer_y + ns_view_content_top(view);
        ClientToScreen(view->hwnd, &point);
    } else {
        point.x = GET_X_LPARAM(lparam);
        point.y = GET_Y_LPARAM(lparam);
    }
    view->context_point = point;
    POINT client = point;
    ScreenToClient(view->hwnd, &client);
    int top = ns_view_content_top(view);
    if (client.y < top || !view->opened)
        return;
    double scale = ns_view_scale(view);
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_CONTEXT;
    request->seq = ++view->context_seq;
    request->x = view->scroll_x + (int)(client.x / scale);
    request->y = view->scroll_y + (int)((client.y - top) / scale);
    ns_push_request(view, request);
}

static void
ns_handle_drop(NsWinView *view, HDROP drop)
{
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    POINT point = {0};
    DragQueryPoint(drop, &point);
    GString *paths = g_string_new(NULL);
    for (UINT i = 0; i < count; i++) {
        UINT length = DragQueryFileW(drop, i, NULL, 0);
        wchar_t *wide = g_new(wchar_t, (gsize)length + 1);
        DragQueryFileW(drop, i, wide, length + 1);
        char *path = ns_wide_to_utf8(wide);
        g_free(wide);
        if (path) {
            if (paths->len)
                g_string_append_c(paths, '\n');
            g_string_append(paths, path);
            g_free(path);
        }
    }
    DragFinish(drop);
    if (!view->opened || paths->len == 0) {
        g_string_free(paths, TRUE);
        return;
    }
    int top = ns_view_content_top(view);
    double scale = ns_view_scale(view);
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_DROP_FILES;
    request->x = view->scroll_x + (int)(point.x / scale);
    request->y = view->scroll_y +
        (int)(MAX(0, point.y - top) / scale);
    request->paths = g_string_free(paths, FALSE);
    ns_push_request(view, request);
}

static gboolean
ns_handle_page_key(NsWinView *view, UINT key)
{
    int mods = ns_modifiers();
    if (mods & 2) {
        ns_start_key(view, 0, key, 0, 0);
        switch (key) {
        case 'C':
            ns_start_select(view, 4, 0, 0);
            return TRUE;
        case 'V': {
            char *text = ns_clipboard_get_utf8(view->hwnd);
            ns_start_text(view, text);
            g_free(text);
            return TRUE;
        }
        case 'A':
            view->has_selection = TRUE;
            ns_start_select(view, 3, 0, 0);
            return TRUE;
        case 'F':
            ns_winview_find_open(view);
            return TRUE;
        case 'G':
            ns_find_request(view, (mods & 1) ? 2 : 1);
            return TRUE;
        case 'P':
            ns_export_page(view, TRUE);
            return TRUE;
        case VK_OEM_PLUS:
        case VK_ADD:
            ns_winview_zoom_in(view);
            return TRUE;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            ns_winview_zoom_out(view);
            return TRUE;
        case '0':
        case VK_NUMPAD0:
            ns_winview_zoom_reset(view);
            return TRUE;
        default:
            return FALSE;
        }
    }
    if ((mods & 4) && !(mods & (2 | 8))) {
        ns_start_key(view, 0, key, 0, 0);
        return TRUE;
    }
    int width = 1;
    int height = 1;
    ns_viewport_size(view, &width, &height);
    int line = 60;
    int page = MAX(line, (int)(height / ns_view_scale(view)) - line);
    int target_x = view->scroll_x;
    int target_y = view->scroll_y;
    switch (key) {
    case VK_TAB:
        ns_start_key(view, 0, key, 0, 0);
        return TRUE;
    case VK_DOWN:
        target_y += line;
        break;
    case VK_UP:
        target_y -= line;
        break;
    case VK_RIGHT:
        target_x += line;
        break;
    case VK_LEFT:
        target_x -= line;
        break;
    case VK_NEXT:
    case VK_SPACE:
        target_y += page;
        break;
    case VK_PRIOR:
        target_y -= page;
        break;
    case VK_HOME:
        target_y = 0;
        break;
    case VK_END:
        target_y = view->page_height;
        break;
    default:
        ns_start_key(view, 0, key, 0, 0);
        if ((key >= '0' && key <= 'Z') || key == VK_SPACE)
            ns_start_key(view, 3, key, 0, 0);
        return FALSE;
    }
    ns_start_key(view, 0, key, target_x, target_y);
    return TRUE;
}

static void
ns_handle_character(NsWinView *view, WCHAR character)
{
    WCHAR pair[3] = {0};
    int length = 1;
    if (character >= 0xD800 && character <= 0xDBFF) {
        view->pending_high_surrogate = character;
        return;
    }
    if (character >= 0xDC00 && character <= 0xDFFF &&
        view->pending_high_surrogate) {
        pair[0] = view->pending_high_surrogate;
        pair[1] = character;
        length = 2;
    } else {
        pair[0] = character;
    }
    view->pending_high_surrogate = 0;
    char *text = g_utf16_to_utf8((gunichar2 *)pair, length,
                                 NULL, NULL, NULL);
    if (text) {
        ns_start_text(view, text);
        g_free(text);
    }
}

static void
ns_do_load(NsWinView *view, const char *url, gboolean record,
           gboolean history, gboolean user_activated)
{
    if (!view || !url || !*url || ns_view_closed(view))
        return;
    if (view->permission_pending)
        ns_permission_resolve(view, FALSE);
    ns_audio_context_reset(view->audio_context);
    view->pending_record = record;
    int seq = ++view->load_seq;
    ++view->render_seq;
    ++view->context_seq;
    ++view->click_seq;
    ++view->viewport_seq;
    ++view->key_seq;
    ++view->select_seq;
    ++view->hover_seq;
    ++view->find_seq;
    view->render_pending = FALSE;
    view->render_inflight = FALSE;
    view->hover_inflight = FALSE;
    view->hover_pending = FALSE;
    view->has_selection = FALSE;
    view->opened = FALSE;
    view->page_animating = FALSE;
    view->caret_blinking = FALSE;
    KillTimer(view->hwnd, NS_TIMER_ANIM);
    g_clear_pointer(&view->frame_pixels, g_free);
    InvalidateRect(view->hwnd, NULL, TRUE);
    if (!view->loading) {
        view->loading = TRUE;
        ns_notify(view, NS_WINVIEW_LOADING, "1");
    }
    SetCursor(LoadCursorW(NULL, IDC_WAIT));
    ns_notify(view, NS_WINVIEW_STATUS, ns_i18n("Loading…"));
    int width = 1;
    int height = 1;
    ns_viewport_size(view, &width, &height);
    if (width <= 1 || height <= 1) {
        g_free(view->deferred_url);
        view->deferred_url = g_strdup(url);
        view->deferred_record = record;
        view->deferred_history = history;
        view->deferred_activated = user_activated;
        return;
    }
    view->last_viewport_width = width;
    view->last_viewport_height = height;
    NsRequest *request = g_new0(NsRequest, 1);
    request->type = NS_REQ_LOAD;
    request->seq = seq;
    request->url = g_strdup(url);
    request->viewport_width = width;
    request->viewport_height = height;
    request->history = history;
    request->user_activated = user_activated;
    ns_push_request(view, request);
}

gboolean
ns_winview_register(HINSTANCE instance)
{
    WNDCLASSEXW view_class = {
        .cbSize = sizeof view_class,
        .style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
        .lpfnWndProc = ns_winview_window_proc,
        .cbWndExtra = sizeof(void *),
        .hInstance = instance,
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
        .lpszClassName = NS_WINVIEW_CLASS,
    };
    WNDCLASSEXW dev_class = {
        .cbSize = sizeof dev_class,
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = ns_devtools_window_proc,
        .cbWndExtra = sizeof(void *),
        .hInstance = instance,
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1),
        .lpszClassName = NS_DEVTOOLS_CLASS,
    };
    ATOM first = RegisterClassExW(&view_class);
    if (!first && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
    ATOM second = RegisterClassExW(&dev_class);
    return second || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

NsWinView *
ns_winview_new(HWND parent, HINSTANCE instance)
{
    NsWinView *view = g_new0(NsWinView, 1);
    view->instance = instance;
    view->ui_font = ns_create_ui_font(GetDpiForWindow(parent));
    view->queue = g_async_queue_new();
    g_mutex_init(&view->proc_lock);
    view->history = g_ptr_array_new_with_free_func(g_free);
    view->history_index = -1;
    view->pending_record = TRUE;
    view->scale = 1.0;
    view->hwnd = CreateWindowExW(0, NS_WINVIEW_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN |
        WS_HSCROLL | WS_VSCROLL,
        0, 0, 1, 1, parent, NULL, instance, view);
    if (!view->hwnd) {
        g_ptr_array_unref(view->history);
        g_async_queue_unref(view->queue);
        g_mutex_clear(&view->proc_lock);
        g_free(view);
        return NULL;
    }
    view->find_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_AUTOHSCROLL,
        0, 0, 0, 0, view->hwnd, (HMENU)(INT_PTR)NS_FIND_EDIT,
        instance, NULL);
    view->find_label = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0, 0, 0, 0, view->hwnd, (HMENU)(INT_PTR)NS_FIND_LABEL,
        instance, NULL);
    view->find_prev = CreateWindowExW(0, L"BUTTON", L"↑",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, view->hwnd, (HMENU)(INT_PTR)NS_FIND_PREV,
        instance, NULL);
    view->find_next = CreateWindowExW(0, L"BUTTON", L"↓",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, view->hwnd, (HMENU)(INT_PTR)NS_FIND_NEXT,
        instance, NULL);
    view->find_close = CreateWindowExW(0, L"BUTTON", L"×",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, view->hwnd, (HMENU)(INT_PTR)NS_FIND_CLOSE,
        instance, NULL);
    view->perm_label = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
        0, 0, 0, 0, view->hwnd, (HMENU)(INT_PTR)NS_PERM_LABEL,
        instance, NULL);
    wchar_t *allow = ns_utf8_to_wide(ns_i18n("Allow"));
    view->perm_allow = CreateWindowExW(0, L"BUTTON", allow,
        WS_CHILD | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, view->hwnd, (HMENU)(INT_PTR)NS_PERM_ALLOW,
        instance, NULL);
    g_free(allow);
    wchar_t *deny = ns_utf8_to_wide(ns_i18n("Not now"));
    view->perm_deny = CreateWindowExW(0, L"BUTTON", deny,
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, view->hwnd, (HMENU)(INT_PTR)NS_PERM_DENY,
        instance, NULL);
    g_free(deny);
    HWND controls[] = {
        view->find_edit, view->find_label, view->find_prev,
        view->find_next, view->find_close, view->perm_label,
        view->perm_allow, view->perm_deny
    };
    for (gsize i = 0; i < G_N_ELEMENTS(controls); i++)
        ns_set_control_font(view, controls[i]);
    SetWindowSubclass(view->find_edit, ns_edit_subclass_proc, 3,
                      (DWORD_PTR)view);
    ns_show_find_controls(view, FALSE);
    ns_show_permission_controls(view, FALSE);
    DragAcceptFiles(view->hwnd, TRUE);
    view->thread = g_thread_new("ns-winview", ns_worker_main, view);
    return view;
}

HWND
ns_winview_hwnd(NsWinView *view)
{
    return view ? view->hwnd : NULL;
}

void
ns_winview_refresh_font(NsWinView *view, UINT dpi)
{
    if (!view)
        return;
    HFONT font = ns_create_ui_font(dpi);
    HFONT previous = view->ui_font;
    view->ui_font = font;
    HWND controls[] = {
        view->find_edit, view->find_label, view->find_prev, view->find_next,
        view->find_close, view->perm_label, view->perm_allow, view->perm_deny,
        view->dev_tab, view->dev_output, view->dev_input, view->dev_inspect,
        view->dev_refresh, view->dev_clear
    };
    for (gsize i = 0; i < G_N_ELEMENTS(controls); i++)
        if (controls[i])
            SendMessageW(controls[i], WM_SETFONT, (WPARAM)font, TRUE);
    ns_layout_bars(view);
    if (view->devtools)
        ns_dev_layout(view);
    ns_destroy_ui_font(previous);
}

void
ns_winview_destroy(NsWinView *view)
{
    if (!view)
        return;
    g_atomic_int_set(&view->closed, TRUE);
    if (view->permission_pending)
        ns_permission_resolve(view, FALSE);
    NsRequest *quit = g_new0(NsRequest, 1);
    quit->type = NS_REQ_QUIT;
    ns_push_request(view, quit);
    g_mutex_lock(&view->proc_lock);
    if (view->proc)
        ns_rproc_http_interrupt(view->proc);
    g_mutex_unlock(&view->proc_lock);
    if (view->thread) {
        g_thread_join(view->thread);
        view->thread = NULL;
    }
    if (view->devtools)
        DestroyWindow(view->devtools);
    MSG message;
    while (PeekMessageW(&message, view->hwnd, NS_WM_RESULT,
                        NS_WM_RESULT, PM_REMOVE))
        ns_result_free((NsResult *)message.lParam);
    if (view->hwnd)
        DestroyWindow(view->hwnd);
    NsRequest *request;
    while ((request = g_async_queue_try_pop(view->queue)))
        ns_request_free(request);
    g_async_queue_unref(view->queue);
    ns_audio_context_destroy(view->audio_context);
    g_ptr_array_unref(view->history);
    g_free(view->current_url);
    g_free(view->current_title);
    g_free(view->remote_ip);
    g_free(view->frame_pixels);
    g_free(view->context_link);
    g_free(view->permission_origin);
    g_free(view->deferred_url);
    for (int i = 0; i < NS_DEV_COUNT; i++)
        g_free(view->dev_text[i]);
    ns_destroy_ui_font(view->ui_font);
    g_mutex_clear(&view->proc_lock);
    g_free(view);
}

void
ns_winview_set_notify(NsWinView *view, NsWinViewNotify notify,
                      void *user_data)
{
    if (!view)
        return;
    view->notify = notify;
    view->notify_data = user_data;
}

void
ns_winview_set_private(NsWinView *view, gboolean private_mode)
{
    if (view)
        g_atomic_int_set(&view->private_mode, private_mode);
}

gboolean
ns_winview_is_private(const NsWinView *view)
{
    return view && g_atomic_int_get(&view->private_mode) != 0;
}

void
ns_winview_load(NsWinView *view, const char *url)
{
    if (!view)
        return;
    view->render_restarts = 0;
    ns_do_load(view, url, TRUE, FALSE, TRUE);
}

gboolean
ns_winview_can_back(const NsWinView *view)
{
    return view && view->history_index > 0;
}

gboolean
ns_winview_can_forward(const NsWinView *view)
{
    return view && view->history_index >= 0 &&
        view->history_index < (int)view->history->len - 1;
}

void
ns_winview_back(NsWinView *view)
{
    if (!ns_winview_can_back(view))
        return;
    view->history_index--;
    view->render_restarts = 0;
    ns_notify(view, NS_WINVIEW_HISTORY, NULL);
    ns_do_load(view, g_ptr_array_index(view->history, view->history_index),
               FALSE, TRUE, TRUE);
}

void
ns_winview_forward(NsWinView *view)
{
    if (!ns_winview_can_forward(view))
        return;
    view->history_index++;
    view->render_restarts = 0;
    ns_notify(view, NS_WINVIEW_HISTORY, NULL);
    ns_do_load(view, g_ptr_array_index(view->history, view->history_index),
               FALSE, TRUE, TRUE);
}

void
ns_winview_reload(NsWinView *view)
{
    if (!view)
        return;
    view->render_restarts = 0;
    if (view->history_index >= 0 &&
        view->history_index < (int)view->history->len)
        ns_do_load(view,
            g_ptr_array_index(view->history, view->history_index),
            FALSE, FALSE, TRUE);
    else if (view->current_url)
        ns_do_load(view, view->current_url, FALSE, FALSE, TRUE);
}

const char *
ns_winview_url(const NsWinView *view)
{
    return view ? view->current_url : NULL;
}

const char *
ns_winview_title(const NsWinView *view)
{
    return view ? view->current_title : NULL;
}

gboolean
ns_winview_is_loading(const NsWinView *view)
{
    return view && view->loading;
}

int
ns_winview_security(const NsWinView *view)
{
    return view ? view->security : 0;
}

const char *
ns_winview_remote_ip(const NsWinView *view)
{
    return view ? view->remote_ip : NULL;
}

int
ns_winview_renderer_pid(NsWinView *view)
{
    if (!view)
        return -1;
    g_mutex_lock(&view->proc_lock);
    int pid = view->proc ? ns_rproc_http_pid(view->proc) : -1;
    g_mutex_unlock(&view->proc_lock);
    return pid;
}

void
ns_winview_end_task(NsWinView *view)
{
    if (!view)
        return;
    g_mutex_lock(&view->proc_lock);
    if (view->proc) {
        ns_rproc_http_interrupt(view->proc);
        ns_rproc_http_terminate(view->proc);
    }
    g_mutex_unlock(&view->proc_lock);
}

static void
ns_set_zoom(NsWinView *view, double scale)
{
    int permille = (int)(scale * 1000.0 + 0.5);
    int minimum = (int)(NS_PROC_ZOOM_MIN * 1000.0 + 0.5);
    int maximum = (int)(NS_PROC_ZOOM_MAX * 1000.0 + 0.5);
    permille = CLAMP(permille, minimum, maximum);
    double adjusted = permille / 1000.0;
    if (adjusted == ns_view_scale(view))
        return;
    view->scale = adjusted;
    char status[64];
    g_snprintf(status, sizeof status, "%s %d%%", ns_i18n("Zoom"),
               permille / 10);
    ns_notify(view, NS_WINVIEW_STATUS, status);
    ns_configure_scrollbars(view);
    ns_request_render(view);
}

void
ns_winview_zoom_in(NsWinView *view)
{
    if (view)
        ns_set_zoom(view, ns_view_scale(view) * NS_PROC_ZOOM_STEP);
}

void
ns_winview_zoom_out(NsWinView *view)
{
    if (view)
        ns_set_zoom(view, ns_view_scale(view) / NS_PROC_ZOOM_STEP);
}

void
ns_winview_zoom_reset(NsWinView *view)
{
    if (view)
        ns_set_zoom(view, 1.0);
}

void
ns_winview_focus(NsWinView *view)
{
    if (view)
        SetFocus(view->hwnd);
}

void
ns_winview_find_open(NsWinView *view)
{
    if (!view)
        return;
    view->find_visible = TRUE;
    ns_show_find_controls(view, TRUE);
    ns_layout_bars(view);
    ns_maybe_update_viewport(view);
    SetFocus(view->find_edit);
    SendMessageW(view->find_edit, EM_SETSEL, 0, -1);
    char *query = ns_window_text_utf8(view->find_edit);
    if (query && *query)
        ns_find_request(view, 0);
    g_free(query);
}

void
ns_winview_toggle_devtools(NsWinView *view)
{
    if (!view || !view->opened)
        return;
    if (!view->devtools) {
        wchar_t *title = ns_utf8_to_wide(ns_i18n("Developer Tools"));
        HWND root = GetAncestor(view->hwnd, GA_ROOT);
        view->devtools = CreateWindowExW(WS_EX_TOOLWINDOW,
            NS_DEVTOOLS_CLASS, title, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            ns_view_px(view, 760), ns_view_px(view, 480),
            root, NULL, view->instance, view);
        g_free(title);
    }
    if (!view->devtools)
        return;
    view->devtools_open = !view->devtools_open;
    ShowWindow(view->devtools, view->devtools_open ? SW_SHOW : SW_HIDE);
    if (view->devtools_open) {
        SetForegroundWindow(view->devtools);
        SetFocus(view->dev_input);
        ns_dev_show_text(view, ns_dev_current_tab(view));
    } else {
        SetFocus(view->hwnd);
    }
}

void
ns_winview_layout(NsWinView *view, int x, int y, int width, int height)
{
    if (!view)
        return;
    MoveWindow(view->hwnd, x, y, MAX(1, width), MAX(1, height), TRUE);
}

static LRESULT CALLBACK
ns_edit_subclass_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                      UINT_PTR id, DWORD_PTR data)
{
    NsWinView *view = (NsWinView *)data;
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        if (id == 1)
            ns_dev_evaluate(view);
        else if (id == 2)
            ns_dev_inspect(view);
        else if (id == 3)
            ns_find_request(view,
                (GetKeyState(VK_SHIFT) & 0x8000) ? 2 : 1);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
        if (id == 3) {
            ns_find_close(view);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static LRESULT CALLBACK
ns_devtools_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    NsWinView *view = (NsWinView *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        view = create->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)view);
        if (view)
            view->devtools = hwnd;
    }
    if (!view)
        return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_CREATE:
        ns_dev_create_controls(view, hwnd);
        return 0;
    case WM_SIZE:
        ns_dev_layout(view);
        return 0;
    case WM_DPICHANGED:
        ns_apply_dpi_change(hwnd, lparam);
        ns_dev_layout(view);
        return 0;
    case WM_NOTIFY:
        if (((NMHDR *)lparam)->idFrom == NS_DEV_TAB &&
            ((NMHDR *)lparam)->code == TCN_SELCHANGE) {
            int tab = ns_dev_current_tab(view);
            ns_dev_show_text(view, tab);
            ns_dev_request_dump(view, tab);
            ns_dev_layout(view);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == NS_DEV_REFRESH) {
            ns_dev_request_dump(view, ns_dev_current_tab(view));
            return 0;
        }
        if (LOWORD(wparam) == NS_DEV_CLEAR) {
            int tab = ns_dev_current_tab(view);
            g_clear_pointer(&view->dev_text[tab], g_free);
            ns_dev_show_text(view, tab);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == NS_TIMER_CONSOLE && view->devtools_open &&
            view->opened) {
            NsRequest *request = g_new0(NsRequest, 1);
            request->type = NS_REQ_CONSOLE;
            ns_push_request(view, request);
        }
        return 0;
    case WM_CLOSE:
        view->devtools_open = FALSE;
        ShowWindow(hwnd, SW_HIDE);
        SetFocus(view->hwnd);
        return 0;
    case WM_NCDESTROY:
        KillTimer(hwnd, NS_TIMER_CONSOLE);
        view->devtools = NULL;
        view->dev_tab = NULL;
        view->dev_output = NULL;
        view->dev_input = NULL;
        view->dev_inspect = NULL;
        view->dev_refresh = NULL;
        view->dev_clear = NULL;
        return DefWindowProcW(hwnd, message, wparam, lparam);
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static LRESULT CALLBACK
ns_winview_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    NsWinView *view = (NsWinView *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        view = create->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)view);
        if (view)
            view->hwnd = hwnd;
    }
    if (!view)
        return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        ns_paint_view(view);
        return 0;
    case WM_SIZE:
        ns_layout_bars(view);
        if (view->deferred_url && LOWORD(lparam) > 1 && HIWORD(lparam) > 1) {
            char *url = view->deferred_url;
            gboolean record = view->deferred_record;
            gboolean history = view->deferred_history;
            gboolean activated = view->deferred_activated;
            view->deferred_url = NULL;
            ns_do_load(view, url, record, history, activated);
            g_free(url);
        } else if (view->opened) {
            ns_maybe_update_viewport(view);
        }
        return 0;
    case WM_SETFOCUS:
        ns_request_render(view);
        return 0;
    case WM_HSCROLL:
        ns_handle_scroll_message(view, SB_HORZ, wparam);
        return 0;
    case WM_VSCROLL:
        ns_handle_scroll_message(view, SB_VERT, wparam);
        return 0;
    case WM_MOUSEWHEEL:
        ns_handle_wheel(view, wparam, lparam, FALSE);
        return 0;
    case WM_MOUSEHWHEEL:
        ns_handle_wheel(view, wparam, lparam, TRUE);
        return 0;
    case WM_MOUSEMOVE:
        ns_handle_mouse_move(view, wparam, lparam);
        return 0;
    case WM_MOUSELEAVE:
        SetCursor(LoadCursorW(NULL, IDC_ARROW));
        return 0;
    case WM_LBUTTONDOWN:
        ns_handle_left_down(view, lparam);
        return 0;
    case WM_LBUTTONUP:
        ns_handle_left_up(view, lparam);
        return 0;
    case WM_MBUTTONDOWN: {
        int x = GET_X_LPARAM(lparam);
        int y = GET_Y_LPARAM(lparam) - ns_view_content_top(view);
        if (y >= 0 && view->opened) {
            double scale = ns_view_scale(view);
            int page_x = view->scroll_x + (int)(x / scale);
            int page_y = view->scroll_y + (int)(y / scale);
            ns_start_link_navigation(view, page_x, page_y);
        }
        return 0;
    }
    case WM_XBUTTONDOWN:
        if (GET_XBUTTON_WPARAM(wparam) == XBUTTON1)
            ns_winview_back(view);
        else if (GET_XBUTTON_WPARAM(wparam) == XBUTTON2)
            ns_winview_forward(view);
        return TRUE;
    case WM_CONTEXTMENU:
        ns_handle_context(view, lparam);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wparam == VK_F12) {
            ns_winview_toggle_devtools(view);
            return 0;
        }
        if (ns_handle_page_key(view, (UINT)wparam))
            return 0;
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        ns_start_key(view, 1, (UINT)wparam, 0, 0);
        return 0;
    case WM_CHAR:
        if (!(GetKeyState(VK_CONTROL) & 0x8000) &&
            !(GetKeyState(VK_MENU) & 0x8000))
            ns_handle_character(view, (WCHAR)wparam);
        return 0;
    case WM_DROPFILES:
        ns_handle_drop(view, (HDROP)wparam);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case NS_FIND_EDIT:
            if (HIWORD(wparam) == EN_CHANGE)
                ns_find_request(view, 0);
            return 0;
        case NS_FIND_PREV:
            ns_find_request(view, 2);
            return 0;
        case NS_FIND_NEXT:
            ns_find_request(view, 1);
            return 0;
        case NS_FIND_CLOSE:
            ns_find_close(view);
            return 0;
        case NS_PERM_ALLOW:
            ns_permission_resolve(view, TRUE);
            return 0;
        case NS_PERM_DENY:
            ns_permission_resolve(view, FALSE);
            return 0;
        default:
            break;
        }
        break;
    case WM_TIMER:
        if (wparam == NS_TIMER_ANIM)
            ns_request_render(view);
        return 0;
    case NS_WM_RESULT:
        ns_handle_result(view, (NsResult *)lparam);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT)
            return TRUE;
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
