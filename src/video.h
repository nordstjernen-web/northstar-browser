/* Northstar — MPEG-1 video decoding for <video>.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_VIDEO_H
#define NS_VIDEO_H

#include <glib.h>

#include "texture.h"

G_BEGIN_DECLS

typedef struct ns_video_stream ns_video_stream;

gboolean ns_video_bytes_are_mpeg1(const guchar *data, gsize len);

typedef struct ns_mse_source ns_mse_source;

ns_video_stream *ns_video_stream_new(const guchar *data, gsize len);
ns_video_stream *ns_video_stream_new_mse(ns_mse_source *source);
void             ns_video_stream_free(ns_video_stream *stream);

int          ns_video_stream_width(const ns_video_stream *stream);
int          ns_video_stream_height(const ns_video_stream *stream);
int          ns_video_stream_duration_ms(const ns_video_stream *stream);
gboolean     ns_video_stream_has_audio(const ns_video_stream *stream);
GBytes      *ns_video_stream_bytes(const ns_video_stream *stream);
gsize        ns_video_stream_memory(const ns_video_stream *stream);
ns_texture  *ns_video_stream_texture(const ns_video_stream *stream);
gboolean     ns_video_stream_show(ns_video_stream *stream, int phase_ms);
gboolean     ns_video_stream_is_mse(const ns_video_stream *stream);

G_END_DECLS

#endif
