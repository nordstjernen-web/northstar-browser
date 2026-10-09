/* Northstar — Media Source Extensions: source buffers holding demuxed coded frames.
 * Copyright 2026 Gabriel Ferreira
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_MSE_H
#define NS_MSE_H

#include <glib.h>

#include "mp4.h"

G_BEGIN_DECLS

typedef struct ns_mse_source ns_mse_source;
typedef struct ns_mse_buffer ns_mse_buffer;

typedef enum {
    NS_MSE_TRACK_NONE,
    NS_MSE_TRACK_VIDEO,
    NS_MSE_TRACK_AUDIO,
} ns_mse_track_kind;

typedef struct {
    double   pts;
    double   dts;
    double   duration;
    gboolean keyframe;
    GBytes  *data;
    GBytes  *config;
} ns_mse_frame;

typedef enum {
    NS_MSE_APPEND_OK,
    NS_MSE_APPEND_PARSE_ERROR,
    NS_MSE_APPEND_QUOTA_EXCEEDED,
} ns_mse_append_result;

gboolean       ns_mse_type_supported(const char *type);

ns_mse_source *ns_mse_source_new(void);
ns_mse_source *ns_mse_source_ref(ns_mse_source *source);
void           ns_mse_source_unref(ns_mse_source *source);
double         ns_mse_source_duration(const ns_mse_source *source);
void           ns_mse_source_set_duration(ns_mse_source *source, double duration);
void           ns_mse_source_set_live_seekable_range(ns_mse_source *source,
                                                     double start, double end);
void           ns_mse_source_clear_live_seekable_range(ns_mse_source *source);
gboolean       ns_mse_source_ended(const ns_mse_source *source);
void           ns_mse_source_set_ended(ns_mse_source *source, gboolean ended);
guint          ns_mse_source_generation(const ns_mse_source *source);
ns_mse_buffer *ns_mse_source_add_buffer(ns_mse_source *source, const char *type);
void           ns_mse_source_remove_buffer(ns_mse_source *source,
                                           ns_mse_buffer *buffer);
ns_mse_buffer *ns_mse_source_track_buffer(ns_mse_source *source,
                                          ns_mse_track_kind kind);
GArray        *ns_mse_source_buffered(ns_mse_source *source);
GArray        *ns_mse_source_seekable(ns_mse_source *source);

ns_mse_append_result ns_mse_buffer_append(ns_mse_buffer *buffer,
                                          const guint8 *data, gsize len,
                                          double *timestamp_offset,
                                          gboolean sequence_mode,
                                          double window_start,
                                          double window_end);
void           ns_mse_buffer_remove(ns_mse_buffer *buffer, double start,
                                    double end);
void           ns_mse_buffer_abort(ns_mse_buffer *buffer);
GArray        *ns_mse_buffer_ranges(const ns_mse_buffer *buffer);
ns_mse_track_kind ns_mse_buffer_kind(const ns_mse_buffer *buffer);
const ns_mp4_track *ns_mse_buffer_track(const ns_mse_buffer *buffer);
guint          ns_mse_buffer_n_frames(const ns_mse_buffer *buffer);
const ns_mse_frame *ns_mse_buffer_frame(const ns_mse_buffer *buffer,
                                        guint index);
gssize         ns_mse_buffer_frame_at(const ns_mse_buffer *buffer, double t);
gssize         ns_mse_buffer_frame_from(const ns_mse_buffer *buffer, double t);
gssize         ns_mse_buffer_keyframe_at_or_before(const ns_mse_buffer *buffer,
                                                   gssize index);
double         ns_mse_buffer_end_of_range(const ns_mse_buffer *buffer, double t);
guint          ns_mse_buffer_generation(const ns_mse_buffer *buffer);

G_END_DECLS

#endif
