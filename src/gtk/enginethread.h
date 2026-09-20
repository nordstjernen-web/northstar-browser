/* Northstar — the thread the page engine runs on, with its own main context.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NORTHSTAR_GTK_ENGINETHREAD_H
#define NORTHSTAR_GTK_ENGINETHREAD_H

#include <glib.h>

typedef void (*NsEngineJob)(gpointer data);

void ns_engine_thread_post(NsEngineJob fn, gpointer data);

#endif
