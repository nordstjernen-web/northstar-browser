/* Nordstjernen — browser-side shims for QuickJS APIs absent from upstream quickjs-ng.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_QUICKJS_COMPAT_H
#define NS_QUICKJS_COMPAT_H

#include <stdarg.h>
#include <stdint.h>

#include <quickjs.h>

typedef JSModuleDef *JSModuleLoaderFunc2(JSContext *ctx,
                                         const char *module_name, void *opaque,
                                         JSValueConst attributes);

typedef int JSModuleCheckSupportedImportAttributes(JSContext *ctx, void *opaque,
                                                    JSValueConst attributes);

JSContext *JS_GetCallerRealm(JSContext *ctx);
JSContext *JS_GetFunctionRealm(JSContext *ctx, JSValueConst func_obj);

JSValue JS_GetModulePrivateValue(JSContext *ctx, JSModuleDef *m);
int JS_SetModulePrivateValue(JSContext *ctx, JSModuleDef *m, JSValue val);

void JS_SetModuleLoaderFunc2(JSRuntime *rt,
                             JSModuleNormalizeFunc *module_normalize,
                             JSModuleLoaderFunc2 *module_loader,
                             JSModuleCheckSupportedImportAttributes *module_check_attrs,
                             void *opaque);

int JS_RepointArrayBuffer(JSContext *ctx, JSValueConst obj, uint8_t *data,
                          size_t byte_length);

void JS_MarkFunctionNative(JSContext *ctx, JSValueConst fn);

JSValue JS_NewStringUTF16(JSContext *ctx, const uint16_t *buf, size_t len);

JSValue JS_PRINTF_FORMAT_ATTR(3, 4)
    JS_ThrowDOMException(JSContext *ctx, const char *name,
                         JS_PRINTF_FORMAT const char *fmt, ...);

#endif
