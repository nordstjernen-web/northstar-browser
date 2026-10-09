/* Northstar — native side of the MediaSource and SourceBuffer bindings.
 * Copyright 2026 Gabriel Ferreira
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "js_internal.h"

#include <math.h>
#include <string.h>

#include "mse.h"

typedef struct {
    ns_mse_source *source;
    ns_mse_buffer *buffer;
} mse_handle;

static void
mse_handle_free(gpointer data)
{
    mse_handle *handle = data;
    if (!handle) return;
    ns_mse_source_unref(handle->source);
    g_free(handle);
}

static void
mse_ensure_tables(ns_js *js)
{
    if (!js->mse_objects)
        js->mse_objects = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                NULL, mse_handle_free);
    if (!js->mse_urls)
        js->mse_urls = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                             (GDestroyNotify)ns_mse_source_unref);
}

static mse_handle *
mse_lookup(JSContext *ctx, JSValueConst id_value)
{
    ns_js *js = js_from_ctx(ctx);
    uint32_t id = 0;
    if (!js || !js->mse_objects || JS_ToUint32(ctx, &id, id_value)) return NULL;
    return g_hash_table_lookup(js->mse_objects, GUINT_TO_POINTER(id));
}

static guint
mse_register(ns_js *js, ns_mse_source *source, ns_mse_buffer *buffer)
{
    mse_ensure_tables(js);
    mse_handle *handle = g_new0(mse_handle, 1);
    handle->source = ns_mse_source_ref(source);
    handle->buffer = buffer;
    guint id = ++js->next_mse_id;
    g_hash_table_insert(js->mse_objects, GUINT_TO_POINTER(id), handle);
    return id;
}

static gboolean
mse_bytes(JSContext *ctx, JSValueConst value, const guint8 **data, size_t *len,
          JSValue *holder)
{
    *data = NULL;
    *len = 0;
    *holder = JS_UNDEFINED;
    if (JS_IsArrayBuffer(value)) {
        size_t total = 0;
        guint8 *base = JS_GetArrayBuffer(ctx, &total, value);
        if (!base) return FALSE;
        *data = base;
        *len = total;
        *holder = JS_DupValue(ctx, value);
        return TRUE;
    }
    size_t offset = 0, byte_len = 0, element = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &byte_len, &element);
    if (JS_IsException(buffer)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return FALSE;
    }
    size_t total = 0;
    guint8 *base = JS_GetArrayBuffer(ctx, &total, buffer);
    if (!base || offset + byte_len > total) {
        JS_FreeValue(ctx, buffer);
        return FALSE;
    }
    *data = base + offset;
    *len = byte_len;
    *holder = buffer;
    return TRUE;
}

static JSValue
ranges_to_array(JSContext *ctx, GArray *ranges)
{
    JSValue array = JS_NewArray(ctx);
    for (guint i = 0; i < ranges->len; i++)
        JS_SetPropertyUint32(ctx, array, i,
                             JS_NewFloat64(ctx, g_array_index(ranges, double, i)));
    g_array_free(ranges, TRUE);
    return array;
}

static JSValue
js_mse_type_supported(JSContext *ctx, JSValueConst this_val, int argc,
                      JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) return JS_FALSE;
    const char *type = JS_ToCString(ctx, argv[0]);
    if (!type) return JS_EXCEPTION;
    gboolean ok = ns_mse_type_supported(type);
    JS_FreeCString(ctx, type);
    return JS_NewBool(ctx, ok);
}

static JSValue
js_mse_create(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    ns_js *js = js_from_ctx(ctx);
    if (!js) return JS_NewUint32(ctx, 0);
    ns_mse_source *source = ns_mse_source_new();
    guint id = mse_register(js, source, NULL);
    ns_mse_source_unref(source);
    return JS_NewUint32(ctx, id);
}

static JSValue
js_mse_bind_url(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    ns_js *js = js_from_ctx(ctx);
    mse_handle *handle = argc >= 2 ? mse_lookup(ctx, argv[0]) : NULL;
    if (!js || !handle || handle->buffer) return JS_FALSE;
    const char *url = JS_ToCString(ctx, argv[1]);
    if (!url) return JS_EXCEPTION;
    g_hash_table_replace(js->mse_urls, g_strdup(url),
                         ns_mse_source_ref(handle->source));
    JS_FreeCString(ctx, url);
    return JS_TRUE;
}

static JSValue
js_mse_add_buffer(JSContext *ctx, JSValueConst this_val, int argc,
                  JSValueConst *argv)
{
    (void)this_val;
    ns_js *js = js_from_ctx(ctx);
    mse_handle *handle = argc >= 2 ? mse_lookup(ctx, argv[0]) : NULL;
    if (!js || !handle || handle->buffer) return JS_NewUint32(ctx, 0);
    const char *type = JS_ToCString(ctx, argv[1]);
    if (!type) return JS_EXCEPTION;
    ns_mse_buffer *buffer = ns_mse_source_add_buffer(handle->source, type);
    JS_FreeCString(ctx, type);
    if (!buffer) return JS_NewUint32(ctx, 0);
    return JS_NewUint32(ctx, mse_register(js, handle->source, buffer));
}

static JSValue
js_mse_remove_buffer(JSContext *ctx, JSValueConst this_val, int argc,
                     JSValueConst *argv)
{
    (void)this_val;
    ns_js *js = js_from_ctx(ctx);
    mse_handle *handle = argc >= 1 ? mse_lookup(ctx, argv[0]) : NULL;
    if (!js || !handle || !handle->buffer) return JS_FALSE;
    uint32_t id = 0;
    JS_ToUint32(ctx, &id, argv[0]);
    ns_mse_source_remove_buffer(handle->source, handle->buffer);
    g_hash_table_remove(js->mse_objects, GUINT_TO_POINTER(id));
    return JS_TRUE;
}

static JSValue
js_mse_append(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    mse_handle *handle = argc >= 6 ? mse_lookup(ctx, argv[0]) : NULL;
    JSValue result = JS_NewObject(ctx);
    if (!handle || !handle->buffer) {
        JS_SetPropertyStr(ctx, result, "status", JS_NewString(ctx, "error"));
        return result;
    }
    const guint8 *data = NULL;
    size_t len = 0;
    JSValue holder = JS_UNDEFINED;
    double offset = 0, window_start = 0, window_end = INFINITY;
    if (!mse_bytes(ctx, argv[1], &data, &len, &holder) ||
        JS_ToFloat64(ctx, &offset, argv[2]) ||
        JS_ToFloat64(ctx, &window_start, argv[4]) ||
        JS_ToFloat64(ctx, &window_end, argv[5])) {
        JS_FreeValue(ctx, holder);
        JS_SetPropertyStr(ctx, result, "status", JS_NewString(ctx, "error"));
        return result;
    }
    gboolean sequence = JS_ToBool(ctx, argv[3]) > 0;
    ns_mse_append_result status = ns_mse_buffer_append(handle->buffer, data, len,
                                                       &offset, sequence,
                                                       window_start, window_end);
    JS_FreeValue(ctx, holder);
    const char *name = status == NS_MSE_APPEND_OK ? "ok"
                     : status == NS_MSE_APPEND_QUOTA_EXCEEDED ? "quota" : "error";
    JS_SetPropertyStr(ctx, result, "status", JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, result, "offset", JS_NewFloat64(ctx, offset));
    JS_SetPropertyStr(ctx, result, "ranges",
                      ranges_to_array(ctx, ns_mse_buffer_ranges(handle->buffer)));
    return result;
}

static JSValue
js_mse_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    mse_handle *handle = argc >= 3 ? mse_lookup(ctx, argv[0]) : NULL;
    if (!handle || !handle->buffer) return JS_NewArray(ctx);
    double start = 0, end = 0;
    if (JS_ToFloat64(ctx, &start, argv[1]) || JS_ToFloat64(ctx, &end, argv[2]))
        return JS_EXCEPTION;
    ns_mse_buffer_remove(handle->buffer, start, end);
    return ranges_to_array(ctx, ns_mse_buffer_ranges(handle->buffer));
}

static JSValue
js_mse_abort(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    mse_handle *handle = argc >= 1 ? mse_lookup(ctx, argv[0]) : NULL;
    if (handle && handle->buffer) ns_mse_buffer_abort(handle->buffer);
    return JS_UNDEFINED;
}

static JSValue
js_mse_ranges(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    (void)this_val;
    mse_handle *handle = argc >= 1 ? mse_lookup(ctx, argv[0]) : NULL;
    if (!handle || !handle->buffer) return JS_NewArray(ctx);
    return ranges_to_array(ctx, ns_mse_buffer_ranges(handle->buffer));
}

static JSValue
js_mse_set_duration(JSContext *ctx, JSValueConst this_val, int argc,
                    JSValueConst *argv)
{
    (void)this_val;
    mse_handle *handle = argc >= 2 ? mse_lookup(ctx, argv[0]) : NULL;
    double duration = NAN;
    if (!handle || JS_ToFloat64(ctx, &duration, argv[1])) return JS_UNDEFINED;
    ns_mse_source_set_duration(handle->source, duration);
    return JS_UNDEFINED;
}

static JSValue
js_mse_set_ended(JSContext *ctx, JSValueConst this_val, int argc,
                 JSValueConst *argv)
{
    (void)this_val;
    mse_handle *handle = argc >= 2 ? mse_lookup(ctx, argv[0]) : NULL;
    if (handle) ns_mse_source_set_ended(handle->source, JS_ToBool(ctx, argv[1]) > 0);
    return JS_UNDEFINED;
}

static JSValue
js_mse_set_live_seekable_range(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv)
{
    (void)this_val;
    mse_handle *handle = argc >= 1 ? mse_lookup(ctx, argv[0]) : NULL;
    double start = 0.0, end = 0.0;
    if (!handle) return JS_UNDEFINED;
    if (argc < 3 || JS_ToFloat64(ctx, &start, argv[1]) ||
        JS_ToFloat64(ctx, &end, argv[2]))
        ns_mse_source_clear_live_seekable_range(handle->source);
    else
        ns_mse_source_set_live_seekable_range(handle->source, start, end);
    return JS_UNDEFINED;
}

ns_mse_source *
ns_js_mse_source_for_url(ns_js *js, const char *url)
{
    if (!js || !js->mse_urls || !url) return NULL;
    return g_hash_table_lookup(js->mse_urls, url);
}

void
ns_js_mse_teardown(ns_js *js)
{
    if (!js) return;
    g_clear_pointer(&js->mse_urls, g_hash_table_destroy);
    g_clear_pointer(&js->mse_objects, g_hash_table_destroy);
}

void
ns_js_mse_install(JSContext *ctx, JSValueConst global)
{
    ns_bind_fn(ctx, global, "__ndMseTypeSupported", js_mse_type_supported, 1);
    ns_bind_fn(ctx, global, "__ndMseCreate",        js_mse_create,         0);
    ns_bind_fn(ctx, global, "__ndMseBindUrl",       js_mse_bind_url,       2);
    ns_bind_fn(ctx, global, "__ndMseAddBuffer",     js_mse_add_buffer,     2);
    ns_bind_fn(ctx, global, "__ndMseRemoveBuffer",  js_mse_remove_buffer,  1);
    ns_bind_fn(ctx, global, "__ndMseAppend",        js_mse_append,         6);
    ns_bind_fn(ctx, global, "__ndMseRemove",        js_mse_remove,         3);
    ns_bind_fn(ctx, global, "__ndMseAbort",         js_mse_abort,          1);
    ns_bind_fn(ctx, global, "__ndMseRanges",        js_mse_ranges,         1);
    ns_bind_fn(ctx, global, "__ndMseSetDuration",   js_mse_set_duration,   2);
    ns_bind_fn(ctx, global, "__ndMseSetEnded",      js_mse_set_ended,      2);
    ns_bind_fn(ctx, global, "__ndMseSetLiveSeekableRange",
               js_mse_set_live_seekable_range, 3);
}
