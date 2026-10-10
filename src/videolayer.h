/* Northstar — a playing video's picture, presented apart from page painting.
 * Copyright 2026 Gabriel Ferreira
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_VIDEOLAYER_H
#define NS_VIDEOLAYER_H

#include <glib.h>

#include "texture.h"
#include "videoworker.h"

G_BEGIN_DECLS

/* The current picture of an MSE video and the clock that picks it, shared
 * between the engine thread and the view. While the view composites the
 * video (the page frame has a transparent hole where the picture goes),
 * the view takes decoded frames for its own frame clock, so script,
 * relayouts and paints on the engine thread do not hold up the picture.
 * Otherwise the engine takes them on its animation ticks. */
typedef struct ns_video_layer ns_video_layer;

typedef struct {
    gboolean paused;
    gboolean loop;
    gint64   start_us;
    int      paused_ms;
    int      total_ms;
    guint    seek_gen;
} ns_video_clock;

/* Where a composited picture goes: the fitted picture rectangle in the
 * page's device pixels (viewport pixels plus the scroll offset). */
typedef struct {
    ns_video_layer *layer;
    double          x, y, w, h;
} ns_video_layer_rect;

void ns_video_layer_rect_clear(gpointer data);

ns_video_layer *ns_video_layer_new(void);
ns_video_layer *ns_video_layer_ref(ns_video_layer *layer);
void            ns_video_layer_unref(ns_video_layer *layer);

void     ns_video_layer_attach(ns_video_layer *layer, ns_video_worker *worker);
void     ns_video_layer_detach(ns_video_layer *layer);
gboolean ns_video_layer_detached(ns_video_layer *layer);

void     ns_video_layer_set_clock(ns_video_layer *layer,
                                  const ns_video_clock *clock);
double   ns_video_layer_position(ns_video_layer *layer, gint64 now_us);

gboolean     ns_video_layer_present(ns_video_layer *layer, double t);
gboolean     ns_video_layer_present_now(ns_video_layer *layer, gint64 now_us);
ns_texture  *ns_video_layer_texture(ns_video_layer *layer, double *shown_pts);
/* The time of the picture shown now, -1 before the first one since the
 * last seek. */
double       ns_video_layer_shown_pts(ns_video_layer *layer);

void     ns_video_layer_set_composited(ns_video_layer *layer,
                                       gboolean composited);
gboolean ns_video_layer_composited(ns_video_layer *layer, gint64 now_us);

G_END_DECLS

#endif
