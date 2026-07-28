/* Northstar — MPEG-1 video decoding for <video>.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_VIDEO_H
#define NS_VIDEO_H

#include <glib.h>

G_BEGIN_DECLS

gboolean ns_video_bytes_are_mpeg1(const guchar *data, gsize len);
gboolean ns_video_supports_mime(const char *mime);

GArray *ns_video_decode_mpeg1_to_pixels(const guchar *data, gsize len,
                                        int *out_w, int *out_h);

G_END_DECLS

#endif
