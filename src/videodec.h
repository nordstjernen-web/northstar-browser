/* Northstar — the video decoders behind Media Source Extensions, chosen by codec.
 * Copyright 2026 Gabriel Ferreira
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_VIDEODEC_H
#define NS_VIDEODEC_H

#include <glib.h>

#include "texture.h"

G_BEGIN_DECLS

typedef struct ns_video_decoder ns_video_decoder;

gboolean          ns_video_decoder_supports(const char *codec);
ns_video_decoder *ns_video_decoder_new(const char *codec);
void              ns_video_decoder_free(ns_video_decoder *decoder);
void              ns_video_decoder_flush(ns_video_decoder *decoder);
ns_texture       *ns_video_decoder_decode(ns_video_decoder *decoder,
                                          GBytes *sample, gint64 timestamp,
                                          gboolean want_texture);

G_END_DECLS

#endif
