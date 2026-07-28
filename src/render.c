/* Northstar — shared style/layout pipeline used by GUI and headless.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "render.h"

#include <math.h>
#include <string.h>

#include "font.h"
#include "net.h"

static gboolean g_render_page_uses_hover = FALSE;

gboolean
ns_render_page_uses_hover(void)
{
    return g_render_page_uses_hover;
}

static gboolean g_render_page_uses_active = FALSE;

gboolean
ns_render_page_uses_active(void)
{
    return g_render_page_uses_active;
}

static void
render_feed_animations(const ns_render_ctx *c, GHashTable *styles)
{
    if (!c->anim) return;
    for (guint i = 0; i < c->n_sheets; i++)
        if (c->sheets[i]) ns_anim_load_from_stylesheet(c->anim, c->sheets[i]);
    gint64 now_us = g_get_monotonic_time();
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, styles);
    while (g_hash_table_iter_next(&it, &key, &val))
        ns_anim_observe(c->anim, (const ns_node *)key,
                        (const ns_style *)val, now_us);
    ns_anim_prune(c->anim, styles);
}

static void
render_request_fonts(const ns_render_ctx *c)
{
    if (!ns_font_available()) return;
    for (guint i = 0; i < c->n_sheets; i++) {
        const ns_css_stylesheet *sh = c->sheets[i];
        if (!sh || !sh->font_faces) continue;
        for (guint j = 0; j < sh->font_faces->len; j++) {
            const ns_css_font_face *ff =
                &g_array_index(sh->font_faces, ns_css_font_face, j);
            if (!ff->family || !ff->src_url) continue;
            char *abs = c->resolve_url
                ? c->resolve_url(ff->src_url, c->cb_ud)
                : ns_url_resolve(c->base_url, ff->src_url);
            if (!abs) continue;
            if (c->font_allowed && !c->font_allowed(abs, c->cb_ud)) {
                g_free(abs);
                continue;
            }
            ns_font_request(ff->family, abs, c->base_url);
            g_free(abs);
        }
    }
}

static void
render_apply_zoom(const ns_render_ctx *c, GHashTable *styles)
{
    double zoom = c->zoom > 0 ? c->zoom : 1.0;
    if (fabs(zoom - 1.0) <= 0.001) return;
    GHashTableIter it;
    gpointer key, val;
    g_hash_table_iter_init(&it, styles);
    while (g_hash_table_iter_next(&it, &key, &val)) {
        ns_style *st = val;
        if (!st || !st->values[NS_CSS_FONT_SIZE]) continue;
        if (st->values[NS_CSS_FONT_SIZE]->kind != NS_CSS_V_LENGTH) continue;
        st->values[NS_CSS_FONT_SIZE]->u.length.v *= zoom;
    }
}

static void
render_collect_containers(const ns_box *b, GHashTable *map)
{
    if (!b) return;
    if (b->dom && b->style) {
        const ns_css_value *ct = b->style->values[NS_CSS_CONTAINER_TYPE];
        if (ct && ct->kind == NS_CSS_V_KEYWORD && ct->u.keyword &&
            g_ascii_strcasecmp(ct->u.keyword, "normal") != 0) {
            const ns_css_value *nm = b->style->values[NS_CSS_CONTAINER_NAME];
            const char *names = (nm && nm->kind == NS_CSS_V_KEYWORD)
                ? nm->u.keyword : NULL;
            ns_css_container_map_add(map, b->dom, ct->u.keyword, names,
                                     b->content_width, b->content_height);
        }
    }
    for (const ns_box *ch = b->first_child; ch; ch = ch->next_sibling)
        render_collect_containers(ch, map);
}

static void
render_style_pass(const ns_render_ctx *c, GHashTable *styles)
{
    render_feed_animations(c, styles);
    render_request_fonts(c);
    render_apply_zoom(c, styles);
}

static gboolean
render_value_equal(const ns_css_value *a, const ns_css_value *b)
{
    if (a == b) return TRUE;
    if (!a || !b || a->kind != b->kind) return FALSE;
    gboolean equal = FALSE;
    switch (a->kind) {
    case NS_CSS_V_KEYWORD:
        equal = g_strcmp0(a->u.keyword, b->u.keyword) == 0;
        break;
    case NS_CSS_V_LENGTH:
        equal = a->u.length.v == b->u.length.v &&
                a->u.length.unit == b->u.length.unit;
        break;
    case NS_CSS_V_SIZE:
        equal = a->u.size.w == b->u.size.w &&
                a->u.size.h == b->u.size.h &&
                a->u.size.w_unit == b->u.size.w_unit &&
                a->u.size.h_unit == b->u.size.h_unit &&
                a->u.size.w_auto == b->u.size.w_auto &&
                a->u.size.h_auto == b->u.size.h_auto;
        break;
    case NS_CSS_V_COLOR:
        equal = a->u.color.r == b->u.color.r &&
                a->u.color.g == b->u.color.g &&
                a->u.color.b == b->u.color.b &&
                a->u.color.a == b->u.color.a;
        break;
    case NS_CSS_V_CALC:
        equal = a->u.calc.pct == b->u.calc.pct &&
                a->u.calc.px == b->u.calc.px &&
                a->u.calc.em == b->u.calc.em &&
                a->u.calc.rem == b->u.calc.rem &&
                a->u.calc.lh == b->u.calc.lh &&
                a->u.calc.rlh == b->u.calc.rlh;
        break;
    case NS_CSS_V_URL:
        equal = g_strcmp0(a->u.url, b->u.url) == 0;
        break;
    case NS_CSS_V_RECT:
        equal = TRUE;
        for (int i = 0; i < 4 && equal; i++)
            equal = a->u.rect.v[i] == b->u.rect.v[i] &&
                    a->u.rect.unit[i] == b->u.rect.unit[i] &&
                    a->u.rect.is_auto[i] == b->u.rect.is_auto[i];
        break;
    default:
        return FALSE;
    }
    return equal && render_value_equal(a->next_layer, b->next_layer);
}

static gboolean
render_vars_equal(const struct ns_var_map *a, const struct ns_var_map *b)
{
    if (a == b) return TRUE;
    GPtrArray *an = ns_var_map_names(a);
    GPtrArray *bn = ns_var_map_names(b);
    gboolean equal = an->len == bn->len;
    for (guint i = 0; equal && i < an->len; i++) {
        const char *name_a = g_ptr_array_index(an, i);
        const char *name_b = g_ptr_array_index(bn, i);
        equal = strcmp(name_a, name_b) == 0 &&
                g_strcmp0(ns_var_map_lookup(a, name_a),
                          ns_var_map_lookup(b, name_b)) == 0;
    }
    g_ptr_array_unref(an);
    g_ptr_array_unref(bn);
    return equal;
}

static gboolean
render_style_equal(const ns_style *a, const ns_style *b)
{
    if (a == b) return TRUE;
    if (!a || !b || a->display.box != b->display.box ||
        a->display.outer != b->display.outer ||
        a->display.inner != b->display.inner ||
        a->display.internal != b->display.internal ||
        a->display.list_item != b->display.list_item)
        return FALSE;
    for (int i = 0; i < NS_CSS_PROP_COUNT; i++)
        if (!render_value_equal(a->values[i], b->values[i])) return FALSE;
    return render_vars_equal(a->vars, b->vars) &&
           render_style_equal(a->before, b->before) &&
           render_style_equal(a->after, b->after) &&
           render_style_equal(a->first_letter, b->first_letter) &&
           render_style_equal(a->first_line, b->first_line) &&
           render_style_equal(a->placeholder, b->placeholder) &&
           render_style_equal(a->selection, b->selection) &&
           render_style_equal(a->marker, b->marker) &&
           render_style_equal(a->backdrop, b->backdrop) &&
           render_style_equal(a->file_selector_button,
                              b->file_selector_button);
}

static gboolean
render_style_tables_equal(GHashTable *a, GHashTable *b)
{
    if (g_hash_table_size(a) != g_hash_table_size(b)) return FALSE;
    GHashTableIter iter;
    gpointer node, style;
    g_hash_table_iter_init(&iter, a);
    while (g_hash_table_iter_next(&iter, &node, &style)) {
        ns_style *other = g_hash_table_lookup(b, node);
        if (!render_style_equal(style, other)) return FALSE;
    }
    return TRUE;
}

static const ns_node *
render_find_viewport_meta(const ns_node *n, int depth)
{
    if (!n || depth >= 512) return NULL;
    if (ns_node_is_element_named(n, "meta")) {
        const char *name = ns_element_get_attr(n, "name");
        if (name && g_ascii_strcasecmp(name, "viewport") == 0)
            return n;
    }
    for (const ns_node *c = n->first_child; c; c = c->next_sibling) {
        const ns_node *found = render_find_viewport_meta(c, depth + 1);
        if (found) return found;
    }
    return NULL;
}

static double
render_parse_viewport_width(const char *content)
{
    if (!content || !*content) return 0;
    double out = 0;
    char **parts = g_strsplit_set(content, ",;", -1);
    for (int i = 0; parts && parts[i]; i++) {
        char *part = g_strstrip(parts[i]);
        char *eq = strchr(part, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = g_strstrip(part);
        char *value = g_strstrip(eq + 1);
        if (g_ascii_strcasecmp(key, "width") != 0) continue;
        if (g_ascii_strcasecmp(value, "device-width") == 0) break;
        char *end = NULL;
        double n = g_ascii_strtod(value, &end);
        if (end != value && n >= 320 && n <= 4096) {
            out = n;
            break;
        }
    }
    g_strfreev(parts);
    return out;
}

static double
render_effective_viewport_width(const ns_render_ctx *c)
{
    double width = c->viewport_width;
    const ns_node *meta = render_find_viewport_meta(c->doc, 0);
    const char *content = meta ? ns_element_get_attr(meta, "content") : NULL;
    double hint = render_parse_viewport_width(content);
    if (hint > width) width = hint;
    return width;
}

GHashTable *
ns_render_relayout_profile(const ns_render_ctx *c, ns_box **out_layout,
                           ns_render_profile *profile)
{
    if (out_layout) *out_layout = NULL;
    if (!c || !out_layout) return NULL;
    if (profile) memset(profile, 0, sizeof *profile);

    double viewport_width = render_effective_viewport_width(c);
    ns_css_set_viewport(viewport_width, c->viewport_height);
    ns_css_set_focus_node(c->focused_input);
    ns_css_set_hover_node(c->hover_node);

    gboolean uses_hover = FALSE;
    for (guint i = 0; i < c->n_sheets && !uses_hover; i++)
        uses_hover = ns_css_stylesheet_has_hover_rules(c->sheets[i]);
    g_render_page_uses_hover = uses_hover;

    gboolean uses_active = FALSE;
    for (guint i = 0; i < c->n_sheets && !uses_active; i++)
        uses_active = ns_css_stylesheet_has_active_rules(c->sheets[i]);
    g_render_page_uses_active = uses_active;

    gint64 t0 = profile ? g_get_monotonic_time() : 0;
    ns_css_set_render_zoom(c->zoom > 0 ? c->zoom : 1.0);
    gboolean cache_selectors = FALSE;
    for (guint i = 0; i < c->n_sheets && !cache_selectors; i++)
        cache_selectors = ns_css_stylesheet_has_container_rules(c->sheets[i]);
    if (cache_selectors) ns_css_selector_cache_begin();
    GHashTable *styles = ns_css_compute(c->doc, c->sheets, c->n_sheets);
    gint64 t1 = profile ? g_get_monotonic_time() : 0;

    render_style_pass(c, styles);
    gint64 t2 = profile ? g_get_monotonic_time() : 0;

    ns_box *layout = ns_layout_build(c->doc, styles, viewport_width,
                                     c->focused_input, c->caret_byte,
                                     c->sel_anchor_byte,
                                     c->images, c->base_url);
    gint64 t3 = profile ? g_get_monotonic_time() : 0;
    if (profile) {
        profile->css1_us = t1 - t0;
        profile->style1_us = t2 - t1;
        profile->layout1_us = t3 - t2;
    }

    GHashTable *containers = ns_css_container_map_new();
    gint64 tc0 = profile ? g_get_monotonic_time() : 0;
    render_collect_containers(layout, containers);
    gint64 tc1 = profile ? g_get_monotonic_time() : 0;
    guint n_containers = g_hash_table_size(containers);
    if (profile) {
        profile->container_us = tc1 - tc0;
        profile->containers = n_containers;
    }
    if (n_containers > 0) {
        if (profile) profile->container_pass = TRUE;
        ns_css_set_container_map(containers);
        ns_css_container_features_begin();
        gint64 t4 = profile ? g_get_monotonic_time() : 0;
        GHashTable *styles2 = ns_css_compute(c->doc, c->sheets, c->n_sheets);
        gint64 t5 = profile ? g_get_monotonic_time() : 0;
        gboolean container_features_used = ns_css_container_features_used();
        ns_css_set_container_map(NULL);
        if (container_features_used &&
            !render_style_tables_equal(styles, styles2)) {
            render_style_pass(c, styles2);
            gint64 t6 = profile ? g_get_monotonic_time() : 0;
            ns_box *layout2 = ns_layout_build(c->doc, styles2, viewport_width,
                                              c->focused_input, c->caret_byte,
                                              c->sel_anchor_byte,
                                              c->images, c->base_url);
            gint64 t7 = profile ? g_get_monotonic_time() : 0;
            if (profile) {
                profile->css2_us = t5 - t4;
                profile->style2_us = t6 - t5;
                profile->layout2_us = t7 - t6;
            }
            ns_box_free(layout);
            g_hash_table_destroy(styles);
            layout = layout2;
            styles = styles2;
        } else {
            if (profile) profile->css2_us = t5 - t4;
            g_hash_table_destroy(styles2);
        }
    }
    g_hash_table_destroy(containers);
    if (cache_selectors) ns_css_selector_cache_end();
    ns_css_set_focus_node(NULL);
    ns_css_set_hover_node(NULL);

    if (c->js) {
        ns_js_set_style_table(c->js, styles);
        ns_js_set_layout_root(c->js, layout);
    }
    *out_layout = layout;
    return styles;
}

GHashTable *
ns_render_relayout(const ns_render_ctx *c, ns_box **out_layout)
{
    return ns_render_relayout_profile(c, out_layout, NULL);
}
