/* audio/audio.h: In-process audio playback interface.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NS_AUDIO_H
#define NS_AUDIO_H

#include <glib.h>

typedef struct NsAudioContext NsAudioContext;

NsAudioContext *ns_audio_context_new(void);
void ns_audio_context_dispatch(NsAudioContext *context, const char *command);
void ns_audio_context_dispatch_blob(NsAudioContext *context,
                                    const char *token, GBytes *bytes,
                                    gboolean reload);
void ns_audio_context_reset(NsAudioContext *context);
void ns_audio_context_destroy(NsAudioContext *context);
void ns_audio_shutdown(void);

#endif
