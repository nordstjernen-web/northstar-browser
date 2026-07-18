/* Nordstjernen — per-process thread dump to stderr, incl. a SIGQUIT trigger.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_THREADDUMP_H
#define NS_THREADDUMP_H

char *ns_thread_dump_text(int pid, const char *label);
void  ns_thread_dump_to_stderr(int pid, const char *label);
void  ns_thread_dump_install_signal(const char *label);

#endif
