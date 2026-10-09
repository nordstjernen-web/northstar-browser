/* Northstar — MPEG-1 video decoding for <video>, streamed over the vendored pl_mpeg.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "video.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mse.h"
#include "videodec.h"
#include "pl_mpeg.h"

enum {
    NS_VIDEO_MAX_DIMENSION = 4096,
};

static const double NS_VIDEO_FORWARD_DECODE_S = 2.0;
static const int NS_VIDEO_OPEN_ENDED_MS = 24 * 3600 * 1000;

struct ns_video_stream {
    GBytes      *bytes;
    plm_t       *program;
    plm_video_t *elementary;
    int          width;
    int          height;
    double       frame_s;
    int          duration_ms;
    gboolean     has_audio;
    ns_texture  *texture;
    double       shown_time;
    ns_mse_source    *mse;
    ns_video_decoder *decoder;
    double          decoded_pts;
    gboolean        decoded_valid;
    GArray         *early;
};

typedef struct {
    double      pts;
    ns_texture *texture;
} mse_early_picture;

static gboolean
bytes_are_program_stream(const guchar *data, gsize len)
{
    return data && len >= 4 && data[0] == 0x00 && data[1] == 0x00 &&
           data[2] == 0x01 && data[3] == 0xBA;
}

static gboolean
bytes_are_elementary_stream(const guchar *data, gsize len)
{
    return data && len >= 4 && data[0] == 0x00 && data[1] == 0x00 &&
           data[2] == 0x01 && data[3] == 0xB3;
}

gboolean
ns_video_bytes_are_mpeg1(const guchar *data, gsize len)
{
    return bytes_are_program_stream(data, len) ||
           bytes_are_elementary_stream(data, len);
}

static guint
count_pictures(const guchar *data, gsize len)
{
    guint pictures = 0;
    for (gsize i = 0; i + 3 < len; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01 &&
            data[i + 3] == 0x00)
            pictures++;
    }
    return pictures;
}

static gboolean
stream_open(ns_video_stream *s, double *framerate, double *duration_s)
{
    gsize len = 0;
    const guchar *data = g_bytes_get_data(s->bytes, &len);
    if (bytes_are_program_stream(data, len)) {
        s->program = plm_create_with_memory((uint8_t *)data, len, 0);
        if (!s->program) return FALSE;
        plm_set_video_enabled(s->program, 1);
        plm_set_audio_enabled(s->program, 0);
        s->width = plm_get_width(s->program);
        s->height = plm_get_height(s->program);
        s->has_audio = plm_get_num_audio_streams(s->program) > 0;
        *framerate = plm_get_framerate(s->program);
        *duration_s = plm_get_duration(s->program);
        return TRUE;
    }
    plm_buffer_t *buffer =
        plm_buffer_create_with_memory((uint8_t *)data, len, 0);
    if (!buffer) return FALSE;
    s->elementary = plm_video_create_with_buffer(buffer, TRUE);
    if (!s->elementary) {
        plm_buffer_destroy(buffer);
        return FALSE;
    }
    s->width = plm_video_get_width(s->elementary);
    s->height = plm_video_get_height(s->elementary);
    *framerate = plm_video_get_framerate(s->elementary);
    *duration_s = *framerate > 0.0
        ? count_pictures(data, len) / *framerate : 0.0;
    return TRUE;
}

static plm_frame_t *
stream_next(ns_video_stream *s)
{
    if (s->program) return plm_decode_video(s->program);
    return plm_video_decode(s->elementary);
}

static plm_frame_t *
stream_decode_until(ns_video_stream *s, double t)
{
    plm_frame_t *last = NULL;
    plm_frame_t *frame;
    while ((frame = stream_next(s)) != NULL) {
        last = frame;
        if (frame->time + s->frame_s > t) break;
    }
    return last;
}

static plm_frame_t *
stream_seek(ns_video_stream *s, double t)
{
    if (s->program) {
        plm_frame_t *frame = plm_seek_frame(s->program, t, TRUE);
        if (frame) return frame;
        plm_rewind(s->program);
        return stream_decode_until(s, t);
    }
    plm_video_rewind(s->elementary);
    return stream_decode_until(s, t);
}

static gboolean
stream_present(ns_video_stream *s, plm_frame_t *frame)
{
    gsize stride = (gsize)s->width * 4;
    gsize frame_bytes = stride * (gsize)s->height;
    guint8 *pixels = g_try_malloc(frame_bytes);
    if (!pixels) return FALSE;
    memset(pixels, 0xFF, frame_bytes);
    plm_frame_to_bgra(frame, pixels, (int)stride);
    GBytes *bytes = g_bytes_new_take(pixels, frame_bytes);
    ns_texture *texture = ns_texture_new(s->width, s->height,
                                         NS_TEXTURE_BGRA_PREMULTIPLIED,
                                         bytes, stride);
    g_bytes_unref(bytes);
    if (!texture) return FALSE;
    ns_texture_unref(s->texture);
    s->texture = texture;
    s->shown_time = frame->time;
    return TRUE;
}

ns_video_stream *
ns_video_stream_new(const guchar *data, gsize len)
{
    if (!ns_video_bytes_are_mpeg1(data, len)) return NULL;
    ns_video_stream *s = g_new0(ns_video_stream, 1);
    s->bytes = g_bytes_new(data, len);
    double framerate = 0.0;
    double duration_s = 0.0;
    if (!stream_open(s, &framerate, &duration_s) ||
        s->width <= 0 || s->height <= 0 ||
        s->width > NS_VIDEO_MAX_DIMENSION ||
        s->height > NS_VIDEO_MAX_DIMENSION) {
        ns_video_stream_free(s);
        return NULL;
    }
    s->frame_s = framerate > 0.0 ? 1.0 / framerate : 0.04;
    plm_frame_t *first = stream_next(s);
    if (!first || !stream_present(s, first)) {
        ns_video_stream_free(s);
        return NULL;
    }
    if (duration_s < s->frame_s) duration_s = s->frame_s;
    s->duration_ms = (int)CLAMP(duration_s * 1000.0 + 0.5, 1, G_MAXINT / 2);
    return s;
}

static void
early_picture_clear(gpointer data)
{
    mse_early_picture *picture = data;
    ns_texture_unref(picture->texture);
}

ns_video_stream *
ns_video_stream_new_mse(ns_mse_source *source)
{
    if (!source) return NULL;
    ns_video_stream *s = g_new0(ns_video_stream, 1);
    s->mse = ns_mse_source_ref(source);
    s->shown_time = -1.0;
    s->early = g_array_new(FALSE, FALSE, sizeof(mse_early_picture));
    g_array_set_clear_func(s->early, early_picture_clear);
    return s;
}

static gboolean
mse_present(ns_video_stream *s, ns_texture *texture, double pts)
{
    if (!texture) return FALSE;
    ns_texture_unref(s->texture);
    s->texture = texture;
    s->shown_time = pts;
    s->width = ns_texture_get_width(texture);
    s->height = ns_texture_get_height(texture);
    return TRUE;
}

static ns_texture *
mse_take_early(ns_video_stream *s, double pts)
{
    ns_texture *found = NULL;
    for (guint i = 0; i < s->early->len;) {
        mse_early_picture *picture = &g_array_index(s->early, mse_early_picture, i);
        if (picture->pts > pts + 1e-6) {
            i++;
            continue;
        }
        if (!found && fabs(picture->pts - pts) < 1e-6) {
            found = picture->texture;
            picture->texture = NULL;
        }
        g_array_remove_index(s->early, i);
    }
    return found;
}

static ns_texture *
mse_decode_frame(ns_video_stream *s, const ns_mse_frame *frame, gboolean want)
{
    gint64 stamp = (gint64)llround(frame->pts * 1e6);
    ns_texture *texture = ns_video_decoder_decode(s->decoder, frame->data,
                                                  frame->config, stamp, want);
    s->decoded_pts = frame->pts;
    s->decoded_valid = TRUE;
    return texture;
}

static gssize
mse_decoded_index(ns_video_stream *s, ns_mse_buffer *buffer)
{
    if (!s->decoded_valid) return -1;
    gssize last = ns_mse_buffer_frame_at(buffer, s->decoded_pts);
    const ns_mse_frame *at = last >= 0 ? ns_mse_buffer_frame(buffer, (guint)last) : NULL;
    return at && fabs(at->pts - s->decoded_pts) < 1e-6 ? last : -1;
}

static gboolean
mse_show(ns_video_stream *s, double t)
{
    ns_mse_buffer *buffer = ns_mse_source_track_buffer(s->mse, NS_MSE_TRACK_VIDEO);
    const ns_mp4_track *track = ns_mse_buffer_track(buffer);
    if (!track) return FALSE;
    if (!s->decoder) {
        s->decoder = ns_video_decoder_new(track->codec);
        s->decoded_valid = FALSE;
        if (!s->decoder) return FALSE;
    }
    gssize target = ns_mse_buffer_frame_at(buffer, t);
    if (target < 0) return FALSE;
    const ns_mse_frame *goal = ns_mse_buffer_frame(buffer, (guint)target);
    if (s->texture && fabs(goal->pts - s->shown_time) < 1e-6) return FALSE;
    ns_texture *texture = mse_take_early(s, goal->pts);
    if (texture) return mse_present(s, texture, goal->pts);
    gssize start = -1;
    gssize last = mse_decoded_index(s, buffer);
    if (last >= 0 && last < target && goal->pts > s->shown_time &&
        goal->pts - s->shown_time < NS_VIDEO_FORWARD_DECODE_S)
        start = last + 1;
    if (start < 0) {
        start = ns_mse_buffer_keyframe_at_or_before(buffer, target);
        if (start < 0) return FALSE;
        ns_video_decoder_flush(s->decoder);
        g_array_set_size(s->early, 0);
    }
    for (gssize i = start; i <= target; i++) {
        const ns_mse_frame *frame = ns_mse_buffer_frame(buffer, (guint)i);
        gboolean early = i < target && frame->pts > goal->pts;
        ns_texture *decoded = mse_decode_frame(s, frame, i == target || early);
        if (early && decoded) {
            mse_early_picture picture = { frame->pts, decoded };
            g_array_append_val(s->early, picture);
        } else if (decoded) {
            if (texture) ns_texture_unref(texture);
            texture = decoded;
        }
    }
    return mse_present(s, texture, goal->pts);
}

void
ns_video_stream_free(ns_video_stream *s)
{
    if (!s) return;
    ns_video_decoder_free(s->decoder);
    if (s->early) g_array_free(s->early, TRUE);
    ns_mse_source_unref(s->mse);
    if (s->program) plm_destroy(s->program);
    if (s->elementary) plm_video_destroy(s->elementary);
    ns_texture_unref(s->texture);
    if (s->bytes) g_bytes_unref(s->bytes);
    g_free(s);
}

int
ns_video_stream_width(const ns_video_stream *s)
{
    return s ? s->width : 0;
}

int
ns_video_stream_height(const ns_video_stream *s)
{
    return s ? s->height : 0;
}

int
ns_video_stream_duration_ms(const ns_video_stream *s)
{
    if (s && s->mse) {
        double d = ns_mse_source_duration(s->mse);
        if (!(d > 0) || isinf(d) || d * 1000.0 > NS_VIDEO_OPEN_ENDED_MS)
            return NS_VIDEO_OPEN_ENDED_MS;
        return (int)(d * 1000.0 + 0.5);
    }
    return s ? s->duration_ms : 0;
}

gboolean
ns_video_stream_has_audio(const ns_video_stream *s)
{
    return s && s->has_audio;
}

GBytes *
ns_video_stream_bytes(const ns_video_stream *s)
{
    return s && s->bytes ? g_bytes_ref(s->bytes) : NULL;
}

gsize
ns_video_stream_memory(const ns_video_stream *s)
{
    if (!s) return 0;
    gsize encoded = s->bytes ? g_bytes_get_size(s->bytes) : 0;
    return encoded + (gsize)s->width * (gsize)s->height * 4;
}

gboolean
ns_video_stream_is_mse(const ns_video_stream *s)
{
    return s && s->mse;
}

ns_texture *
ns_video_stream_texture(const ns_video_stream *s)
{
    return s ? s->texture : NULL;
}

gboolean
ns_video_stream_show(ns_video_stream *s, int phase_ms)
{
    if (!s) return FALSE;
    double t = phase_ms / 1000.0;
    if (s->mse) return mse_show(s, t);
    if (t >= s->shown_time && t < s->shown_time + s->frame_s) return FALSE;
    gboolean forward = t > s->shown_time &&
                       t - s->shown_time < NS_VIDEO_FORWARD_DECODE_S;
    plm_frame_t *frame = forward ? stream_decode_until(s, t)
                                 : stream_seek(s, t);
    return frame && stream_present(s, frame);
}
