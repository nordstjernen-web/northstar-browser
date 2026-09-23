/* Northstar — maps changed elements and images to the page rectangles they repaint.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "damage.h"

#include <math.h>
#include <string.h>

#include "css.h"
#include "dom.h"
#include "image.h"

#define NS_DAMAGE_INFLATE 2.0
#define NS_DAMAGE_FIXED_INFLATE 16.0
#define NS_DAMAGE_EM_PX 64.0
#define NS_DAMAGE_PAINT_BOUND 1.0e7

typedef struct damage_walk {
    ns_js      *js;
    ns_anim    *anim;
    GHashTable *nodes;
    GHashTable *images;
    GHashTable *images_seen;
    GArray     *rects;
    gboolean    full;
} damage_walk;

static gboolean
style_keyword_is(const ns_style *s, ns_css_prop p, const char *kw)
{
    const char *v = ns_style_keyword(s, p);
    return v && strcmp(v, kw) == 0;
}

static gboolean
box_has_transform(const ns_box *b, ns_anim *anim, gboolean *three_d)
{
    const ns_style *s = b->style;
    const ns_css_transform *anim_tf =
        anim && b->dom ? ns_anim_get_transform(anim, b->dom) : NULL;
    if (!anim_tf &&
        !(s && (s->values[NS_CSS_TRANSFORM] || s->values[NS_CSS_TRANSLATE] ||
                s->values[NS_CSS_ROTATE] || s->values[NS_CSS_SCALE])))
        return FALSE;
    ns_css_transform tf;
    tf.n_ops = 0;
    ns_css_style_effective_transform(s, anim_tf, &tf);
    if (tf.n_ops > 0 && three_d && ns_css_transform_is_3d(&tf))
        *three_d = TRUE;
    return tf.n_ops > 0;
}

static gboolean
box_has_perspective(const ns_box *b)
{
    const ns_css_value *v = b->style ? b->style->values[NS_CSS_PERSPECTIVE]
                                     : NULL;
    return v && v->kind == NS_CSS_V_LENGTH && v->u.length.v > 0;
}

static gboolean
box_moves_at_paint(const ns_box *b, ns_anim *anim)
{
    if (b->fragment_context) return TRUE;
    if (!b->style) return FALSE;
    if (style_keyword_is(b->style, NS_CSS_POSITION, "sticky")) return TRUE;
    if (style_keyword_is(b->style, NS_CSS_TRANSFORM_STYLE, "preserve-3d"))
        return TRUE;
    if (box_has_perspective(b)) return TRUE;
    return box_has_transform(b, anim, NULL);
}

static gboolean
box_has_fixed_background(const ns_box *b)
{
    const ns_css_value *v =
        b->style ? b->style->values[NS_CSS_BACKGROUND_ATTACHMENT] : NULL;
    int n = ns_css_value_layer_count(v);
    for (int i = 0; i < n; i++) {
        const ns_css_value *layer = ns_css_value_layer(v, i);
        if (layer && layer->kind == NS_CSS_V_KEYWORD && layer->u.keyword &&
            strcmp(layer->u.keyword, "fixed") == 0)
            return TRUE;
    }
    return FALSE;
}

static gboolean
box_is_canvas_root(const ns_box *b)
{
    return !b->parent || ns_node_is_element_named(b->dom, "html") ||
           ns_node_is_element_named(b->dom, "body");
}

static gboolean
image_changed(damage_walk *w, gconstpointer img)
{
    if (!img || !g_hash_table_contains(w->images, img)) return FALSE;
    g_hash_table_add(w->images_seen, (gpointer)img);
    return TRUE;
}

static gboolean
box_uses_changed_image(damage_walk *w, const ns_box *b)
{
    gboolean used = FALSE;
    const ns_box_media *m = b->media;
    if (m) {
        used |= image_changed(w, m->image);
        used |= image_changed(w, m->bg_image);
        used |= image_changed(w, m->marker_image);
        used |= image_changed(w, m->border_image);
        used |= image_changed(w, m->video);
        if (m->bg_layer_images)
            for (guint i = 0; i < m->bg_layer_images->len; i++)
                used |= image_changed(w,
                                      g_ptr_array_index(m->bg_layer_images, i));
    }
    if (b->kind == NS_BOX_IMAGE && b->dom && w->js)
        used |= image_changed(w, ns_js_image_for_node(w->js, b->dom));
    if (b->attrs)
        for (guint i = 0; i < b->attrs->len; i++)
            used |= image_changed(
                w, g_array_index(b->attrs, ns_inline_attr, i).bg_image);
    return used;
}

static void
box_border_rect(const ns_box *b, double *x, double *y, double *w, double *h)
{
    *x = b->x + b->margin.left;
    *y = b->y + b->margin.top;
    *w = b->border.left + b->padding.left + b->content_width +
         b->padding.right + b->border.right;
    *h = b->border.top + b->padding.top + b->content_height +
         b->padding.bottom + b->border.bottom;
}

static void
damage_add_box(damage_walk *w, const ns_box *b, double dx, double dy,
               gboolean fixed)
{
    ns_damage_rect r;
    box_border_rect(b, &r.x, &r.y, &r.w, &r.h);
    if (!isfinite(r.x) || !isfinite(r.y) || !isfinite(r.w) || !isfinite(r.h)) {
        w->full = TRUE;
        return;
    }
    r.x += dx - NS_DAMAGE_INFLATE;
    r.y += dy - NS_DAMAGE_INFLATE;
    r.w += 2 * NS_DAMAGE_INFLATE;
    r.h += 2 * NS_DAMAGE_INFLATE;
    r.fixed = fixed;
    g_array_append_val(w->rects, r);
}

static void
damage_walk_box(damage_walk *w, const ns_box *b, double dx, double dy,
                gboolean fixed, gboolean moved)
{
    if (!b || w->full) return;
    gboolean in_fixed = fixed || ns_box_is_fixed(b);
    gboolean in_moved = moved || box_moves_at_paint(b, w->anim);
    gboolean hit = w->nodes && b->dom && g_hash_table_contains(w->nodes, b->dom);
    if (w->images && box_uses_changed_image(w, b)) {
        if (box_is_canvas_root(b)) {
            w->full = TRUE;
            return;
        }
        hit = TRUE;
    }
    if (hit) {
        if (in_moved) {
            w->full = TRUE;
            return;
        }
        damage_add_box(w, b, dx, dy, in_fixed);
    }
    double cdx = dx, cdy = dy;
    if (isfinite(b->scroll_x) && isfinite(b->scroll_y)) {
        cdx -= b->scroll_x;
        cdy -= b->scroll_y;
    }
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        damage_walk_box(w, c, cdx, cdy, in_fixed, in_moved);
    if (!b->inline_atomics) return;
    for (guint i = 0; i < b->inline_atomics->len; i++) {
        const ns_inline_atomic *a =
            &g_array_index(b->inline_atomics, ns_inline_atomic, i);
        if (!a->box) continue;
        double tx = b->x + a->owner_offset_x - a->box->x;
        double ty = b->y + a->owner_offset_y - a->box->y;
        if (!isfinite(tx) || !isfinite(ty)) {
            w->full = TRUE;
            return;
        }
        damage_walk_box(w, a->box, cdx + tx, cdy + ty, in_fixed, in_moved);
    }
}

gboolean
ns_damage_resolve(const ns_box *root, ns_js *js, ns_anim *anim,
                  GHashTable *nodes, GHashTable *images, GArray *out_rects)
{
    if (!root) return FALSE;
    gboolean have_nodes = nodes && g_hash_table_size(nodes) > 0;
    gboolean have_images = images && g_hash_table_size(images) > 0;
    if (!have_nodes && !have_images) return TRUE;
    damage_walk w = {
        .js = js,
        .anim = anim,
        .nodes = have_nodes ? nodes : NULL,
        .images = have_images ? images : NULL,
        .images_seen = g_hash_table_new(g_direct_hash, g_direct_equal),
        .rects = out_rects,
    };
    damage_walk_box(&w, root, 0, 0, FALSE, FALSE);
    if (have_images &&
        g_hash_table_size(w.images_seen) < g_hash_table_size(images))
        w.full = TRUE;
    g_hash_table_destroy(w.images_seen);
    return !w.full;
}

typedef struct paint_extent {
    double x0, y0, x1, y1;
    double overflow;
} paint_extent;

static double
style_shadow_overflow(const ns_style *s)
{
    const ns_css_value *v = s ? s->values[NS_CSS_BOX_SHADOW] : NULL;
    if (!v || v->kind != NS_CSS_V_SHADOW) return 0;
    double out = 0;
    const ns_css_shadow_list *sl = &v->u.shadow;
    for (int i = 0; i < sl->n; i++) {
        const ns_css_shadow *sh = &sl->s[i];
        if (sh->inset) continue;
        double rel = 0;
        for (int k = 0; k < 4; k++) rel += fabs(sh->em[k]) + fabs(sh->rem[k]);
        out = MAX(out, MAX(fabs(sh->x), fabs(sh->y)) + fabs(sh->spread) +
                       1.5 * fabs(sh->blur) + NS_DAMAGE_EM_PX * rel);
    }
    return out;
}

static void
subtree_extent(const ns_box *b, double dx, double dy, paint_extent *e)
{
    double x, y, w, h;
    box_border_rect(b, &x, &y, &w, &h);
    if (isfinite(x) && isfinite(y) && isfinite(w) && isfinite(h)) {
        e->x0 = MIN(e->x0, x + dx);
        e->y0 = MIN(e->y0, y + dy);
        e->x1 = MAX(e->x1, x + dx + w);
        e->y1 = MAX(e->y1, y + dy + h);
    }
    if (b->paint_bottom > b->paint_top &&
        fabs(b->paint_top) < NS_DAMAGE_PAINT_BOUND &&
        fabs(b->paint_bottom) < NS_DAMAGE_PAINT_BOUND) {
        e->y0 = MIN(e->y0, b->paint_top + dy);
        e->y1 = MAX(e->y1, b->paint_bottom + dy);
    }
    e->overflow = MAX(e->overflow, style_shadow_overflow(b->style));
    if (b->scrolls) return;
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        subtree_extent(c, dx, dy, e);
    if (!b->inline_atomics) return;
    for (guint i = 0; i < b->inline_atomics->len; i++) {
        const ns_inline_atomic *a =
            &g_array_index(b->inline_atomics, ns_inline_atomic, i);
        if (!a->box) continue;
        double tx = b->x + a->owner_offset_x - a->box->x;
        double ty = b->y + a->owner_offset_y - a->box->y;
        if (isfinite(tx) && isfinite(ty))
            subtree_extent(a->box, dx + tx, dy + ty, e);
    }
}

static void
hazards_walk(const ns_box *b, ns_anim *anim, ns_paint_hazards *h,
             gboolean in_fixed)
{
    if (!b) return;
    if (b->style) {
        if (style_keyword_is(b->style, NS_CSS_POSITION, "sticky"))
            h->sticky = TRUE;
        if (style_keyword_is(b->style, NS_CSS_TRANSFORM_STYLE, "preserve-3d") ||
            box_has_perspective(b))
            h->three_d = TRUE;
        box_has_transform(b, anim, &h->three_d);
        if (box_has_fixed_background(b)) h->fixed_background = TRUE;
    }
    if (!in_fixed && ns_box_is_fixed(b)) {
        paint_extent e = { G_MAXDOUBLE, G_MAXDOUBLE, -G_MAXDOUBLE,
                           -G_MAXDOUBLE, 0 };
        subtree_extent(b, 0, 0, &e);
        if (e.x1 > e.x0 && e.y1 > e.y0) {
            double pad = NS_DAMAGE_FIXED_INFLATE + e.overflow;
            ns_damage_rect band = {
                .x = e.x0 - pad,
                .y = e.y0 - pad,
                .w = e.x1 - e.x0 + 2 * pad,
                .h = e.y1 - e.y0 + 2 * pad,
                .fixed = TRUE,
            };
            g_array_append_val(h->fixed_bands, band);
        }
        in_fixed = TRUE;
    }
    for (const ns_box *c = b->first_child; c; c = c->next_sibling)
        hazards_walk(c, anim, h, in_fixed);
    if (!b->inline_atomics) return;
    for (guint i = 0; i < b->inline_atomics->len; i++)
        hazards_walk(g_array_index(b->inline_atomics, ns_inline_atomic, i).box,
                     anim, h, in_fixed);
}

void
ns_paint_hazards_scan(const ns_box *root, ns_anim *anim, ns_paint_hazards *out)
{
    ns_paint_hazards_clear(out);
    out->fixed_bands = g_array_new(FALSE, FALSE, sizeof(ns_damage_rect));
    hazards_walk(root, anim, out, FALSE);
}

void
ns_paint_hazards_clear(ns_paint_hazards *hazards)
{
    if (!hazards) return;
    if (hazards->fixed_bands) g_array_free(hazards->fixed_bands, TRUE);
    memset(hazards, 0, sizeof *hazards);
}
