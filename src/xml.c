/* Northstar — minimal namespaced XML / XHTML parser to the DOM.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "html.h"
#include "dom.h"

#include <glib.h>
#include <string.h>

#define XML_NS_XML   "http://www.w3.org/XML/1998/namespace"
#define XML_NS_XMLNS "http://www.w3.org/2000/xmlns/"
#define XML_NS_XHTML "http://www.w3.org/1999/xhtml"
#define XML_NS_SVG   "http://www.w3.org/2000/svg"

typedef struct {
    char *prefix;
    char *uri;
} xml_binding;

typedef struct {
    const char *p;
    const char *end;
    GPtrArray  *ns_stack;
    GHashTable *entities;
    gboolean    ok;
} xml_parser;

static gboolean
xml_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void
xml_skip_space(xml_parser *xp)
{
    while (xp->p < xp->end && xml_is_space(*xp->p)) xp->p++;
}

static gboolean
xml_name_char(char c, gboolean first)
{
    if (c == ':' || c == '_') return TRUE;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return TRUE;
    if ((unsigned char)c >= 0x80) return TRUE;
    if (first) return FALSE;
    if (c == '-' || c == '.') return TRUE;
    if (c >= '0' && c <= '9') return TRUE;
    return FALSE;
}

static char *
xml_read_name(xml_parser *xp)
{
    const char *s = xp->p;
    if (xp->p >= xp->end || !xml_name_char(*xp->p, TRUE)) return NULL;
    xp->p++;
    while (xp->p < xp->end && xml_name_char(*xp->p, FALSE)) xp->p++;
    return g_strndup(s, (gsize)(xp->p - s));
}

static gboolean
xml_qname_valid(const char *name)
{
    const char *colon = name ? strchr(name, ':') : NULL;
    return !colon || (colon != name && colon[1] != '\0' &&
                      strchr(colon + 1, ':') == NULL);
}

static const char *
xml_skip_space_range(const char *p, const char *end)
{
    while (p < end && xml_is_space(*p)) p++;
    return p;
}

static gboolean
xml_skip_quoted_range(const char **cursor, const char *end)
{
    const char *p = *cursor;
    if (p >= end || (*p != '"' && *p != '\'')) return FALSE;
    char quote = *p++;
    while (p < end && *p != quote) p++;
    if (p >= end) return FALSE;
    *cursor = p + 1;
    return TRUE;
}

static gboolean
xml_doctype_tail_valid(const char *p, const char *end)
{
    p = xml_skip_space_range(p, end);
    if (p == end || *p == '[') return TRUE;
    gboolean public_id = end - p >= 6 && strncmp(p, "PUBLIC", 6) == 0 &&
                         p + 6 < end && xml_is_space(p[6]);
    gboolean system_id = end - p >= 6 && strncmp(p, "SYSTEM", 6) == 0 &&
                         p + 6 < end && xml_is_space(p[6]);
    if (!public_id && !system_id) return FALSE;
    p = xml_skip_space_range(p + 6, end);
    if (!xml_skip_quoted_range(&p, end)) return FALSE;
    p = xml_skip_space_range(p, end);
    if (public_id) {
        if (!xml_skip_quoted_range(&p, end)) return FALSE;
        p = xml_skip_space_range(p, end);
    }
    return p == end || *p == '[';
}

static void
xml_append_codepoint(GString *out, guint64 cp)
{
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        g_string_append(out, "\xEF\xBF\xBD");
        return;
    }
    char buf[6];
    gint nb = g_unichar_to_utf8((gunichar)cp, buf);
    g_string_append_len(out, buf, nb);
}

static char *
xml_decode_text(xml_parser *xp, const char *s, gsize len)
{
    GString *out = g_string_sized_new(len);
    const char *p = s, *end = s + len;
    while (p < end) {
        if (*p != '&') { g_string_append_c(out, *p++); continue; }
        const char *semi = memchr(p, ';', (gsize)(end - p));
        if (!semi) { xp->ok = FALSE; break; }
        gsize elen = (gsize)(semi - p - 1);
        const char *e = p + 1;
        if (elen == 0) { xp->ok = FALSE; break; }
        if (*e == '#') {
            gboolean hex = elen >= 2 && (e[1] == 'x' || e[1] == 'X');
            const char *digits = e + (hex ? 2 : 1);
            char *raw = g_strndup(digits, (gsize)(semi - digits));
            char *num_end = NULL;
            guint64 cp = g_ascii_strtoull(raw, &num_end, hex ? 16 : 10);
            gboolean valid = num_end && num_end != raw && *num_end == '\0' &&
                cp != 0 && cp <= 0x10FFFF &&
                !(cp >= 0xD800 && cp <= 0xDFFF);
            g_free(raw);
            if (!valid) { xp->ok = FALSE; break; }
            xml_append_codepoint(out, cp);
            p = semi + 1;
            continue;
        }
        if (elen == 3 && strncmp(e, "amp", 3) == 0)       g_string_append_c(out, '&');
        else if (elen == 2 && strncmp(e, "lt", 2) == 0)   g_string_append_c(out, '<');
        else if (elen == 2 && strncmp(e, "gt", 2) == 0)   g_string_append_c(out, '>');
        else if (elen == 4 && strncmp(e, "quot", 4) == 0) g_string_append_c(out, '"');
        else if (elen == 4 && strncmp(e, "apos", 4) == 0) g_string_append_c(out, '\'');
        else {
            char *name = g_strndup(e, elen);
            const char *value = g_hash_table_lookup(xp->entities, name);
            g_free(name);
            if (!value) { xp->ok = FALSE; break; }
            g_string_append(out, value);
        }
        p = semi + 1;
    }
    return g_string_free(out, FALSE);
}

static const char *
xml_lookup_prefix(xml_parser *xp, const char *prefix)
{
    if (prefix && strcmp(prefix, "xml") == 0)   return XML_NS_XML;
    if (prefix && strcmp(prefix, "xmlns") == 0) return XML_NS_XMLNS;
    for (guint i = xp->ns_stack->len; i > 0; i--) {
        xml_binding *b = g_ptr_array_index(xp->ns_stack, i - 1);
        if ((prefix == NULL && b->prefix == NULL) ||
            (prefix && b->prefix && strcmp(prefix, b->prefix) == 0))
            return b->uri;
    }
    return NULL;
}

static void
xml_push_binding(xml_parser *xp, const char *prefix, const char *uri)
{
    xml_binding *b = g_new0(xml_binding, 1);
    b->prefix = prefix && *prefix ? g_strdup(prefix) : NULL;
    b->uri = g_strdup(uri ? uri : "");
    g_ptr_array_add(xp->ns_stack, b);
}

static void
xml_binding_free(gpointer p)
{
    xml_binding *b = p;
    g_free(b->prefix);
    g_free(b->uri);
    g_free(b);
}

#define XML_MAX_DEPTH 256

static gboolean xml_parse_element(xml_parser *xp, ns_node *parent, int depth);

static void
xml_parse_internal_entities(xml_parser *xp, const char *p, const char *end)
{
    while (p < end && xp->ok) {
        const char *decl = g_strstr_len(p, (gssize)(end - p), "<!ENTITY");
        if (!decl) return;
        p = decl + 8;
        xml_parser name_parser = *xp;
        name_parser.p = p;
        name_parser.end = end;
        xml_skip_space(&name_parser);
        if (name_parser.p < end && *name_parser.p == '%') {
            p = name_parser.p + 1;
            continue;
        }
        char *name = xml_read_name(&name_parser);
        if (!name) { xp->ok = FALSE; return; }
        xml_skip_space(&name_parser);
        if (name_parser.p >= end ||
            (*name_parser.p != '"' && *name_parser.p != '\'')) {
            g_free(name);
            p = name_parser.p;
            continue;
        }
        char quote = *name_parser.p++;
        const char *value_start = name_parser.p;
        while (name_parser.p < end && *name_parser.p != quote)
            name_parser.p++;
        if (name_parser.p >= end) {
            g_free(name);
            xp->ok = FALSE;
            return;
        }
        char *value = xml_decode_text(
            xp, value_start, (gsize)(name_parser.p - value_start));
        if (xp->ok) g_hash_table_replace(xp->entities, name, value);
        else { g_free(name); g_free(value); }
        p = name_parser.p + 1;
    }
}

static void
xml_skip_misc_and_doctype(xml_parser *xp, ns_node *doc)
{
    for (;;) {
        xml_skip_space(xp);
        if (xp->end - xp->p < 2 || xp->p[0] != '<') return;
        char c1 = xp->p[1];
        if (c1 == '?') {
            const char *e = xp->p + 2;
            while (e + 1 < xp->end && !(e[0] == '?' && e[1] == '>')) e++;
            if (e + 1 >= xp->end) {
                xp->p = xp->end;
                xp->ok = FALSE;
                return;
            }
            if (doc && !((e - xp->p) >= 5 &&
                         xp->p[2] == 'x' && xp->p[3] == 'm' && xp->p[4] == 'l' &&
                         (e == xp->p + 5 || xml_is_space(xp->p[5])))) {
                const char *t = xp->p + 2;
                const char *tn = t;
                while (tn < e && !xml_is_space(*tn)) tn++;
                char *target = g_strndup(t, (gsize)(tn - t));
                const char *d = tn;
                while (d < e && xml_is_space(*d)) d++;
                ns_node *pi = ns_node_new_comment(g_strndup(d, (gsize)(e - d)));
                ns_node_set_name_owned(pi, target);
                pi->flags |= NS_NODE_PI;
                ns_node_append_child(doc, pi);
            }
            xp->p = e + 2;
            continue;
        }
        if (c1 == '!') {
            if (xp->end - xp->p >= 4 && strncmp(xp->p, "<!--", 4) == 0) {
                const char *e = xp->p + 4;
                while (e + 2 < xp->end && strncmp(e, "-->", 3) != 0) e++;
                if (e + 2 >= xp->end) {
                    xp->p = xp->end;
                    xp->ok = FALSE;
                    return;
                }
                if (doc)
                    ns_node_append_child(doc,
                        ns_node_new_comment(g_strndup(xp->p + 4,
                                                      (gsize)(e - (xp->p + 4)))));
                xp->p = e + 3;
                continue;
            }
            if (xp->end - xp->p >= 9 && g_ascii_strncasecmp(xp->p, "<!DOCTYPE", 9) == 0) {
                const char *e = xp->p + 9;
                int bracket = 0;
                while (e < xp->end) {
                    if (*e == '[') bracket++;
                    else if (*e == ']') { if (bracket > 0) bracket--; }
                    else if (*e == '>' && bracket == 0) break;
                    e++;
                }
                if (e >= xp->end) {
                    xp->p = xp->end;
                    xp->ok = FALSE;
                    return;
                }
                const char *subset = memchr(xp->p + 9, '[',
                    (gsize)(e - (xp->p + 9)));
                if (subset) {
                    const char *subset_end = e;
                    while (subset_end > subset && subset_end[-1] != ']')
                        subset_end--;
                    if (subset_end > subset)
                        xml_parse_internal_entities(xp, subset + 1,
                                                    subset_end - 1);
                }
                if (doc) {
                    const char *t = xp->p + 9;
                    while (t < e && xml_is_space(*t)) t++;
                    const char *tn = t;
                    while (tn < e && !xml_is_space(*tn) && *tn != '>' && *tn != '[') tn++;
                    char *doctype_name = tn > t
                        ? g_strndup(t, (gsize)(tn - t)) : NULL;
                    if (!doctype_name || !xml_qname_valid(doctype_name) ||
                        !xml_doctype_tail_valid(tn, e)) {
                        g_free(doctype_name);
                        xp->ok = FALSE;
                        return;
                    }
                    if (tn > t) {
                        ns_node *dt = ns_node_new_element(NULL);
                        ns_node_set_name_owned(dt, doctype_name);
                        ns_element_set_attr(dt, "publicId", "");
                        ns_element_set_attr(dt, "systemId", "");
                        dt->kind = NS_NODE_DOCTYPE;
                        ns_node_append_child(doc, dt);
                    }
                }
                xp->p = (e < xp->end) ? e + 1 : xp->end;
                continue;
            }
        }
        return;
    }
}

static gboolean
xml_parse_attributes(xml_parser *xp,
                     GPtrArray *names, GPtrArray *values, guint *base)
{
    *base = xp->ns_stack->len;
    for (;;) {
        xml_skip_space(xp);
        if (xp->p >= xp->end) { xp->ok = FALSE; return FALSE; }
        if (*xp->p == '>' || *xp->p == '/') return TRUE;
        char *aname = xml_read_name(xp);
        if (!aname || !xml_qname_valid(aname)) {
            g_free(aname);
            xp->ok = FALSE;
            return FALSE;
        }
        xml_skip_space(xp);
        if (xp->p >= xp->end || *xp->p != '=') { g_free(aname); xp->ok = FALSE; return FALSE; }
        xp->p++;
        xml_skip_space(xp);
        if (xp->p >= xp->end || (*xp->p != '"' && *xp->p != '\'')) {
            g_free(aname); xp->ok = FALSE; return FALSE;
        }
        char q = *xp->p++;
        const char *vs = xp->p;
        while (xp->p < xp->end && *xp->p != q) xp->p++;
        if (xp->p >= xp->end) { g_free(aname); xp->ok = FALSE; return FALSE; }
        char *aval = xml_decode_text(xp, vs, (gsize)(xp->p - vs));
        xp->p++;
        if (!xp->ok) {
            g_free(aname);
            g_free(aval);
            return FALSE;
        }

        gboolean default_decl = strcmp(aname, "xmlns") == 0;
        gboolean prefix_decl = strncmp(aname, "xmlns:", 6) == 0;
        const char *decl_prefix = prefix_decl ? aname + 6 : NULL;
        gboolean invalid_decl =
            (prefix_decl && strcmp(decl_prefix, "xmlns") == 0) ||
            (prefix_decl && strcmp(decl_prefix, "xml") == 0 &&
             strcmp(aval, XML_NS_XML) != 0) ||
            (prefix_decl && strcmp(decl_prefix, "xml") != 0 &&
             (!*aval || strcmp(aval, XML_NS_XML) == 0 ||
              strcmp(aval, XML_NS_XMLNS) == 0)) ||
            (default_decl && (strcmp(aval, XML_NS_XML) == 0 ||
                              strcmp(aval, XML_NS_XMLNS) == 0));
        if (invalid_decl) {
            g_free(aname);
            g_free(aval);
            xp->ok = FALSE;
            return FALSE;
        }

        for (guint i = 0; i < names->len; i++) {
            const char *prior = g_ptr_array_index(names, i);
            if (strcmp(prior, aname) == 0) {
                g_free(aname);
                g_free(aval);
                xp->ok = FALSE;
                return FALSE;
            }
        }

        if (strcmp(aname, "xmlns") == 0) {
            xml_push_binding(xp, NULL, aval);
        } else if (strncmp(aname, "xmlns:", 6) == 0) {
            xml_push_binding(xp, aname + 6, aval);
        }
        g_ptr_array_add(names, aname);
        g_ptr_array_add(values, aval);
    }
}

static void
xml_apply_namespace(ns_node *el, const char *qname, const char *uri)
{
    const char *colon = strchr(qname, ':');
    gboolean has_prefix = colon != NULL;
    el->flags |= NS_NODE_KEEP_CASE | NS_NODE_XML_DOC;
    if (has_prefix) {
        char *prefix = g_strndup(qname, (gsize)(colon - qname));
        ns_element_set_attr(el, "data-nd-ns-prefix", prefix);
        g_free(prefix);
    }
    if (uri && *uri)
        ns_element_set_attr(el, "data-nd-ns-uri", uri);
    if (uri && strcmp(uri, XML_NS_XHTML) == 0) {
        return;
    }
    if (uri && strcmp(uri, XML_NS_SVG) == 0)
        el->flags |= NS_NODE_SVG_NS;
    else
        el->flags |= NS_NODE_FOREIGN_NS;
}

static gboolean
xml_parse_element(xml_parser *xp, ns_node *parent, int depth)
{
    if (depth > XML_MAX_DEPTH) { xp->ok = FALSE; return FALSE; }
    xp->p++;
    char *qname = xml_read_name(xp);
    if (!qname || !xml_qname_valid(qname) ||
        g_str_has_prefix(qname, "xmlns:")) {
        g_free(qname);
        xp->ok = FALSE;
        return FALSE;
    }

    GPtrArray *anames = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *avals  = g_ptr_array_new_with_free_func(g_free);
    guint base = 0;
    if (!xml_parse_attributes(xp, anames, avals, &base)) {
        g_free(qname);
        g_ptr_array_free(anames, TRUE);
        g_ptr_array_free(avals, TRUE);
        return FALSE;
    }

    const char *colon = strchr(qname, ':');
    char *prefix = colon ? g_strndup(qname, (gsize)(colon - qname)) : NULL;
    const char *el_uri = xml_lookup_prefix(xp, prefix);
    if (prefix && !el_uri) {
        xp->ok = FALSE;
        g_free(prefix);
        g_free(qname);
        g_ptr_array_free(anames, TRUE);
        g_ptr_array_free(avals, TRUE);
        g_ptr_array_set_size(xp->ns_stack, base);
        return FALSE;
    }

    gboolean is_xhtml = el_uri && strcmp(el_uri, XML_NS_XHTML) == 0;
    char *stored = (is_xhtml && colon) ? g_strdup(colon + 1) : g_strdup(qname);
    ns_node *el = ns_node_new_element(stored);
    el->flags |= NS_NODE_NOT_PARSER_INSERTED;
    xml_apply_namespace(el, qname, el_uri);

    for (guint i = 0; i < anames->len; i++) {
        const char *an = g_ptr_array_index(anames, i);
        const char *av = g_ptr_array_index(avals, i);
        const char *acolon = strchr(an, ':');
        if (strcmp(an, "xmlns") == 0) {
            ns_element_set_attr_ns(el, XML_NS_XMLNS, NULL, "xmlns", "xmlns", av);
        } else if (acolon) {
            char *apfx = g_strndup(an, (gsize)(acolon - an));
            const char *auri = xml_lookup_prefix(xp, apfx);
            if (!auri) xp->ok = FALSE;
            ns_element_set_attr_ns(el, auri, apfx, acolon + 1, an, av);
            g_free(apfx);
        } else {
            ns_element_set_attr_ns(el, NULL, NULL, an, an, av);
        }
    }
    g_ptr_array_free(anames, TRUE);
    g_ptr_array_free(avals, TRUE);
    g_free(prefix);

    if (!xp->ok) {
        ns_node_free(el);
        g_ptr_array_set_size(xp->ns_stack, base);
        g_free(qname);
        return FALSE;
    }

    ns_node_append_child(parent, el);

    xml_skip_space(xp);
    if (xp->p < xp->end && *xp->p == '/') {
        xp->p++;
        if (xp->p >= xp->end || *xp->p != '>') { xp->ok = FALSE; }
        else xp->p++;
        g_ptr_array_set_size(xp->ns_stack, base);
        g_free(qname);
        return xp->ok;
    }
    if (xp->p >= xp->end || *xp->p != '>') { xp->ok = FALSE; g_free(qname); return FALSE; }
    xp->p++;

    gboolean closed = FALSE;
    while (xp->ok && xp->p < xp->end) {
        if (*xp->p == '<') {
            if (xp->end - xp->p >= 2 && xp->p[1] == '/') {
                xp->p += 2;
                char *ename = xml_read_name(xp);
                if (ename && !xml_qname_valid(ename)) xp->ok = FALSE;
                xml_skip_space(xp);
                if (xp->p < xp->end && *xp->p == '>') xp->p++;
                else xp->ok = FALSE;
                if (!ename || strcmp(ename, qname) != 0) xp->ok = FALSE;
                else closed = TRUE;
                g_free(ename);
                break;
            }
            if (xp->end - xp->p >= 4 && strncmp(xp->p, "<!--", 4) == 0) {
                const char *e = xp->p + 4;
                while (e + 2 < xp->end && strncmp(e, "-->", 3) != 0) e++;
                if (e + 2 >= xp->end) { xp->ok = FALSE; break; }
                ns_node_append_child(el,
                    ns_node_new_comment(g_strndup(xp->p + 4,
                                                  (gsize)(e - (xp->p + 4)))));
                xp->p = e + 3;
                continue;
            }
            if (xp->end - xp->p >= 9 && strncmp(xp->p, "<![CDATA[", 9) == 0) {
                const char *e = xp->p + 9;
                while (e + 2 < xp->end && strncmp(e, "]]>", 3) != 0) e++;
                if (e + 2 >= xp->end) { xp->ok = FALSE; break; }
                ns_node *t = ns_node_new_text(g_strndup(xp->p + 9,
                                                        (gsize)(e - (xp->p + 9))));
                t->flags |= NS_NODE_CDATA;
                ns_node_append_child(el, t);
                xp->p = e + 3;
                continue;
            }
            if (xp->end - xp->p >= 2 && xp->p[1] == '?') {
                const char *e = xp->p + 2;
                while (e + 1 < xp->end && !(e[0] == '?' && e[1] == '>')) e++;
                if (e + 1 >= xp->end) { xp->ok = FALSE; break; }
                const char *t = xp->p + 2;
                const char *tn = t;
                while (tn < e && !xml_is_space(*tn)) tn++;
                char *target = g_strndup(t, (gsize)(tn - t));
                const char *d = tn;
                while (d < e && xml_is_space(*d)) d++;
                ns_node *pi = ns_node_new_comment(g_strndup(d, (gsize)(e - d)));
                ns_node_set_name_owned(pi, target);
                pi->flags |= NS_NODE_PI;
                ns_node_append_child(el, pi);
                xp->p = e + 2;
                continue;
            }
            if (!xml_parse_element(xp, el, depth + 1)) break;
            continue;
        }
        const char *ts = xp->p;
        while (xp->p < xp->end && *xp->p != '<') xp->p++;
        char *txt = xml_decode_text(xp, ts, (gsize)(xp->p - ts));
        ns_node_append_child(el, ns_node_new_text(txt));
    }

    if (xp->ok && !closed) xp->ok = FALSE;

    g_ptr_array_set_size(xp->ns_stack, base);
    g_free(qname);
    return xp->ok;
}

ns_node *
ns_xml_parse(const char *input, gssize len)
{
    if (!input) return NULL;
    gsize n = (len < 0) ? strlen(input) : (gsize)len;
    xml_parser xp = {
        .p = input,
        .end = input + n,
        .ns_stack = g_ptr_array_new_with_free_func(xml_binding_free),
        .entities = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          g_free, g_free),
        .ok = TRUE,
    };
    if (n >= 3 && (unsigned char)xp.p[0] == 0xEF &&
        (unsigned char)xp.p[1] == 0xBB && (unsigned char)xp.p[2] == 0xBF)
        xp.p += 3;

    ns_node *doc = ns_node_new_document();
    doc->flags |= NS_NODE_XML_DOC;
    xml_skip_misc_and_doctype(&xp, doc);

    ns_node *root = NULL;
    if (xp.p < xp.end && *xp.p == '<') {
        guint depth = xp.ns_stack->len;
        if (xml_parse_element(&xp, doc, 0))
            root = doc->last_child;
        (void)depth;
    } else {
        xp.ok = FALSE;
    }

    if (xp.ok)
        xml_skip_misc_and_doctype(&xp, doc);
    if (xp.ok && xp.p != xp.end)
        xp.ok = FALSE;

    g_ptr_array_free(xp.ns_stack, TRUE);
    g_hash_table_destroy(xp.entities);

    if (!xp.ok || !root) {
        ns_node_free(doc);
        return NULL;
    }
    return doc;
}
