/* Nordstjernen — native ECMA-402 (Intl) implementation for the QuickJS engine.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NS_JS_INTL_H
#define NS_JS_INTL_H

#include <quickjs.h>

void ns_js_intl_install(JSContext *ctx, JSValueConst global);

#endif
