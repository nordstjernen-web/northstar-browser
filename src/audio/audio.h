/* audio/audio.h: In-process audio playback interface.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NS_AUDIO_H
#define NS_AUDIO_H

#include <glib.h>

typedef struct NsAudioContext NsAudioContext;

typedef enum {
    NS_AUDIO_PLAYER_ABSENT,
    NS_AUDIO_PLAYER_LOADING,
    NS_AUDIO_PLAYER_READY,
    NS_AUDIO_PLAYER_FAILED,
} NsAudioPlayerState;

typedef enum {
    NS_AUDIO_ERROR_NONE,
    NS_AUDIO_ERROR_FETCH,
    NS_AUDIO_ERROR_DECODE,
    NS_AUDIO_ERROR_NO_DEVICE,
    NS_AUDIO_ERROR_TOO_MANY,
} NsAudioError;

typedef struct {
    NsAudioPlayerState state;
    NsAudioError       error;
    double             duration;
    double             position;
    gboolean           playing;
    gboolean           ended;
} NsAudioStatus;

void ns_audio_set_silent(gboolean silent);

NsAudioContext *ns_audio_context_new(const char *document_url);
void ns_audio_context_open(NsAudioContext *context, const char *token,
                           const char *url);
void ns_audio_context_open_bytes(NsAudioContext *context, const char *token,
                                 GBytes *bytes, gboolean reload);
void ns_audio_context_play(NsAudioContext *context, const char *token);
void ns_audio_context_pause(NsAudioContext *context, const char *token);
void ns_audio_context_seek(NsAudioContext *context, const char *token,
                           double seconds);
void ns_audio_context_set_volume(NsAudioContext *context, const char *token,
                                 double volume);
void ns_audio_context_set_loop(NsAudioContext *context, const char *token,
                               gboolean loop);
void ns_audio_context_close(NsAudioContext *context, const char *token);
gboolean ns_audio_context_status(NsAudioContext *context, const char *token,
                                 NsAudioStatus *out);
void ns_audio_context_reset(NsAudioContext *context);
void ns_audio_context_destroy(NsAudioContext *context);
void ns_audio_shutdown(void);

#endif
