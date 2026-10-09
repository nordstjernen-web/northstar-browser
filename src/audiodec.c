/* Northstar — the audio decoders behind Media Source Extensions, chosen by codec.
 * Copyright 2026 Gabriel Ferreira
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "audiodec.h"

#include <string.h>

#include "audio/aac.h"

#ifdef NS_HAVE_OPUS
#include <opus.h>
#endif

enum {
    NS_OPUS_RATE = 48000,
};

struct ns_audio_decoder {
    ns_aac_decoder *aac;
#ifdef NS_HAVE_OPUS
    OpusDecoder    *opus;
#endif
    int             channels;
    int             pre_skip;
    int             skip_left;
};

gboolean
ns_audio_decoder_supports(const char *codec)
{
    if (!codec) return FALSE;
    if (strcmp(codec, "mp4a") == 0) return TRUE;
#ifdef NS_HAVE_OPUS
    if (strcmp(codec, "Opus") == 0) return TRUE;
#endif
    return FALSE;
}

#ifdef NS_HAVE_OPUS
static ns_audio_decoder *
opus_decoder_from_head(GBytes *config, int channels)
{
    int pre_skip = 0;
    if (config) {
        gsize len = 0;
        const guint8 *head = g_bytes_get_data(config, &len);
        if (len >= 19 && memcmp(head, "OpusHead", 8) == 0) {
            channels = head[9];
            pre_skip = head[10] | (head[11] << 8);
        }
    }
    if (channels < 1 || channels > 2) return NULL;
    int error = 0;
    OpusDecoder *opus = opus_decoder_create(NS_OPUS_RATE, channels, &error);
    if (!opus || error != OPUS_OK) return NULL;
    ns_audio_decoder *decoder = g_new0(ns_audio_decoder, 1);
    decoder->opus = opus;
    decoder->channels = channels;
    decoder->pre_skip = pre_skip;
    decoder->skip_left = pre_skip;
    return decoder;
}
#endif

ns_audio_decoder *
ns_audio_decoder_new(const char *codec, GBytes *config, int channels,
                     int sample_rate)
{
    (void)sample_rate;
    if (!ns_audio_decoder_supports(codec)) return NULL;
#ifdef NS_HAVE_OPUS
    if (strcmp(codec, "Opus") == 0) return opus_decoder_from_head(config, channels);
#endif
    if (!config) return NULL;
    gsize len = 0;
    const guint8 *asc = g_bytes_get_data(config, &len);
    ns_aac_decoder *aac = ns_aac_decoder_new(asc, len);
    if (!aac) return NULL;
    ns_audio_decoder *decoder = g_new0(ns_audio_decoder, 1);
    decoder->aac = aac;
    decoder->channels = ns_aac_decoder_channels(aac);
    return decoder;
}

void
ns_audio_decoder_free(ns_audio_decoder *decoder)
{
    if (!decoder) return;
    ns_aac_decoder_free(decoder->aac);
#ifdef NS_HAVE_OPUS
    if (decoder->opus) opus_decoder_destroy(decoder->opus);
#endif
    g_free(decoder);
}

int
ns_audio_decoder_sample_rate(const ns_audio_decoder *decoder)
{
    if (!decoder) return 0;
    if (decoder->aac) return ns_aac_decoder_sample_rate(decoder->aac);
    return NS_OPUS_RATE;
}

int
ns_audio_decoder_channels(const ns_audio_decoder *decoder)
{
    return decoder ? decoder->channels : 0;
}

void
ns_audio_decoder_reset(ns_audio_decoder *decoder, gboolean from_start)
{
    if (!decoder) return;
    if (decoder->aac) ns_aac_decoder_reset(decoder->aac);
#ifdef NS_HAVE_OPUS
    if (decoder->opus) opus_decoder_ctl(decoder->opus, OPUS_RESET_STATE);
#endif
    decoder->skip_left = from_start ? decoder->pre_skip : 0;
}

int
ns_audio_decoder_decode(ns_audio_decoder *decoder, const guint8 *data,
                        gsize len, float *out, int out_frames)
{
    if (!decoder || !data || len == 0) return -1;
    if (decoder->aac)
        return ns_aac_decode_frame(decoder->aac, data, len, out, out_frames);
#ifdef NS_HAVE_OPUS
    if (decoder->opus && len <= G_MAXINT32) {
        int frames = opus_decode_float(decoder->opus, data, (opus_int32)len,
                                       out, out_frames, 0);
        if (frames <= 0) return frames < 0 ? -1 : 0;
        int skip = MIN(decoder->skip_left, frames);
        if (skip > 0) {
            memmove(out, out + skip * decoder->channels,
                    (size_t)(frames - skip) * (size_t)decoder->channels * sizeof(float));
            decoder->skip_left -= skip;
            frames -= skip;
        }
        return frames;
    }
#endif
    return -1;
}
