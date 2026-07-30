/* winview.h - Native Win32 page-view interface over the renderer protocol. */

#ifndef NORTHSTAR_WIN32_WINVIEW_H
#define NORTHSTAR_WIN32_WINVIEW_H

#include <glib.h>
#include <windows.h>

typedef struct NsWinView NsWinView;

typedef enum NsWinViewEvent {
    NS_WINVIEW_TITLE,
    NS_WINVIEW_URL,
    NS_WINVIEW_STATUS,
    NS_WINVIEW_HISTORY,
    NS_WINVIEW_LOADING,
    NS_WINVIEW_DOWNLOAD
} NsWinViewEvent;

typedef void (*NsWinViewNotify)(NsWinView *view, NsWinViewEvent event,
                                const char *text, void *user_data);

gboolean ns_winview_register(HINSTANCE instance);
NsWinView *ns_winview_new(HWND parent, HINSTANCE instance);
HWND ns_winview_hwnd(NsWinView *view);
void ns_winview_destroy(NsWinView *view);
void ns_winview_set_notify(NsWinView *view, NsWinViewNotify notify,
                           void *user_data);
void ns_winview_set_private(NsWinView *view, gboolean private_mode);
gboolean ns_winview_is_private(const NsWinView *view);
void ns_winview_load(NsWinView *view, const char *url);
void ns_winview_back(NsWinView *view);
void ns_winview_forward(NsWinView *view);
void ns_winview_reload(NsWinView *view);
gboolean ns_winview_can_back(const NsWinView *view);
gboolean ns_winview_can_forward(const NsWinView *view);
const char *ns_winview_url(const NsWinView *view);
const char *ns_winview_title(const NsWinView *view);
gboolean ns_winview_is_loading(const NsWinView *view);
int ns_winview_security(const NsWinView *view);
const char *ns_winview_remote_ip(const NsWinView *view);
int ns_winview_renderer_pid(NsWinView *view);
void ns_winview_end_task(NsWinView *view);
void ns_winview_zoom_in(NsWinView *view);
void ns_winview_zoom_out(NsWinView *view);
void ns_winview_zoom_reset(NsWinView *view);
void ns_winview_focus(NsWinView *view);
void ns_winview_find_open(NsWinView *view);
void ns_winview_toggle_devtools(NsWinView *view);
void ns_winview_layout(NsWinView *view, int x, int y, int width, int height);
void ns_winview_refresh_font(NsWinView *view, UINT dpi);

#endif
