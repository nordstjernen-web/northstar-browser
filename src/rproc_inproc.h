/* Nordstjernen — single-process mode: in-process renderer host.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_RPROC_INPROC_H
#define NS_RPROC_INPROC_H

#ifdef __cplusplus
extern "C" {
#endif

void ns_rproc_single_process_enable(void);
int  ns_rproc_single_process_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
