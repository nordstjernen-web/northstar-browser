/* Northstar — browser-side shims that give quickjs-ng and the original QuickJS one API.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "quickjs_compat.h"

#include <glib.h>

JSClassID ns_new_class_id(JSClassID *pclass_id)
{
    static gint next_id = 192;
    gint id = g_atomic_int_get((gint *)pclass_id);
    if (id == 0) {
        gint fresh = g_atomic_int_add(&next_id, 1);
        if (!g_atomic_int_compare_and_exchange((gint *)pclass_id, 0, fresh))
            id = g_atomic_int_get((gint *)pclass_id);
        else
            id = fresh;
    }
    return (JSClassID)id;
}

JSContext *JS_GetCallerRealm(JSContext *ctx)
{
    return ctx;
}

JSContext *JS_GetFunctionRealm(JSContext *ctx, JSValueConst func_obj)
{
    (void)func_obj;
    return ctx;
}

int JS_RepointArrayBuffer(JSContext *ctx, JSValueConst obj, uint8_t *data,
                          size_t byte_length)
{
    (void)ctx;
    (void)obj;
    (void)data;
    (void)byte_length;
    return -1;
}

#ifdef NS_QUICKJS_ORIGINAL

#include <stdarg.h>
#include <string.h>

static JSClassID ns_quickjs_array_class;
static JSClassID ns_quickjs_error_class;
static JSClassID ns_quickjs_array_buffer_class;
static JSClassID ns_quickjs_typed_array_first_class;
static JSClassID ns_quickjs_typed_array_last_class;

static JSClassID ns_quickjs_class_of(JSContext *ctx, JSValue val)
{
    JSClassID id = JS_GetClassID(val);
    JS_FreeValue(ctx, val);
    return id;
}

static JSClassID ns_quickjs_typed_array_class(JSContext *ctx, JSValueConst buffer,
                                              JSTypedArrayEnum type)
{
    return ns_quickjs_class_of(ctx, JS_NewTypedArray(ctx, 1, &buffer, type));
}

static void ns_quickjs_probe_classes(JSContext *ctx)
{
    static const uint8_t bytes[8];
    ns_quickjs_array_class = ns_quickjs_class_of(ctx, JS_NewArray(ctx));
    ns_quickjs_error_class = ns_quickjs_class_of(ctx, JS_NewError(ctx));
    JSValue buffer = JS_NewArrayBufferCopy(ctx, bytes, sizeof(bytes));
    ns_quickjs_array_buffer_class = JS_GetClassID(buffer);
    ns_quickjs_typed_array_first_class =
        ns_quickjs_typed_array_class(ctx, buffer, JS_TYPED_ARRAY_UINT8C);
    ns_quickjs_typed_array_last_class =
        ns_quickjs_typed_array_class(ctx, buffer, JS_TYPED_ARRAY_FLOAT64);
    JS_FreeValue(ctx, buffer);
    g_assert(ns_quickjs_typed_array_last_class - ns_quickjs_typed_array_first_class ==
             JS_TYPED_ARRAY_FLOAT64 - JS_TYPED_ARRAY_UINT8C);
}

JSContext *ns_quickjs_new_context(JSRuntime *rt)
{
    static gsize probed;
    JSContext *ctx = (JS_NewContext)(rt);
    if (ctx && g_once_init_enter(&probed)) {
        ns_quickjs_probe_classes(ctx);
        g_once_init_leave(&probed, 1);
    }
    return ctx;
}

JSValue ns_quickjs_new_array_buffer(JSContext *ctx, uint8_t *buf, size_t len,
                                    size_t max_len,
                                    JSReallocArrayBufferDataFunc *realloc_func,
                                    void *opaque, bool is_shared)
{
    if (max_len != 0 || realloc_func)
        return JS_ThrowInternalError(ctx, "resizable external ArrayBuffer is not supported");
    return (JS_NewArrayBuffer)(ctx, buf, len, NULL, opaque, is_shared);
}

JSValue ns_quickjs_new_typed_array(JSContext *ctx, int argc, JSValueConst *argv,
                                   JSTypedArrayEnum type)
{
    JSValueConst padded[3] = { JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED };
    for (int i = 0; i < argc && i < 3; i++)
        padded[i] = argv[i];
    return (JS_NewTypedArray)(ctx, argc < 3 ? 3 : argc, argc < 3 ? padded : argv,
                              type);
}

bool ns_quickjs_is_array(JSValueConst val)
{
    return JS_GetClassID(val) == ns_quickjs_array_class;
}

bool ns_quickjs_is_error(JSValueConst val)
{
    return JS_GetClassID(val) == ns_quickjs_error_class;
}

const char *JS_GetVersion(void)
{
    return NS_QUICKJS_VERSION;
}

bool JS_IsArrayBuffer(JSValueConst obj)
{
    return JS_GetClassID(obj) == ns_quickjs_array_buffer_class;
}

int JS_GetTypedArrayType(JSValueConst obj)
{
    JSClassID id = JS_GetClassID(obj);
    if (id == JS_INVALID_CLASS_ID || id < ns_quickjs_typed_array_first_class ||
        id > ns_quickjs_typed_array_last_class)
        return -1;
    return (int)(id - ns_quickjs_typed_array_first_class) + JS_TYPED_ARRAY_UINT8C;
}

JSValue JS_NewUint8ArrayCopy(JSContext *ctx, const uint8_t *buf, size_t len)
{
    JSValue buffer = JS_NewArrayBufferCopy(ctx, buf, len);
    if (JS_IsException(buffer))
        return buffer;
    JSValue array = JS_NewTypedArray(ctx, 1, &buffer, JS_TYPED_ARRAY_UINT8);
    JS_FreeValue(ctx, buffer);
    return array;
}

uint8_t *JS_GetUint8Array(JSContext *ctx, size_t *psize, JSValueConst obj)
{
    *psize = 0;
    if (JS_GetTypedArrayType(obj) != JS_TYPED_ARRAY_UINT8) {
        JS_ThrowTypeError(ctx, "not a Uint8Array");
        return NULL;
    }
    size_t offset = 0, length = 0, buffer_size = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, obj, &offset, &length, NULL);
    if (JS_IsException(buffer))
        return NULL;
    uint8_t *data = JS_GetArrayBuffer(ctx, &buffer_size, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data)
        return NULL;
    if (offset > buffer_size || length > buffer_size - offset) {
        JS_ThrowTypeError(ctx, "out-of-bound access");
        return NULL;
    }
    *psize = length;
    return data + offset;
}

JSValue JS_ToObject(JSContext *ctx, JSValueConst val)
{
    if (JS_IsObject(val))
        return JS_DupValue(ctx, val);
    if (JS_IsNull(val) || JS_IsUndefined(val))
        return JS_ThrowTypeError(ctx, "cannot convert to object");
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue object_ctor = JS_GetPropertyStr(ctx, global, "Object");
    JS_FreeValue(ctx, global);
    JSValue obj = JS_Call(ctx, object_ctor, JS_UNDEFINED, 1, &val);
    JS_FreeValue(ctx, object_ctor);
    return obj;
}

JSValue JS_ThrowDOMException(JSContext *ctx, const char *name, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *message = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global, "DOMException");
    JS_FreeValue(ctx, global);
    JSValue error;
    if (JS_IsConstructor(ctx, ctor)) {
        JSValue args[2] = { JS_NewString(ctx, message), JS_NewString(ctx, name) };
        error = JS_CallConstructor(ctx, ctor, 2, args);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, args[1]);
    } else {
        error = JS_NewError(ctx);
        JS_DefinePropertyValueStr(ctx, error, "name", JS_NewString(ctx, name),
                                  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        JS_DefinePropertyValueStr(ctx, error, "message", JS_NewString(ctx, message),
                                  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    }
    JS_FreeValue(ctx, ctor);
    g_free(message);
    if (JS_IsException(error))
        return error;
    return JS_Throw(ctx, error);
}

JSValue JS_EvalThis2(JSContext *ctx, JSValueConst this_obj, const char *input,
                     size_t input_len, JSEvalOptions *options)
{
    const char *filename = options->filename ? options->filename : "<input>";
    size_t lead = options->line_num > 1 ? (size_t)options->line_num - 1 : 0;
    if (lead == 0)
        return JS_EvalThis(ctx, this_obj, input, input_len, filename,
                           options->eval_flags);
    char *shifted = g_malloc(lead + input_len + 1);
    memset(shifted, '\n', lead);
    memcpy(shifted + lead, input, input_len);
    shifted[lead + input_len] = '\0';
    JSValue result = JS_EvalThis(ctx, this_obj, shifted, lead + input_len, filename,
                                 options->eval_flags);
    g_free(shifted);
    return result;
}

#endif
