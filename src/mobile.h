/* Nordstjernen - force the mobile variant of select sites.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_MOBILE_H
#define NS_MOBILE_H

#include <glib.h>

G_BEGIN_DECLS

const char *ns_mobile_user_agent(void);

gboolean ns_mobile_force_host(const char *host);


G_END_DECLS

#endif
