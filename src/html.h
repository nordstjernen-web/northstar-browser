/* Northstar — HTML parser API (lexbor).
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_HTML_H
#define NS_HTML_H

#include <glib.h>

#include "dom.h"

G_BEGIN_DECLS

typedef struct ns_html_parser ns_html_parser;
typedef struct ns_html_fragment_parser ns_html_fragment_parser;

ns_html_parser *ns_html_parser_new(gboolean scripting);
gboolean ns_html_parser_write(ns_html_parser *parser,
                              const char *input, gsize len);
ns_node *ns_html_parser_finish(ns_html_parser *parser);
void ns_html_parser_free(ns_html_parser *parser);

ns_html_fragment_parser *ns_html_fragment_parser_new(
    const char *context_tag, gboolean scripting);
gboolean ns_html_fragment_parser_write(ns_html_fragment_parser *parser,
                                       const char *input, gsize len);
gboolean ns_html_fragment_parser_finish(ns_html_fragment_parser *parser);
gboolean ns_html_fragment_parser_needs_source(
    const ns_html_fragment_parser *parser);
ns_node *ns_html_fragment_parser_output(ns_html_fragment_parser *parser);
void ns_html_fragment_parser_free(ns_html_fragment_parser *parser);

ns_node *ns_html_parse(const char *input, gssize len);
ns_node *ns_html_parse_with_scripting(const char *input, gssize len,
                                      gboolean scripting);

ns_node *ns_xml_parse(const char *input, gssize len);
ns_node *ns_xml_parse_reporting(const char *input, gssize len,
                                int *line, int *column);

char *ns_html_mime_essence(const char *content_type);
gboolean ns_html_mime_is_xml(const char *content_type);
ns_node *ns_html_parse_document(const char *input, gssize len,
                                const char *content_type);

gboolean ns_xml_well_formed(const char *input, gssize len, char **out_root_ns);

ns_node *ns_html_parse_fragment_with_scripting(const char *context_tag,
                                               const char *input, gssize len,
                                               gboolean scripting);

void ns_html_convert_declarative_shadow(ns_node *root);

gboolean ns_html_is_void(const char *tag);

gboolean ns_html_is_raw_text(const char *tag);

void ns_html_escape_append(GString *out, const char *s, gboolean escape_quotes);

char *ns_html_escape_text(const char *s);

char *ns_html_declared_charset(const char *body, gsize len,
                               const char *content_type);

char *ns_html_decode_body_full(const char *body, gsize len,
                               const char *content_type, char **charset_out);

char *ns_html_image_document(const char *url);

char *ns_html_json_document(const char *url, const char *json, gsize len);
G_END_DECLS

#endif
