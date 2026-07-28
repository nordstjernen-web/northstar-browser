/* Northstar — MPEG-1 video decoding for <video>, over the vendored pl_mpeg.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "video.h"

#include <stdio.h>
#include <string.h>

#include "image.h"
#include "pl_mpeg.h"

enum {
    NS_VIDEO_MAX_FRAMES    = 4096,
    NS_VIDEO_MAX_DIMENSION = 4096,
};

static const gsize NS_VIDEO_MAX_TOTAL_BYTES = (gsize)256 * 1024 * 1024;

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

gboolean
ns_video_supports_mime(const char *mime)
{
    if (!mime) return FALSE;
    return g_ascii_strcasecmp(mime, "video/mpeg") == 0 ||
           g_ascii_strcasecmp(mime, "video/mpg") == 0 ||
           g_ascii_strcasecmp(mime, "video/x-mpeg") == 0 ||
           g_ascii_strcasecmp(mime, "video/mpeg-1") == 0;
}

typedef struct {
    plm_t       *program;
    plm_video_t *elementary;
} ns_mpeg1_reader;

static void
ns_mpeg1_signal_end(plm_buffer_t *buffer, void *user)
{
    (void)user;
    plm_buffer_signal_end(buffer);
}

static void
ns_mpeg1_reader_close(ns_mpeg1_reader *reader)
{
    if (reader->program) plm_destroy(reader->program);
    if (reader->elementary) plm_video_destroy(reader->elementary);
}

static gboolean
ns_mpeg1_reader_open(ns_mpeg1_reader *reader, const guchar *data, gsize len,
                     int *out_w, int *out_h, double *out_framerate)
{
    memset(reader, 0, sizeof *reader);
    if (bytes_are_program_stream(data, len)) {
        reader->program = plm_create_with_memory((uint8_t *)data, len, 0);
        if (!reader->program) return FALSE;
        plm_set_video_enabled(reader->program, 1);
        plm_set_audio_enabled(reader->program, 0);
        *out_w = plm_get_width(reader->program);
        *out_h = plm_get_height(reader->program);
        *out_framerate = plm_get_framerate(reader->program);
        return TRUE;
    }
    plm_buffer_t *buffer = plm_buffer_create_for_appending(len);
    if (!buffer) return FALSE;
    plm_buffer_write(buffer, (uint8_t *)data, len);
    plm_buffer_set_load_callback(buffer, ns_mpeg1_signal_end, NULL);
    reader->elementary = plm_video_create_with_buffer(buffer, TRUE);
    if (!reader->elementary) {
        plm_buffer_destroy(buffer);
        return FALSE;
    }
    *out_w = plm_video_get_width(reader->elementary);
    *out_h = plm_video_get_height(reader->elementary);
    *out_framerate = plm_video_get_framerate(reader->elementary);
    return TRUE;
}

static plm_frame_t *
ns_mpeg1_reader_next(ns_mpeg1_reader *reader)
{
    if (reader->program) return plm_decode_video(reader->program);
    return plm_video_decode(reader->elementary);
}

GArray *
ns_video_decode_mpeg1_to_pixels(const guchar *data, gsize len,
                                int *out_w, int *out_h)
{
    if (!ns_video_bytes_are_mpeg1(data, len)) return NULL;

    ns_mpeg1_reader reader;
    int w = 0, h = 0;
    double framerate = 0.0;
    if (!ns_mpeg1_reader_open(&reader, data, len, &w, &h, &framerate))
        return NULL;
    if (w <= 0 || h <= 0 ||
        w > NS_VIDEO_MAX_DIMENSION || h > NS_VIDEO_MAX_DIMENSION) {
        ns_mpeg1_reader_close(&reader);
        return NULL;
    }

    int delay_ms = framerate > 0.0 ? (int)(1000.0 / framerate + 0.5) : 40;
    if (delay_ms < 1) delay_ms = 1;

    gsize stride = (gsize)w * 4;
    gsize frame_bytes = stride * (gsize)h;

    GArray *frames = g_array_new(FALSE, FALSE, sizeof(ns_image_pixel_frame));
    g_array_set_clear_func(frames, ns_image_pixel_frame_clear);
    gsize total_bytes = 0;

    while (frames->len < NS_VIDEO_MAX_FRAMES &&
           total_bytes + frame_bytes <= NS_VIDEO_MAX_TOTAL_BYTES) {
        plm_frame_t *frame = ns_mpeg1_reader_next(&reader);
        if (!frame) break;
        guint8 *pixels = g_try_malloc(frame_bytes);
        if (!pixels) break;
        memset(pixels, 0xFF, frame_bytes);
        plm_frame_to_bgra(frame, pixels, (int)stride);
        ns_image_pixel_frame pf = {
            .pixels = pixels,
            .pixels_len = frame_bytes,
            .stride = stride,
            .format = NS_TEXTURE_BGRA_PREMULTIPLIED,
            .width = w,
            .height = h,
            .delay_ms = delay_ms,
        };
        g_array_append_val(frames, pf);
        total_bytes += frame_bytes;
    }

    ns_mpeg1_reader_close(&reader);

    if (frames->len == 0) {
        g_array_free(frames, TRUE);
        return NULL;
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return frames;
}
