/* audio/stub.c: Audio interface fallback for builds without SDL2.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "audio.h"

#include <glib.h>

struct NsAudioContext {
    int unused;
};

NsAudioContext *ns_audio_context_new(void) { return g_new0(NsAudioContext, 1); }
void ns_audio_context_dispatch(NsAudioContext *context, const char *command)
{ (void)context; (void)command; }
void ns_audio_context_dispatch_blob(NsAudioContext *context, const char *token,
                                    GBytes *bytes, gboolean reload)
{ (void)context; (void)token; (void)bytes; (void)reload; }
void ns_audio_context_set_document(NsAudioContext *context, const char *url)
{ (void)context; (void)url; }
void ns_audio_context_reset(NsAudioContext *context) { (void)context; }
void ns_audio_context_destroy(NsAudioContext *context) { g_free(context); }
void ns_audio_shutdown(void) {}
