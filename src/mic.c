/* Northstar — microphone capture stubs; this edition does no audio capture.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mic.h"

#include <string.h>

gboolean ns_mic_acquire(void) { return FALSE; }
void     ns_mic_release(void) {}
void ns_mic_fill_time_domain(guint8 *out, int n) {
    if (out && n > 0) memset(out, 128, (gsize)n);
}
void ns_mic_fill_frequency(guint8 *out, int n) {
    if (out && n > 0) memset(out, 0, (gsize)n);
}
