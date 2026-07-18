/* Nordstjernen — IndexedDB backend bridge.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_IDB_H
#define NS_IDB_H

#include <quickjs.h>

void ns_idb_install(JSContext *ctx, JSValueConst global);

#endif
