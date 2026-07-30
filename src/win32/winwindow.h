/* winwindow.h - Native Win32 single-page browser shell interface. */

#ifndef NORTHSTAR_WIN32_WINWINDOW_H
#define NORTHSTAR_WIN32_WINWINDOW_H

#include <glib.h>

int ns_winapp_run(const char *startup_url, const char *session_path,
                  gboolean recover, gboolean private_mode);
void ns_winapp_set_window_size(int width, int height);

#endif
