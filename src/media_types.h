/* Northstar — which media types the compiled decoders can play.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_MEDIA_TYPES_H
#define NS_MEDIA_TYPES_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
    NS_MEDIA_ELEMENT_AUDIO,
    NS_MEDIA_ELEMENT_VIDEO,
} ns_media_element;

typedef enum {
    NS_MEDIA_SOURCE_FILE,
    NS_MEDIA_SOURCE_MSE,
} ns_media_source;

typedef enum {
    NS_MEDIA_CANNOT,
    NS_MEDIA_MAYBE,
    NS_MEDIA_PROBABLY,
} ns_media_answer;

ns_media_answer ns_media_type_support(const char *type,
                                      ns_media_element element,
                                      ns_media_source source);
const char *ns_media_answer_string(ns_media_answer answer);

G_END_DECLS

#endif
