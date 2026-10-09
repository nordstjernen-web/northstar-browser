/* Northstar — MP4 (ISO BMFF) demuxing: fragmented streams for Media Source Extensions, sample tables for whole files.
 * Copyright 2026 Gabriel Ferreira
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_MP4_H
#define NS_MP4_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct ns_mp4_demuxer ns_mp4_demuxer;

typedef struct {
    guint32  id;
    char     handler[5];
    char     codec[5];
    guint32  timescale;
    int      width;
    int      height;
    int      channels;
    int      sample_rate;
    guint8   object_type_indication;
    GBytes  *codec_config;
    GBytes  *config_obus;
    gint64   edit_media_time;
} ns_mp4_track;

typedef struct {
    guint32   track_id;
    gint64    dts;
    gint64    pts;
    guint32   duration;
    gboolean  keyframe;
    GBytes   *data;
} ns_mp4_sample;

ns_mp4_demuxer *ns_mp4_demuxer_new(void);
void            ns_mp4_demuxer_free(ns_mp4_demuxer *d);
gboolean        ns_mp4_demuxer_append(ns_mp4_demuxer *d, const guint8 *data,
                                      gsize len);
gboolean        ns_mp4_demuxer_has_init(const ns_mp4_demuxer *d);
gboolean        ns_mp4_demuxer_has_error(const ns_mp4_demuxer *d);
guint           ns_mp4_demuxer_n_tracks(const ns_mp4_demuxer *d);
const ns_mp4_track *ns_mp4_demuxer_track(const ns_mp4_demuxer *d,
                                         guint index);
const ns_mp4_track *ns_mp4_demuxer_track_by_id(const ns_mp4_demuxer *d,
                                               guint32 id);
gboolean        ns_mp4_demuxer_pop_sample(ns_mp4_demuxer *d,
                                          ns_mp4_sample *out);
void            ns_mp4_demuxer_reset_parser(ns_mp4_demuxer *d);

#define NS_MP4_INDEX_END G_MAXUINT64

gboolean        ns_mp4_demuxer_has_sample_index(const ns_mp4_demuxer *d);
double          ns_mp4_demuxer_index_duration(const ns_mp4_demuxer *d);
guint64         ns_mp4_demuxer_index_seek(const ns_mp4_demuxer *d,
                                          double seconds);
guint64         ns_mp4_demuxer_index_extract(ns_mp4_demuxer *d,
                                             guint64 position,
                                             const guint8 *data, gsize len);

void            ns_mp4_sample_clear(ns_mp4_sample *sample);
double          ns_mp4_time_to_seconds(gint64 time, guint32 timescale);

G_END_DECLS

#endif
