/* audio/stub.c: Audio interface fallback for builds without SDL2.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "audio.h"

#include <string.h>

#include <glib.h>

struct NsAudioContext {
    int unused;
};

void ns_audio_set_silent(gboolean silent) { (void)silent; }
NsAudioContext *ns_audio_context_new(const char *document_url)
{ (void)document_url; return g_new0(NsAudioContext, 1); }
void ns_audio_context_open(NsAudioContext *context, const char *token,
                           const char *url)
{ (void)context; (void)token; (void)url; }
void ns_audio_context_open_bytes(NsAudioContext *context, const char *token,
                                 GBytes *bytes, gboolean reload)
{ (void)context; (void)token; (void)bytes; (void)reload; }
void ns_audio_context_play(NsAudioContext *context, const char *token)
{ (void)context; (void)token; }
void ns_audio_context_pause(NsAudioContext *context, const char *token)
{ (void)context; (void)token; }
void ns_audio_context_seek(NsAudioContext *context, const char *token,
                           double seconds)
{ (void)context; (void)token; (void)seconds; }
void ns_audio_context_set_volume(NsAudioContext *context, const char *token,
                                 double volume)
{ (void)context; (void)token; (void)volume; }
void ns_audio_context_set_loop(NsAudioContext *context, const char *token,
                               gboolean loop)
{ (void)context; (void)token; (void)loop; }
void ns_audio_context_close(NsAudioContext *context, const char *token)
{ (void)context; (void)token; }
gboolean ns_audio_context_status(NsAudioContext *context, const char *token,
                                 NsAudioStatus *out)
{
    (void)context;
    (void)token;
    memset(out, 0, sizeof *out);
    out->state = NS_AUDIO_PLAYER_FAILED;
    out->error = NS_AUDIO_ERROR_NO_DEVICE;
    return TRUE;
}
void ns_audio_context_reset(NsAudioContext *context) { (void)context; }
void ns_audio_context_destroy(NsAudioContext *context) { g_free(context); }
void ns_audio_shutdown(void) {}
