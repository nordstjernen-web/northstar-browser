/* Northstar — maps changed elements and images to the page rectangles they repaint.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_DAMAGE_H
#define NS_DAMAGE_H

#include <glib.h>

#include "anim.h"
#include "js.h"
#include "layout.h"

G_BEGIN_DECLS

typedef struct ns_damage_rect {
    double x, y, w, h;
    gboolean fixed;
} ns_damage_rect;

typedef struct ns_paint_hazards {
    gboolean three_d;
    gboolean sticky;
    gboolean fixed_background;
    GArray  *fixed_bands;
} ns_paint_hazards;

gboolean ns_damage_resolve(const ns_box *root, ns_js *js, ns_anim *anim,
                           GHashTable *nodes, GHashTable *images,
                           GArray *out_rects);

void ns_paint_hazards_scan(const ns_box *root, ns_anim *anim,
                           ns_paint_hazards *out);
void ns_paint_hazards_clear(ns_paint_hazards *hazards);

G_END_DECLS

#endif
