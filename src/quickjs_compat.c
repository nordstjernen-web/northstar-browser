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

static JSValue ns_quickjs_error_stack_set(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv,
                                          int magic, JSValueConst *func_data)
{
    (void)magic;
    if (!JS_IsObject(this_val))
        return JS_ThrowTypeError(ctx, "Error.prototype.stack setter called on a non-object");
    if (JS_IsStrictEqual(ctx, this_val, func_data[0]))
        return JS_ThrowTypeError(ctx, "Error.prototype.stack setter called on the home object");
    JSValueConst value = argc > 0 ? argv[0] : JS_UNDEFINED;
    JSAtom stack = JS_NewAtom(ctx, "stack");
    JSPropertyDescriptor desc;
    int own = JS_GetOwnProperty(ctx, &desc, this_val, stack);
    int ret = -1;
    if (own == 0) {
        ret = JS_DefinePropertyValue(ctx, this_val, stack, JS_DupValue(ctx, value),
                                     JS_PROP_C_W_E | JS_PROP_THROW);
    } else if (own > 0) {
        JS_FreeValue(ctx, desc.value);
        JS_FreeValue(ctx, desc.getter);
        JS_FreeValue(ctx, desc.setter);
        ret = JS_SetProperty(ctx, this_val, stack, JS_DupValue(ctx, value));
    }
    JS_FreeAtom(ctx, stack);
    return ret < 0 ? JS_EXCEPTION : JS_UNDEFINED;
}

static void ns_quickjs_accept_any_error_stack(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue error = JS_GetPropertyStr(ctx, global, "Error");
    JSValue proto = JS_GetPropertyStr(ctx, error, "prototype");
    JSAtom stack = JS_NewAtom(ctx, "stack");
    JSPropertyDescriptor desc;
    if (JS_IsObject(proto) && JS_GetOwnProperty(ctx, &desc, proto, stack) > 0) {
        if ((desc.flags & JS_PROP_GETSET) && JS_IsFunction(ctx, desc.setter)) {
            JSValue setter = JS_NewCFunctionData(ctx, ns_quickjs_error_stack_set,
                                                 1, 0, 1, (JSValueConst *)&proto);
            JS_DefinePropertyValueStr(ctx, setter, "name",
                                      JS_NewString(ctx, "set stack"),
                                      JS_PROP_CONFIGURABLE);
            JS_DefineProperty(ctx, proto, stack, JS_UNDEFINED, desc.getter, setter,
                              JS_PROP_HAS_GET | JS_PROP_HAS_SET);
            JS_FreeValue(ctx, setter);
        }
        JS_FreeValue(ctx, desc.value);
        JS_FreeValue(ctx, desc.getter);
        JS_FreeValue(ctx, desc.setter);
    }
    if (JS_HasException(ctx))
        JS_FreeValue(ctx, JS_GetException(ctx));
    JS_FreeAtom(ctx, stack);
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, error);
    JS_FreeValue(ctx, global);
}

#ifdef NS_QUICKJS_ORIGINAL
static void ns_quickjs_learn_class_ids_once(JSContext *ctx);
#endif

JSContext *ns_quickjs_new_context(JSRuntime *rt)
{
    JSContext *ctx = (JS_NewContext)(rt);
    if (!ctx) return NULL;
#ifdef NS_QUICKJS_ORIGINAL
    ns_quickjs_learn_class_ids_once(ctx);
#endif
    ns_quickjs_accept_any_error_stack(ctx);
    return ctx;
}

#ifdef NS_QUICKJS_ORIGINAL

#include <stdarg.h>
#include <string.h>

typedef struct ns_quickjs_class_ids {
    JSClassID array;
    JSClassID error;
    JSClassID array_buffer;
    JSClassID typed_array[JS_TYPED_ARRAY_FLOAT64 + 1];
} ns_quickjs_class_ids;

typedef struct ns_quickjs_array_buffer_owner {
    JSReallocArrayBufferDataFunc *realloc_func;
    void *opaque;
} ns_quickjs_array_buffer_owner;

static ns_quickjs_class_ids ns_quickjs_classes;

static JSClassID ns_quickjs_class_of(JSContext *ctx, JSValue val)
{
    JSClassID id = JS_GetClassID(val);
    if (JS_IsException(val))
        JS_FreeValue(ctx, JS_GetException(ctx));
    JS_FreeValue(ctx, val);
    return id;
}

static void ns_quickjs_learn_class_ids(JSContext *ctx)
{
    static const uint8_t one_byte;
    ns_quickjs_class_ids *ids = &ns_quickjs_classes;
    ids->array = ns_quickjs_class_of(ctx, JS_NewArray(ctx));
    ids->error = ns_quickjs_class_of(ctx, JS_NewError(ctx));
    ids->array_buffer = ns_quickjs_class_of(ctx, JS_NewArrayBufferCopy(ctx, &one_byte, 1));
    JSValue zero = JS_NewInt32(ctx, 0);
    for (int type = JS_TYPED_ARRAY_UINT8C; type <= JS_TYPED_ARRAY_FLOAT64; type++)
        ids->typed_array[type] = ns_quickjs_class_of(ctx,
            JS_NewTypedArray(ctx, 1, &zero, (JSTypedArrayEnum)type));
}

