/* Northstar — TextDecoder and TextEncoder from the Encoding Standard (QuickJS).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "quickjs_compat.h"
#include "js_internal.h"
#include "encoding.h"

#include <string.h>

typedef struct {
    const ns_encoding *enc;
    ns_decoder *decoder;
    gboolean fatal;
    gboolean ignore_bom;
    gboolean bom_seen;
    gboolean do_not_flush;
} ns_text_decoder;

static JSClassID ns_text_decoder_class_id;
static JSClassID ns_text_encoder_class_id;

static void
ns_text_decoder_finalizer(JSRuntime *rt, JSValue val)
{
    (void)rt;
    ns_text_decoder *td = JS_GetOpaque(val, ns_text_decoder_class_id);
    if (!td) return;
    ns_decoder_free(td->decoder);
    g_free(td);
}

static JSClassDef ns_text_decoder_class = {
    "TextDecoder", .finalizer = ns_text_decoder_finalizer,
};

static JSClassDef ns_text_encoder_class = {
    "TextEncoder", .finalizer = NULL,
};

static gboolean
ns_value_is_array_buffer_view(JSContext *ctx, JSValueConst v)
{
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ab = JS_GetPropertyStr(ctx, global, "ArrayBuffer");
    JSValue is_view = JS_GetPropertyStr(ctx, ab, "isView");
    JSValue r = JS_IsFunction(ctx, is_view)
        ? JS_Call(ctx, is_view, ab, 1, &v) : JS_FALSE;
    gboolean yes = JS_ToBool(ctx, r) > 0;
    JS_FreeValue(ctx, r);
    JS_FreeValue(ctx, is_view);
    JS_FreeValue(ctx, ab);
    JS_FreeValue(ctx, global);
    return yes;
}

static gboolean
ns_view_bytes(JSContext *ctx, JSValueConst buffer, uint64_t offset,
              uint64_t length, const uint8_t **data, size_t *len)
{
    size_t total = 0;
    uint8_t *base = JS_GetArrayBuffer(ctx, &total, buffer);
    if (!base) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return TRUE;
    }
    if (offset <= total && length <= total - offset) {
        *data = base + offset;
        *len = (size_t)length;
    }
    return TRUE;
}

gboolean
ns_js_buffer_source_bytes(JSContext *ctx, JSValueConst v, const uint8_t **data,
                          size_t *len)
{
    *data = NULL;
    *len = 0;
    if (!JS_IsObject(v)) return FALSE;
    if (JS_GetTypedArrayType(v) >= 0) {
        size_t offset = 0, length = 0;
        JSValue buf = JS_GetTypedArrayBuffer(ctx, v, &offset, &length, NULL);
        if (JS_IsException(buf)) {
            JS_FreeValue(ctx, JS_GetException(ctx));
            return TRUE;
        }
        ns_view_bytes(ctx, buf, offset, length, data, len);
        JS_FreeValue(ctx, buf);
        return TRUE;
    }
    size_t total = 0;
    uint8_t *base = JS_GetArrayBuffer(ctx, &total, v);
    if (base) {
        *data = base;
        *len = total;
        return TRUE;
    }
    JS_FreeValue(ctx, JS_GetException(ctx));
    if (JS_IsArrayBuffer(v)) return TRUE;
    if (!ns_value_is_array_buffer_view(ctx, v)) return FALSE;
    JSValue buf = JS_GetPropertyStr(ctx, v, "buffer");
    JSValue off_v = JS_GetPropertyStr(ctx, v, "byteOffset");
    JSValue len_v = JS_GetPropertyStr(ctx, v, "byteLength");
    uint64_t offset = 0, length = 0;
    if (JS_ToIndex(ctx, &offset, off_v) == 0 &&
        JS_ToIndex(ctx, &length, len_v) == 0)
        ns_view_bytes(ctx, buf, offset, length, data, len);
    else
        JS_FreeValue(ctx, JS_GetException(ctx));
    JS_FreeValue(ctx, len_v);
    JS_FreeValue(ctx, off_v);
    JS_FreeValue(ctx, buf);
    return TRUE;
}

static int
ns_dictionary_bool(JSContext *ctx, JSValueConst dict, const char *key,
                   gboolean *out)
{
    *out = FALSE;
    if (JS_IsUndefined(dict) || JS_IsNull(dict)) return 0;
    JSValue v = JS_GetPropertyStr(ctx, dict, key);
    if (JS_IsException(v)) return -1;
    *out = JS_ToBool(ctx, v) > 0;
    JS_FreeValue(ctx, v);
    return 0;
}

static JSValue
ns_check_dictionary(JSContext *ctx, int argc, JSValueConst *argv, int index,
                    const char *what)
{
    if (argc <= index || JS_IsUndefined(argv[index]) ||
        JS_IsNull(argv[index]) || JS_IsObject(argv[index]))
        return JS_UNDEFINED;
    return JS_ThrowTypeError(ctx, "%s: options must be an object", what);
}

static JSValue
ns_new_instance(JSContext *ctx, JSValueConst new_target, JSClassID class_id)
{
    JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
    if (JS_IsException(proto)) return proto;
    JSValue obj = JS_IsObject(proto)
        ? JS_NewObjectProtoClass(ctx, proto, class_id)
        : JS_NewObjectClass(ctx, (int)class_id);
    JS_FreeValue(ctx, proto);
    return obj;
}

static JSValue
ns_text_decoder_ctor(JSContext *ctx, JSValueConst new_target,
                     int argc, JSValueConst *argv)
{
    const ns_encoding *enc = ns_encoding_utf8();
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        size_t len = 0;
        const char *label = JS_ToCStringLen(ctx, &len, argv[0]);
        if (!label) return JS_EXCEPTION;
        enc = strlen(label) == len ? ns_encoding_for_label(label) : NULL;
        JS_FreeCString(ctx, label);
    }
    JSValue bad = ns_check_dictionary(ctx, argc, argv, 1, "TextDecoder");
    if (JS_IsException(bad)) return bad;
    JSValueConst options = argc >= 2 ? argv[1] : JS_UNDEFINED;
    gboolean fatal = FALSE, ignore_bom = FALSE;
    if (ns_dictionary_bool(ctx, options, "fatal", &fatal) < 0 ||
        ns_dictionary_bool(ctx, options, "ignoreBOM", &ignore_bom) < 0)
        return JS_EXCEPTION;
    if (!enc || ns_encoding_is_replacement(enc))
        return JS_ThrowRangeError(ctx,
            "TextDecoder: the encoding label is not supported");
    JSValue obj = ns_new_instance(ctx, new_target, ns_text_decoder_class_id);
    if (JS_IsException(obj)) return obj;
    ns_text_decoder *td = g_new0(ns_text_decoder, 1);
    td->enc = enc;
    td->fatal = fatal;
    td->ignore_bom = ignore_bom;
    td->decoder = ns_decoder_new(enc, fatal);
    JS_SetOpaque(obj, td);
    return obj;
}

static JSValue
ns_text_decoder_decode(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv)
{
    ns_text_decoder *td = JS_GetOpaque2(ctx, this_val, ns_text_decoder_class_id);
    if (!td) return JS_EXCEPTION;
    gboolean has_input = argc >= 1 && !JS_IsUndefined(argv[0]);
    const uint8_t *data = NULL;
    size_t len = 0;
    if (has_input && !ns_js_buffer_source_bytes(ctx, argv[0], &data, &len))
        return JS_ThrowTypeError(ctx,
            "TextDecoder.decode: input must be an ArrayBuffer or ArrayBufferView");
    JSValue bad = ns_check_dictionary(ctx, argc, argv, 1, "TextDecoder.decode");
    if (JS_IsException(bad)) return bad;
    gboolean stream = FALSE;
    if (ns_dictionary_bool(ctx, argc >= 2 ? argv[1] : JS_UNDEFINED, "stream",
                           &stream) < 0)
        return JS_EXCEPTION;
    if (has_input) ns_js_buffer_source_bytes(ctx, argv[0], &data, &len);

    if (!td->do_not_flush) {
        ns_decoder_reset(td->decoder);
        td->bom_seen = FALSE;
    }
    td->do_not_flush = stream;
    GString *out = g_string_sized_new(len + 1);
    if (!ns_decoder_decode(td->decoder, data, len, !stream, out)) {
        g_string_free(out, TRUE);
        return JS_ThrowTypeError(ctx,
            "TextDecoder.decode: the encoded data was not valid");
    }
    if (!td->ignore_bom && !td->bom_seen && out->len > 0 &&
        (ns_encoding_is_utf8(td->enc) || ns_encoding_is_utf16(td->enc))) {
        td->bom_seen = TRUE;
        if (out->len >= 3 && memcmp(out->str, "\xEF\xBB\xBF", 3) == 0)
            g_string_erase(out, 0, 3);
    }
    JSValue r = JS_NewStringLen(ctx, out->str, out->len);
    g_string_free(out, TRUE);
    return r;
}

static JSValue
ns_text_decoder_get_encoding(JSContext *ctx, JSValueConst this_val)
{
    ns_text_decoder *td = JS_GetOpaque2(ctx, this_val, ns_text_decoder_class_id);
    if (!td) return JS_EXCEPTION;
    char *name = g_ascii_strdown(ns_encoding_name(td->enc), -1);
    JSValue r = JS_NewString(ctx, name);
    g_free(name);
    return r;
}

static JSValue
ns_text_decoder_get_fatal(JSContext *ctx, JSValueConst this_val)
{
    ns_text_decoder *td = JS_GetOpaque2(ctx, this_val, ns_text_decoder_class_id);
    if (!td) return JS_EXCEPTION;
    return JS_NewBool(ctx, td->fatal);
}

static JSValue
ns_text_decoder_get_ignore_bom(JSContext *ctx, JSValueConst this_val)
{
    ns_text_decoder *td = JS_GetOpaque2(ctx, this_val, ns_text_decoder_class_id);
    if (!td) return JS_EXCEPTION;
    return JS_NewBool(ctx, td->ignore_bom);
}

static JSValue
ns_text_encoder_ctor(JSContext *ctx, JSValueConst new_target,
                     int argc, JSValueConst *argv)
{
    (void)argc; (void)argv;
    return ns_new_instance(ctx, new_target, ns_text_encoder_class_id);
}

static gboolean
ns_text_encoder_check(JSContext *ctx, JSValueConst this_val)
{
    if (JS_GetClassID(this_val) == ns_text_encoder_class_id) return TRUE;
    JS_ThrowTypeError(ctx, "Illegal invocation");
    return FALSE;
}

static JSValue
ns_text_encoder_get_encoding(JSContext *ctx, JSValueConst this_val)
{
    if (!ns_text_encoder_check(ctx, this_val)) return JS_EXCEPTION;
    return JS_NewString(ctx, "utf-8");
}

static char *
ns_usv_string_utf8(JSContext *ctx, JSValueConst v, size_t *out_len)
{
    size_t len = 0;
    const char *s = JS_ToCStringLen(ctx, &len, v);
    if (!s) return NULL;
    char *out = g_malloc(len + 1);
    size_t o = 0;
    for (size_t i = 0; i < len; ) {
        if ((guint8)s[i] == 0xED && i + 2 < len && (guint8)s[i + 1] >= 0xA0) {
            memcpy(out + o, "\xEF\xBF\xBD", 3);
            o += 3;
            i += 3;
        } else {
            out[o++] = s[i++];
        }
    }
    out[o] = '\0';
    JS_FreeCString(ctx, s);
    *out_len = o;
    return out;
}

static JSValue
ns_text_encoder_encode(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv)
{
    if (!ns_text_encoder_check(ctx, this_val)) return JS_EXCEPTION;
    size_t len = 0;
    char *utf8 = argc >= 1 && !JS_IsUndefined(argv[0])
        ? ns_usv_string_utf8(ctx, argv[0], &len) : g_strdup("");
    if (!utf8) return JS_EXCEPTION;
    JSValue r = JS_NewUint8ArrayCopy(ctx, (const uint8_t *)utf8, len);
    g_free(utf8);
    return r;
}

static JSValue
ns_text_encoder_encode_into(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    if (!ns_text_encoder_check(ctx, this_val)) return JS_EXCEPTION;
    size_t len = 0;
    char *utf8 = ns_usv_string_utf8(ctx, argc >= 1 ? argv[0] : JS_UNDEFINED,
                                    &len);
    if (!utf8) return JS_EXCEPTION;
    JSValueConst dest = argc >= 2 ? argv[1] : JS_UNDEFINED;
    if (JS_GetTypedArrayType(dest) != JS_TYPED_ARRAY_UINT8) {
        g_free(utf8);
        return JS_ThrowTypeError(ctx,
            "TextEncoder.encodeInto: destination must be a Uint8Array");
    }
    size_t room = 0;
    uint8_t *out = JS_GetUint8Array(ctx, &room, dest);
    if (!out) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        room = 0;
    }
    guint64 read = 0;
    size_t written = 0;
    for (size_t i = 0; i < len; ) {
        guint8 lead = (guint8)utf8[i];
        size_t n = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        if (written + n > room) break;
        memcpy(out + written, utf8 + i, n);
        written += n;
        i += n;
        read += n == 4 ? 2 : 1;
    }
    g_free(utf8);
    JSValue r = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, r, "read", JS_NewInt64(ctx, (int64_t)read));
    JS_SetPropertyStr(ctx, r, "written", JS_NewInt64(ctx, (int64_t)written));
    return r;
}

#define NS_ENC_FUNC(name, length, fn) \
    { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE, \
      JS_DEF_CFUNC, 0, \
      { .func = { length, JS_CFUNC_generic, { .generic = fn } } } }
#define NS_ENC_GETTER(name, fn) \
    { name, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE, JS_DEF_CGETSET, 0, \
      { .getset = { .get = { .getter = fn }, .set = { .setter = NULL } } } }

static const JSCFunctionListEntry ns_text_decoder_proto_funcs[] = {
    NS_ENC_GETTER("encoding", ns_text_decoder_get_encoding),
    NS_ENC_GETTER("fatal", ns_text_decoder_get_fatal),
    NS_ENC_GETTER("ignoreBOM", ns_text_decoder_get_ignore_bom),
    NS_ENC_FUNC("decode", 0, ns_text_decoder_decode),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TextDecoder",
                       JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry ns_text_encoder_proto_funcs[] = {
    NS_ENC_GETTER("encoding", ns_text_encoder_get_encoding),
    NS_ENC_FUNC("encode", 0, ns_text_encoder_encode),
    NS_ENC_FUNC("encodeInto", 2, ns_text_encoder_encode_into),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "TextEncoder",
                       JS_PROP_CONFIGURABLE),
};

static void
ns_encoding_install_interface(JSContext *ctx, JSValueConst global,
                              const char *name, JSCFunction *ctor,
                              JSClassID *class_id, JSClassDef *def,
                              const JSCFunctionListEntry *funcs, int nfuncs)
{
    ns_new_class_id(class_id);
    JS_NewClass(JS_GetRuntime(ctx), *class_id, def);
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, funcs, nfuncs);
    JSValue func = JS_NewCFunction2(ctx, ctor, name, 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, func, proto);
    JS_SetClassProto(ctx, *class_id, proto);
    JS_DefinePropertyValueStr(ctx, global, name, func,
                              JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
}

void
ns_encoding_install(JSContext *ctx, JSValueConst global)
{
    ns_encoding_install_interface(ctx, global, "TextDecoder",
        ns_text_decoder_ctor, &ns_text_decoder_class_id,
        &ns_text_decoder_class, ns_text_decoder_proto_funcs,
        (int)G_N_ELEMENTS(ns_text_decoder_proto_funcs));
    ns_encoding_install_interface(ctx, global, "TextEncoder",
        ns_text_encoder_ctor, &ns_text_encoder_class_id,
        &ns_text_encoder_class, ns_text_encoder_proto_funcs,
        (int)G_N_ELEMENTS(ns_text_encoder_proto_funcs));
}
