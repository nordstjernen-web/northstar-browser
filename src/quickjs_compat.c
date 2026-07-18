/* Nordstjernen — browser-side shims for QuickJS APIs absent from upstream quickjs-ng.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: LicenseRef-NSL-1.0
 */

#include "quickjs_compat.h"

#include <glib.h>

JSContext *JS_GetCallerRealm(JSContext *ctx)
{
    return ctx;
}

JSContext *JS_GetFunctionRealm(JSContext *ctx, JSValueConst func_obj)
{
    (void)func_obj;
    return ctx;
}

G_LOCK_DEFINE_STATIC(ns_qjs_modpriv_lock);
static GHashTable *ns_qjs_modpriv_map;

int JS_SetModulePrivateValue(JSContext *ctx, JSModuleDef *m, JSValue val)
{
    G_LOCK(ns_qjs_modpriv_lock);
    if (!ns_qjs_modpriv_map)
        ns_qjs_modpriv_map = g_hash_table_new(NULL, NULL);
    JSValue *slot = g_hash_table_lookup(ns_qjs_modpriv_map, m);
    if (slot) {
        JS_FreeValue(ctx, *slot);
        *slot = val;
    } else {
        slot = g_new(JSValue, 1);
        *slot = val;
        g_hash_table_insert(ns_qjs_modpriv_map, m, slot);
    }
    G_UNLOCK(ns_qjs_modpriv_lock);
    return 0;
}

JSValue JS_GetModulePrivateValue(JSContext *ctx, JSModuleDef *m)
{
    JSValue r = JS_UNDEFINED;
    G_LOCK(ns_qjs_modpriv_lock);
    if (ns_qjs_modpriv_map) {
        JSValue *slot = g_hash_table_lookup(ns_qjs_modpriv_map, m);
        if (slot)
            r = JS_DupValue(ctx, *slot);
    }
    G_UNLOCK(ns_qjs_modpriv_lock);
    return r;
}

G_LOCK_DEFINE_STATIC(ns_qjs_loader_lock);
static GHashTable *ns_qjs_loader_map;

static JSModuleDef *ns_qjs_module_loader_thunk(JSContext *ctx,
                                               const char *module_name,
                                               void *opaque)
{
    JSModuleLoaderFunc2 *loader = NULL;
    G_LOCK(ns_qjs_loader_lock);
    if (ns_qjs_loader_map)
        loader = g_hash_table_lookup(ns_qjs_loader_map, JS_GetRuntime(ctx));
    G_UNLOCK(ns_qjs_loader_lock);
    if (!loader)
        return NULL;
    return loader(ctx, module_name, opaque, JS_UNDEFINED);
}

void JS_SetModuleLoaderFunc2(JSRuntime *rt,
                             JSModuleNormalizeFunc *module_normalize,
                             JSModuleLoaderFunc2 *module_loader,
                             JSModuleCheckSupportedImportAttributes *module_check_attrs,
                             void *opaque)
{
    (void)module_check_attrs;
    G_LOCK(ns_qjs_loader_lock);
    if (!ns_qjs_loader_map)
        ns_qjs_loader_map = g_hash_table_new(NULL, NULL);
    g_hash_table_insert(ns_qjs_loader_map, rt, (gpointer)module_loader);
    G_UNLOCK(ns_qjs_loader_lock);
    JS_SetModuleLoaderFunc(rt, module_normalize, ns_qjs_module_loader_thunk,
                           opaque);
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

void JS_MarkFunctionNative(JSContext *ctx, JSValueConst fn)
{
    (void)ctx;
    (void)fn;
}

JSValue JS_NewStringUTF16(JSContext *ctx, const uint16_t *buf, size_t len)
{
    GString *out = g_string_sized_new(len + 1);
    for (size_t i = 0; i < len; i++) {
        uint32_t c = buf[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 < len && buf[i + 1] >= 0xDC00 && buf[i + 1] <= 0xDFFF) {
                uint32_t lo = buf[++i];
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            } else {
                c = 0xFFFD;
            }
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            c = 0xFFFD;
        }
        g_string_append_unichar(out, (gunichar)c);
    }
    JSValue v = JS_NewStringLen(ctx, out->str, out->len);
    g_string_free(out, TRUE);
    return v;
}

JSValue JS_ThrowDOMException(JSContext *ctx, const char *name,
                            const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global, "DOMException");
    JS_FreeValue(ctx, global);
    if (!JS_IsFunction(ctx, ctor)) {
        JS_FreeValue(ctx, ctor);
        return JS_ThrowTypeError(ctx, "%s: %s", name ? name : "Error", buf);
    }
    JSValue argv[2];
    argv[0] = JS_NewString(ctx, buf);
    argv[1] = JS_NewString(ctx, name ? name : "Error");
    JSValue exc = JS_CallConstructor(ctx, ctor, 2, (JSValueConst *)argv);
    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, argv[1]);
    JS_FreeValue(ctx, ctor);
    if (JS_IsException(exc))
        return JS_EXCEPTION;
    return JS_Throw(ctx, exc);
}
