/* Nordstjernen — native ShadowRealm and AsyncContext for the QuickJS engine.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NS_JS_REALM_H
#define NS_JS_REALM_H

#include <quickjs.h>

void ns_js_realm_install(JSContext *ctx, JSValueConst global);

#endif