static void ns_quickjs_learn_class_ids_once(JSContext *ctx)
{
    static gsize learned;
    if (g_once_init_enter(&learned)) {
        ns_quickjs_learn_class_ids(ctx);
        g_once_init_leave(&learned, 1);
    }
}

static bool ns_quickjs_has_class(JSValueConst val, JSClassID class_id)
{
    return class_id != JS_INVALID_CLASS_ID && JS_GetClassID(val) == class_id;
}

bool ns_quickjs_is_array(JSValueConst val)
{
    return ns_quickjs_has_class(val, ns_quickjs_classes.array);
}

bool ns_quickjs_is_error(JSValueConst val)
{
    return ns_quickjs_has_class(val, ns_quickjs_classes.error);
}

bool JS_IsArrayBuffer(JSValueConst obj)
{
    return ns_quickjs_has_class(obj, ns_quickjs_classes.array_buffer);
}

int JS_GetTypedArrayType(JSValueConst obj)
{
    for (int type = JS_TYPED_ARRAY_UINT8C; type <= JS_TYPED_ARRAY_FLOAT64; type++)
        if (ns_quickjs_has_class(obj, ns_quickjs_classes.typed_array[type]))
            return type;
    return -1;
}

static void ns_quickjs_array_buffer_free(JSRuntime *rt, void *opaque, void *ptr)
{
    ns_quickjs_array_buffer_owner *owner = opaque;
    owner->realloc_func(rt, owner->opaque, ptr, 0);
    g_free(owner);
}

JSValue ns_quickjs_new_array_buffer(JSContext *ctx, uint8_t *buf, size_t len,
                                    size_t max_len,
                                    JSReallocArrayBufferDataFunc *realloc_func,
                                    void *opaque, bool is_shared)
{
    if (max_len != 0)
        return JS_ThrowRangeError(ctx, "resizable external ArrayBuffer is not supported");
    if (!realloc_func)
        return (JS_NewArrayBuffer)(ctx, buf, len, NULL, opaque, is_shared);
    ns_quickjs_array_buffer_owner *owner = g_new(ns_quickjs_array_buffer_owner, 1);
    owner->realloc_func = realloc_func;
    owner->opaque = opaque;
    JSValue buffer = (JS_NewArrayBuffer)(ctx, buf, len, ns_quickjs_array_buffer_free,
                                         owner, is_shared);
    if (JS_IsException(buffer))
        g_free(owner);
    return buffer;
}

JSValue ns_quickjs_new_typed_array(JSContext *ctx, int argc, JSValueConst *argv,
                                   JSTypedArrayEnum type)
{
    JSValueConst padded[3] = { JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED };
    if (argc >= 3)
        return (JS_NewTypedArray)(ctx, argc, argv, type);
    for (int i = 0; i < argc; i++)
        padded[i] = argv[i];
    return (JS_NewTypedArray)(ctx, 3, padded, type);
}

const char *JS_GetVersion(void)
{
    return NS_QUICKJS_VERSION;
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
        if (!JS_IsException(error)) {
            JS_DefinePropertyValueStr(ctx, error, "name", JS_NewString(ctx, name),
                                      JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
            JS_DefinePropertyValueStr(ctx, error, "message", JS_NewString(ctx, message),
                                      JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        }
    }
    JS_FreeValue(ctx, ctor);
    g_free(message);
    if (JS_IsException(error))
        return JS_EXCEPTION;
    return JS_Throw(ctx, error);
}

JSValue JS_EvalThis2(JSContext *ctx, JSValueConst this_obj, const char *input,
                     size_t input_len, JSEvalOptions *options)
{
    const char *filename = options->filename ? options->filename : "<unnamed>";
    size_t lines = options->line_num > 1 ? (size_t)options->line_num - 1 : 0;
    gboolean hashbang = input_len >= 2 && input[0] == '#' && input[1] == '!';
    if (lines == 0 || hashbang || input_len > G_MAXSIZE - lines - 1)
        return JS_EvalThis(ctx, this_obj, input, input_len, filename,
                           options->eval_flags);
    char *shifted = g_try_malloc(lines + input_len + 1);
    if (!shifted)
        return JS_ThrowOutOfMemory(ctx);
    memset(shifted, '\n', lines);
    memcpy(shifted + lines, input, input_len);
    shifted[lines + input_len] = '\0';
    JSValue result = JS_EvalThis(ctx, this_obj, shifted, lines + input_len, filename,
                                 options->eval_flags);
    g_free(shifted);
    return result;
}

#endif
