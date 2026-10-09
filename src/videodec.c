/* Northstar — the video decoders behind Media Source Extensions, chosen by codec.
 * Copyright 2026 Gabriel Ferreira
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "videodec.h"

#include <string.h>

#include "av1.h"
#include "vp9.h"

struct ns_video_decoder {
    ns_av1_decoder *av1;
    ns_vp9_decoder *vp9;
};

gboolean
ns_video_decoder_supports(const char *codec)
{
    if (!codec) return FALSE;
    if (strcmp(codec, "av01") == 0) return ns_av1_available();
    if (strcmp(codec, "vp09") == 0) return ns_vp9_available();
    return FALSE;
}

ns_video_decoder *
ns_video_decoder_new(const char *codec)
{
    if (!ns_video_decoder_supports(codec)) return NULL;
    ns_video_decoder *decoder = g_new0(ns_video_decoder, 1);
    if (strcmp(codec, "av01") == 0) decoder->av1 = ns_av1_decoder_new();
    else decoder->vp9 = ns_vp9_decoder_new();
    if (!decoder->av1 && !decoder->vp9) {
        g_free(decoder);
        return NULL;
    }
    return decoder;
}

void
ns_video_decoder_free(ns_video_decoder *decoder)
{
    if (!decoder) return;
    ns_av1_decoder_free(decoder->av1);
    ns_vp9_decoder_free(decoder->vp9);
    g_free(decoder);
}

void
ns_video_decoder_flush(ns_video_decoder *decoder)
{
    if (!decoder) return;
    ns_av1_decoder_flush(decoder->av1);
    ns_vp9_decoder_flush(decoder->vp9);
}

ns_texture *
ns_video_decoder_decode(ns_video_decoder *decoder, GBytes *sample,
                        gint64 timestamp, gboolean want_texture)
{
    if (!decoder) return NULL;
    if (decoder->av1)
        return ns_av1_decoder_decode(decoder->av1, sample, timestamp, want_texture);
    return ns_vp9_decoder_decode(decoder->vp9, sample, want_texture);
}
