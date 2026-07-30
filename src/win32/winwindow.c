/* winwindow.c - Native Win32 single-page browser window and chrome. */

#include "winwindow.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/audio.h"
#include "bookmarks.h"
#include "cache.h"
#include "config.h"
#include "css.h"
#include "history.h"
#include "i18n.h"
#include "net.h"
#include "rproc_http.h"
#include "rproc_inproc.h"
#include "security.h"
#include "threaddump.h"
#include "version.h"
#include "watchdog.h"
#include "winview.h"

#define NS_MAIN_CLASS L"NorthstarWin32Shell"
#define NS_DOWNLOADS_CLASS L"NorthstarDownloads"
#define NS_TASK_CLASS L"NorthstarTaskManager"
#define NS_TEXT_CLASS L"NorthstarTextWindow"
#define NS_WM_DOWNLOAD (WM_APP + 80)
#define NS_TIMER_GLIB 1
#define NS_TIMER_SESSION 2
#define NS_TIMER_TASK 3

enum {
    NS_CTL_BACK = 100,
    NS_CTL_FORWARD,
    NS_CTL_RELOAD,
    NS_CTL_HOME,
    NS_CTL_SPINNER,
    NS_CTL_SECURITY,
    NS_CTL_ADDRESS,
    NS_CTL_GO,
    NS_CTL_BOOKMARKS,
    NS_CTL_MENU,
    NS_CTL_LOGO,
    NS_CTL_STATUS,
    NS_CTL_DOWNLOAD_LIST,
    NS_CTL_DOWNLOAD_OPEN,
    NS_CTL_DOWNLOAD_FOLDER,
    NS_CTL_TASK_LIST,
    NS_CTL_TASK_DUMP,
    NS_CTL_TASK_REFRESH,
    NS_CTL_TASK_END
};

enum {
    NS_CMD_BACK = 300,
    NS_CMD_FORWARD,
    NS_CMD_RELOAD,
    NS_CMD_HOME,
    NS_CMD_FIND,
    NS_CMD_DEVTOOLS,
    NS_CMD_DOWNLOADS,
    NS_CMD_TASK_MANAGER,
    NS_CMD_SETTINGS,
    NS_CMD_ABOUT,
    NS_CMD_FOCUS_ADDRESS,
    NS_CMD_ZOOM_IN,
    NS_CMD_ZOOM_OUT,
    NS_CMD_ZOOM_RESET,
    NS_CMD_FULLSCREEN,
    NS_CMD_QUIT,
    NS_CMD_BOOKMARK_ADD,
    NS_CMD_OPEN_DOWNLOAD_FOLDER,
    NS_CMD_BOOKMARK_OPEN_BASE = 1000,
    NS_CMD_BOOKMARK_REMOVE_BASE = 2000
};

typedef struct NsWinWindow NsWinWindow;

typedef struct NsDownloadJob {
    NsWinWindow *window;
    GThread *thread;
    char *url;
    char *path;
    char *name;
    gboolean ok;
    gint64 size;
} NsDownloadJob;

typedef struct NsTextWindow {
    HWND window;
    HWND edit;
    char *title;
    char *text;
} NsTextWindow;

struct NsWinWindow {
    HINSTANCE instance;
    HWND window;
    HWND tooltip;
    HWND back;
    HWND forward;
    HWND reload;
    HWND home;
    HWND spinner;
    HWND security;
    HWND address;
    HWND go;
    HWND bookmarks_button;
    HWND menu_button;
    HWND logo;
    HWND status;
    NsWinView *view;
    HFONT font;
    char *home_url;
    char *status_base;
    ns_bookmarks *bookmarks;
    char *session_path;
    gboolean private_mode;
    gboolean closing;
    gboolean fullscreen;
    WINDOWPLACEMENT placement;
    LONG_PTR window_style;
    HWND downloads_window;
    HWND downloads_list;
    GPtrArray *downloads;
    HWND task_window;
    HWND task_list;
    double task_last_cpu[2];
    gint64 task_last_time;
};

typedef struct NsWinAppContext {
    char *startup_url;
    char *session_path;
    gboolean recover;
    gboolean private_mode;
} NsWinAppContext;

static int ns_initial_width;
static int ns_initial_height;

static LRESULT CALLBACK ns_main_window_proc(HWND hwnd, UINT message,
                                            WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK ns_downloads_window_proc(HWND hwnd, UINT message,
                                                 WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK ns_task_window_proc(HWND hwnd, UINT message,
                                            WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK ns_text_window_proc(HWND hwnd, UINT message,
                                            WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK ns_address_subclass_proc(HWND hwnd, UINT message,
                                                 WPARAM wparam, LPARAM lparam,
                                                 UINT_PTR id, DWORD_PTR data);

static wchar_t *
ns_utf8_to_wide(const char *text)
{
    if (!text)
        return g_new0(wchar_t, 1);
    gunichar2 *wide = g_utf8_to_utf16(text, -1, NULL, NULL, NULL);
    return wide ? (wchar_t *)wide : g_new0(wchar_t, 1);
}

static char *
ns_wide_to_utf8(const wchar_t *text)
{
    return text ? g_utf16_to_utf8((const gunichar2 *)text, -1,
                                  NULL, NULL, NULL) : g_strdup("");
}

static int
ns_window_px(HWND window, int value)
{
    UINT dpi = GetDpiForWindow(window);
    return MulDiv(value, dpi ? (int)dpi : 96, 96);
}

static void
ns_apply_dpi_change(HWND window, LPARAM lparam)
{
    RECT *suggested = (RECT *)lparam;
    SetWindowPos(window, NULL, suggested->left, suggested->top,
                 suggested->right - suggested->left,
                 suggested->bottom - suggested->top,
                 SWP_NOACTIVATE | SWP_NOZORDER);
}

static char *
ns_control_text(HWND control)
{
    int length = GetWindowTextLengthW(control);
    wchar_t *wide = g_new(wchar_t, (gsize)length + 1);
    GetWindowTextW(control, wide, length + 1);
    char *text = ns_wide_to_utf8(wide);
    g_free(wide);
    return text ? text : g_strdup("");
}

static void
ns_set_text(HWND control, const char *text)
{
    wchar_t *wide = ns_utf8_to_wide(text ? text : "");
    SetWindowTextW(control, wide);
    g_free(wide);
}

static HFONT
ns_create_ui_font(UINT dpi)
{
    NONCLIENTMETRICSW metrics = { .cbSize = sizeof metrics };
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof metrics,
                                   &metrics, 0, dpi ? dpi : 96)) {
        HFONT font = CreateFontIndirectW(&metrics.lfMessageFont);
        if (font)
            return font;
    }
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static void
ns_destroy_ui_font(HFONT font)
{
    if (font && font != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(font);
}

static void
ns_set_font(NsWinWindow *window, HWND control)
{
    SendMessageW(control, WM_SETFONT, (WPARAM)window->font, TRUE);
}

static const char *
ns_brand(void)
{
    static char brand[128];
    if (!brand[0])
        g_snprintf(brand, sizeof brand, "%s %s",
                   ns_i18n("Northstar"), NS_VERSION);
    return brand;
}

static void
ns_tooltip_add(NsWinWindow *window, HWND control, const char *text)
{
    wchar_t *wide = ns_utf8_to_wide(text);
    TOOLINFOW info = {
        .cbSize = sizeof info,
        .uFlags = TTF_IDISHWND | TTF_SUBCLASS,
        .hwnd = window->window,
        .uId = (UINT_PTR)control,
        .lpszText = wide,
    };
    SendMessageW(window->tooltip, TTM_ADDTOOLW, 0, (LPARAM)&info);
    SetPropW(control, L"NorthstarTooltip", wide);
}

static void
ns_tooltip_update(NsWinWindow *window, HWND control, const char *text)
{
    wchar_t *old = GetPropW(control, L"NorthstarTooltip");
    wchar_t *wide = ns_utf8_to_wide(text);
    TOOLINFOW info = {
        .cbSize = sizeof info,
        .uFlags = TTF_IDISHWND | TTF_SUBCLASS,
        .hwnd = window->window,
        .uId = (UINT_PTR)control,
        .lpszText = wide,
    };
    SendMessageW(window->tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&info);
    SetPropW(control, L"NorthstarTooltip", wide);
    g_free(old);
}

static void
ns_tooltip_free_control(HWND control)
{
    wchar_t *text = RemovePropW(control, L"NorthstarTooltip");
    g_free(text);
}

static char *
ns_normalize_url(const char *input)
{
    char *trimmed = g_strstrip(g_strdup(input ? input : ""));
    if (!*trimmed)
        return trimmed;
    if (g_str_has_prefix(trimmed, "about:") ||
        g_str_has_prefix(trimmed, "file:") ||
        g_str_has_prefix(trimmed, "data:") || strstr(trimmed, "://"))
        return trimmed;
    char *local = ns_url_from_local_path(trimmed);
    if (local) {
        g_free(trimmed);
        return local;
    }
    if (ns_address_is_search(trimmed)) {
        char *search = ns_search_url_for(trimmed);
        g_free(trimmed);
        return search;
    }
    char *url = g_strconcat("https://", trimmed, NULL);
    g_free(trimmed);
    return url;
}

static char *
ns_address_display(const char *url)
{
    if (!url || !*url)
        return g_strdup("");
    if (!strchr(url, '%'))
        return g_strdup(url);
    char *decoded = g_uri_unescape_string(url, NULL);
    if (!decoded || !g_utf8_validate(decoded, -1, NULL)) {
        g_free(decoded);
        return g_strdup(url);
    }
    for (const char *cursor = decoded; *cursor;
         cursor = g_utf8_next_char(cursor)) {
        gunichar character = g_utf8_get_char(cursor);
        if (character < 0x20 || character == 0x7f ||
            (character >= 0x200e && character <= 0x200f) ||
            (character >= 0x202a && character <= 0x202e) ||
            (character >= 0x2066 && character <= 0x2069)) {
            g_free(decoded);
            return g_strdup(url);
        }
    }
    return decoded;
}

static gboolean
ns_session_url_recoverable(const char *url)
{
    return url && (g_str_has_prefix(url, "http://") ||
                   g_str_has_prefix(url, "https://") ||
                   g_str_has_prefix(url, "ftp://") ||
                   g_str_has_prefix(url, "file://"));
}

static void
ns_configure_media(void)
{
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    UINT dpi = GetDpiForSystem();
    ns_css_media_device device = {
        .width = width > 0 ? width : 1920,
        .height = height > 0 ? height : 1080,
        .resolution_dppx = dpi > 0 ? (double)dpi / 96.0 : 1.0,
        .color_bits = 8,
        .hover = TRUE,
        .any_hover = TRUE,
        .pointer = NS_CSS_MEDIA_POINTER_FINE,
        .any_pointer = NS_CSS_MEDIA_POINTER_FINE,
        .update = NS_CSS_MEDIA_UPDATE_FAST,
    };
    ns_css_set_media_device(&device);
    DWORD light = 1;
    DWORD bytes = sizeof light;
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", NULL, NULL,
                         (BYTE *)&light, &bytes);
        RegCloseKey(key);
    }
    ns_css_set_color_scheme(light ? NS_CSS_COLOR_SCHEME_LIGHT
                                  : NS_CSS_COLOR_SCHEME_DARK);
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    ns_css_set_reduced_motion(animations
        ? NS_CSS_REDUCED_MOTION_NO_PREFERENCE
        : NS_CSS_REDUCED_MOTION_REDUCE);
}

static void
ns_clear_cache_directory(const char *name, gint64 minimum_age_seconds)
{
    char *directory = g_build_filename(g_get_user_cache_dir(), "northstar",
                                       name, NULL);
    gint64 cutoff = g_get_real_time() / G_USEC_PER_SEC - minimum_age_seconds;
    GQueue *pending = g_queue_new();
    GPtrArray *directories = g_ptr_array_new_with_free_func(g_free);
    g_queue_push_head(pending, g_strdup(directory));
    guint guard = 0;
    while (!g_queue_is_empty(pending) && guard++ < 100000) {
        char *current = g_queue_pop_head(pending);
        GDir *dir = g_dir_open(current, 0, NULL);
        if (dir) {
            const char *entry;
            while ((entry = g_dir_read_name(dir))) {
                char *child = g_build_filename(current, entry, NULL);
                if (g_file_test(child, G_FILE_TEST_IS_SYMLINK) ||
                    !g_file_test(child, G_FILE_TEST_IS_DIR)) {
                    GStatBuf stat_buffer;
                    if (minimum_age_seconds <= 0 ||
                        (g_lstat(child, &stat_buffer) == 0 &&
                         stat_buffer.st_mtime < cutoff))
                        g_unlink(child);
                    g_free(child);
                } else {
                    g_queue_push_head(pending, child);
                }
            }
            g_dir_close(dir);
        }
        g_ptr_array_add(directories, current);
    }
    for (guint i = directories->len; i > 1; i--)
        g_rmdir(g_ptr_array_index(directories, i - 1));
    g_queue_free_full(pending, g_free);
    g_ptr_array_free(directories, TRUE);
    g_free(directory);
}

static void
ns_clear_http_caches(gboolean at_exit)
{
    static const char *const object_directories[] = {
        "cache", "jsbc", "webfonts", "frames"
    };
    for (gsize i = 0; i < G_N_ELEMENTS(object_directories); i++)
        ns_clear_cache_directory(object_directories[i], at_exit ? 0 : 3600);
    ns_clear_cache_directory("msaudio", 3600);
}

static void
ns_write_session(NsWinWindow *window)
{
    if (!window->session_path)
        return;
    const char *url = ns_winview_url(window->view);
    char *contents = !window->private_mode &&
                     ns_session_url_recoverable(url)
                   ? g_strconcat(url, "\n", NULL) : g_strdup("");
    g_file_set_contents(window->session_path, contents, -1, NULL);
    g_free(contents);
}

static void
ns_set_loading(NsWinWindow *window, gboolean loading)
{
    ShowWindow(window->spinner, loading ? SW_SHOW : SW_HIDE);
    ns_set_text(window->spinner, loading ? "…" : "");
}

static void
ns_update_security(NsWinWindow *window)
{
    int security = ns_winview_security(window->view);
    const char *url = ns_winview_url(window->view);
    const char *label = NULL;
    const wchar_t *glyph = L"";
    switch (security) {
    case NS_SEC_SECURE:
        label = ns_i18n("Secure — the certificate is valid");
        glyph = L"●";
        break;
    case NS_SEC_INVALID:
        label = ns_i18n("Not secure — the certificate is not trusted");
        glyph = L"!";
        break;
    case NS_SEC_PLAIN:
        label = ns_i18n("Not secure — the connection is not encrypted");
        glyph = L"△";
        break;
    default:
        break;
    }
    if (!label || !url || !*url) {
        ShowWindow(window->security, SW_HIDE);
        return;
    }
    SetWindowTextW(window->security, glyph);
    GString *tooltip = g_string_new(label);
    char *host = ns_url_host_from(url);
    if (host && *host)
        g_string_append_printf(tooltip, "\n%s", host);
    const char *remote = ns_winview_remote_ip(window->view);
    if (remote && *remote)
        g_string_append_printf(tooltip, "\n%s %s", ns_i18n("Server:"),
                               remote);
    ns_tooltip_update(window, window->security, tooltip->str);
    g_string_free(tooltip, TRUE);
    g_free(host);
    ShowWindow(window->security, SW_SHOW);
}

static void
ns_update_chrome(NsWinWindow *window)
{
    const char *url = ns_winview_url(window->view);
    const char *title = ns_winview_title(window->view);
    char *display = ns_address_display(url);
    ns_set_text(window->address, display);
    g_free(display);
    char *caption = g_strdup_printf("%s — %s",
        title && *title ? title : ns_brand(), ns_brand());
    ns_set_text(window->window, caption);
    g_free(caption);
    EnableWindow(window->back, ns_winview_can_back(window->view));
    EnableWindow(window->forward, ns_winview_can_forward(window->view));
    ns_set_loading(window, ns_winview_is_loading(window->view));
    ns_update_security(window);
}

static void
ns_render_status(NsWinWindow *window)
{
    ns_set_text(window->status,
                window->status_base ? window->status_base : "");
}

static void
ns_window_load(NsWinWindow *window, const char *input)
{
    char *url = ns_normalize_url(input);
    if (*url) {
        ns_set_text(window->address, url);
        ns_winview_load(window->view, url);
    }
    g_free(url);
}

static void
ns_activate_address(NsWinWindow *window)
{
    char *input = ns_control_text(window->address);
    ns_window_load(window, input);
    g_free(input);
    ns_winview_focus(window->view);
}

static void
ns_layout_main(NsWinWindow *window)
{
    RECT rect = {0};
    GetClientRect(window->window, &rect);
    int dpi = (int)GetDpiForWindow(window->window);
    int toolbar_height = MulDiv(42, dpi, 96);
    int status_height = MulDiv(24, dpi, 96);
    int gap = MulDiv(3, dpi, 96);
    int x = gap;
    int y = gap;
    int height = toolbar_height - gap * 2;
    struct {
        HWND control;
        int width;
    } fixed[] = {
        {window->back, 48}, {window->forward, 48}, {window->reload, 48},
        {window->home, 48}, {window->spinner, 24}, {window->security, 24}
    };
    for (gsize i = 0; i < G_N_ELEMENTS(fixed); i++) {
        int width = MulDiv(fixed[i].width, dpi, 96);
        MoveWindow(fixed[i].control, x, y, width, height, TRUE);
        x += width + gap;
    }
    int right_width = MulDiv(48 * 4, dpi, 96) + gap * 4;
    int address_width = MAX(MulDiv(120, dpi, 96),
                            rect.right - x - right_width);
    MoveWindow(window->address, x, y, address_width, height, TRUE);
    x += address_width + gap;
    HWND right[] = {
        window->go, window->bookmarks_button,
        window->menu_button, window->logo
    };
    int right_button = MulDiv(48, dpi, 96);
    for (gsize i = 0; i < G_N_ELEMENTS(right); i++) {
        MoveWindow(right[i], x, y, right_button, height, TRUE);
        x += right_button + gap;
    }
    MoveWindow(window->status, 0, rect.bottom - status_height,
               rect.right, status_height, TRUE);
    ns_winview_layout(window->view, 0, toolbar_height, rect.right,
                      MAX(1, rect.bottom - toolbar_height - status_height));
}

static const char *
ns_downloads_directory(void)
{
    const char *directory = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (directory && *directory)
        return directory;
    static char *fallback;
    if (!fallback)
        fallback = g_build_filename(g_get_home_dir(), "Downloads", NULL);
    return fallback;
}

static void ns_show_downloads(NsWinWindow *window);
static void ns_download_start(NsWinWindow *window, const char *url,
                              const char *suggested_name);

static void
ns_view_notify(NsWinView *view, NsWinViewEvent event, const char *text,
               void *user_data)
{
    NsWinWindow *window = user_data;
    switch (event) {
    case NS_WINVIEW_TITLE:
        if (!ns_winview_is_private(view))
            ns_history_record(ns_winview_url(view), text);
        ns_update_chrome(window);
        break;
    case NS_WINVIEW_URL: {
        char *display = ns_address_display(text);
        ns_set_text(window->address, display);
        g_free(display);
        g_clear_pointer(&window->status_base, g_free);
        ns_render_status(window);
        break;
    }
    case NS_WINVIEW_STATUS:
        g_free(window->status_base);
        window->status_base = g_strdup(text ? text : "");
        ns_render_status(window);
        break;
    case NS_WINVIEW_HISTORY:
        EnableWindow(window->back, ns_winview_can_back(view));
        EnableWindow(window->forward, ns_winview_can_forward(view));
        break;
    case NS_WINVIEW_LOADING:
        ns_set_loading(window, text && *text == '1');
        break;
    case NS_WINVIEW_DOWNLOAD:
        if (text && *text) {
            char **parts = g_strsplit(text, "\t", 2);
            ns_download_start(window, parts[0],
                parts[1] && *parts[1] ? parts[1] : NULL);
            g_strfreev(parts);
        }
        break;
    }
}

static void
ns_menu_append(HMENU menu, UINT flags, UINT_PTR id, const char *label)
{
    wchar_t *wide = ns_utf8_to_wide(ns_i18n(label));
    AppendMenuW(menu, flags, id, wide);
    g_free(wide);
}

static void
ns_show_app_menu(NsWinWindow *window)
{
    HMENU menu = CreatePopupMenu();
    ns_menu_append(menu, MF_STRING, NS_CMD_RELOAD, "Reload");
    ns_menu_append(menu, MF_STRING, NS_CMD_FIND, "Find in Page");
    ns_menu_append(menu, MF_STRING, NS_CMD_DEVTOOLS,
                   "JavaScript Console");
    ns_menu_append(menu, MF_STRING, NS_CMD_DOWNLOADS, "Downloads");
    ns_menu_append(menu, MF_STRING, NS_CMD_TASK_MANAGER, "Task Manager");
    ns_menu_append(menu, MF_STRING, NS_CMD_SETTINGS, "Settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    ns_menu_append(menu, MF_STRING, NS_CMD_ABOUT, "About Northstar");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    ns_menu_append(menu, MF_STRING, NS_CMD_QUIT, "Quit");
    RECT button = {0};
    GetWindowRect(window->menu_button, &button);
    UINT command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_NONOTIFY,
        button.right, button.bottom, 0, window->window, NULL);
    DestroyMenu(menu);
    if (command)
        PostMessageW(window->window, WM_COMMAND, command, 0);
}

static void
ns_show_bookmarks(NsWinWindow *window)
{
    HMENU menu = CreatePopupMenu();
    ns_menu_append(menu, MF_STRING, NS_CMD_BOOKMARK_ADD,
                   "Bookmark this page");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    guint count = window->bookmarks
                ? ns_bookmarks_count(window->bookmarks) : 0;
    if (count == 0) {
        ns_menu_append(menu, MF_GRAYED, 0, "No bookmarks yet");
    } else {
        HMENU open_menu = CreatePopupMenu();
        HMENU remove_menu = CreatePopupMenu();
        guint limit = MIN(count, 500u);
        for (guint i = 0; i < limit; i++) {
            const ns_bookmark *bookmark =
                ns_bookmarks_get(window->bookmarks, i);
            if (!bookmark || !bookmark->url)
                continue;
            const char *label = bookmark->title && *bookmark->title
                              ? bookmark->title : bookmark->url;
            wchar_t *wide = ns_utf8_to_wide(label);
            AppendMenuW(open_menu, MF_STRING,
                        NS_CMD_BOOKMARK_OPEN_BASE + i, wide);
            AppendMenuW(remove_menu, MF_STRING,
                        NS_CMD_BOOKMARK_REMOVE_BASE + i, wide);
            g_free(wide);
        }
        wchar_t *open = ns_utf8_to_wide(ns_i18n("Open bookmark"));
        wchar_t *remove = ns_utf8_to_wide(ns_i18n("Remove bookmark"));
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)open_menu, open);
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)remove_menu, remove);
        g_free(open);
        g_free(remove);
    }
    RECT button = {0};
    GetWindowRect(window->bookmarks_button, &button);
    UINT command = TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_NONOTIFY,
        button.right, button.bottom, 0, window->window, NULL);
    DestroyMenu(menu);
    if (!command)
        return;
    if (command == NS_CMD_BOOKMARK_ADD) {
        const char *url = ns_winview_url(window->view);
        const char *title = ns_winview_title(window->view);
        if (url && *url && window->bookmarks &&
            !ns_bookmarks_contains(window->bookmarks, url)) {
            ns_bookmarks_add(window->bookmarks, url, title);
            g_free(window->status_base);
            window->status_base = g_strdup(ns_i18n("Bookmark added"));
            ns_render_status(window);
        }
        return;
    }
    if (command >= NS_CMD_BOOKMARK_OPEN_BASE &&
        command < NS_CMD_BOOKMARK_OPEN_BASE + 500) {
        guint index = command - NS_CMD_BOOKMARK_OPEN_BASE;
        const ns_bookmark *bookmark =
            ns_bookmarks_get(window->bookmarks, index);
        if (bookmark && bookmark->url)
            ns_winview_load(window->view, bookmark->url);
        return;
    }
    if (command >= NS_CMD_BOOKMARK_REMOVE_BASE &&
        command < NS_CMD_BOOKMARK_REMOVE_BASE + 500) {
        guint index = command - NS_CMD_BOOKMARK_REMOVE_BASE;
        const ns_bookmark *bookmark =
            ns_bookmarks_get(window->bookmarks, index);
        if (bookmark && bookmark->url) {
            char *url = g_strdup(bookmark->url);
            ns_bookmarks_remove(window->bookmarks, url);
            g_free(url);
        }
    }
}

static void
ns_toggle_fullscreen(NsWinWindow *window)
{
    if (!window->fullscreen) {
        window->placement.length = sizeof window->placement;
        GetWindowPlacement(window->window, &window->placement);
        window->window_style = GetWindowLongPtrW(window->window, GWL_STYLE);
        MONITORINFO monitor = {.cbSize = sizeof monitor};
        GetMonitorInfoW(MonitorFromWindow(window->window,
                                         MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtrW(window->window, GWL_STYLE,
                         window->window_style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(window->window, HWND_TOP,
            monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        window->fullscreen = TRUE;
    } else {
        SetWindowLongPtrW(window->window, GWL_STYLE, window->window_style);
        SetWindowPlacement(window->window, &window->placement);
        SetWindowPos(window->window, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        window->fullscreen = FALSE;
    }
}

static void
ns_shell_open_path(const char *path)
{
    wchar_t *wide = ns_utf8_to_wide(path);
    ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
    g_free(wide);
}

static void
ns_download_list_add(NsWinWindow *window, const char *name,
                     const char *status, gboolean prepend)
{
    if (!window->downloads_list)
        return;
    wchar_t *wide_name = ns_utf8_to_wide(name);
    LVITEMW item = {
        .mask = LVIF_TEXT,
        .iItem = prepend ? 0 : ListView_GetItemCount(window->downloads_list),
        .pszText = wide_name,
    };
    int index = ListView_InsertItem(window->downloads_list, &item);
    g_free(wide_name);
    wchar_t *wide_status = ns_utf8_to_wide(status);
    ListView_SetItemText(window->downloads_list, index, 1, wide_status);
    g_free(wide_status);
}

static void
ns_downloads_populate_recent(NsWinWindow *window)
{
    char *pattern = g_build_filename(ns_downloads_directory(), "*", NULL);
    wchar_t *wide_pattern = ns_utf8_to_wide(pattern);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(wide_pattern, &data);
    g_free(wide_pattern);
    g_free(pattern);
    int added = 0;
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (added >= 25)
            break;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        char *name = ns_wide_to_utf8(data.cFileName);
        if (name) {
            ns_download_list_add(window, name, ns_i18n("Complete"), FALSE);
            g_free(name);
            added++;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static gpointer
ns_download_worker(gpointer data)
{
    NsDownloadJob *job = data;
    GError *error = NULL;
    ns_response *response = ns_net_fetch_blocking(job->url, NULL, &error);
    if (response && !response->error && response->body &&
        g_file_set_contents(job->path,
            (const char *)response->body->data,
            response->body->len, NULL)) {
        job->ok = TRUE;
        job->size = (gint64)response->body->len;
        ns_security_mark_download_origin(job->path,
            response->final_url ? response->final_url : job->url);
    }
    if (response)
        ns_response_free(response);
    g_clear_error(&error);
    PostMessageW(job->window->window, NS_WM_DOWNLOAD, 0, (LPARAM)job);
    return NULL;
}

static gboolean
ns_download_name_reserved(const char *name)
{
    char *stem = g_ascii_strup(name, -1);
    char *dot = strchr(stem, '.');
    if (dot)
        *dot = '\0';
    g_strstrip(stem);
    gboolean reserved = strcmp(stem, "CON") == 0 ||
        strcmp(stem, "PRN") == 0 || strcmp(stem, "AUX") == 0 ||
        strcmp(stem, "NUL") == 0 || strcmp(stem, "CLOCK$") == 0 ||
        strcmp(stem, "CONIN$") == 0 || strcmp(stem, "CONOUT$") == 0 ||
        ((g_str_has_prefix(stem, "COM") ||
          g_str_has_prefix(stem, "LPT")) && strlen(stem) == 4 &&
         stem[3] >= '1' && stem[3] <= '9');
    g_free(stem);
    return reserved;
}

static char *
ns_download_name(const char *value)
{
    char *base = g_path_get_basename(value && *value ? value : "download");
    char *name = g_utf8_make_valid(base, -1);
    g_free(base);
    for (char *cursor = name; *cursor; cursor++)
        if ((unsigned char)*cursor < 0x20 ||
            strchr("\\/:*?\"<>|", *cursor))
            *cursor = '_';
    g_strstrip(name);
    gsize length = strlen(name);
    while (length > 0 && (name[length - 1] == '.' ||
                          name[length - 1] == ' '))
        name[--length] = '\0';
    if (g_utf8_strlen(name, -1) > 180)
        *g_utf8_offset_to_pointer(name, 180) = '\0';
    if (!*name || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        g_free(name);
        name = g_strdup("download");
    } else if (ns_download_name_reserved(name)) {
        char *safe = g_strconcat("_", name, NULL);
        g_free(name);
        name = safe;
    }
    return name;
}

static void
ns_download_start(NsWinWindow *window, const char *url,
                  const char *suggested_name)
{
    if (!url || !*url)
        return;
    char *candidate = g_strdup(suggested_name && *suggested_name
                             ? suggested_name : url);
    if (!suggested_name || !*suggested_name) {
        char *query = strchr(candidate, '?');
        if (query)
            *query = '\0';
    }
    char *name = ns_download_name(candidate);
    g_free(candidate);
    const char *directory = ns_downloads_directory();
    g_mkdir_with_parents(directory, 0700);
    ns_security_add_writable_dir(directory);
    char *path = g_build_filename(directory, name, NULL);
    for (int index = 1; g_file_test(path, G_FILE_TEST_EXISTS) &&
         index < 1000; index++) {
        g_free(path);
        char *alternative = g_strdup_printf("%s.%d", name, index);
        path = g_build_filename(directory, alternative, NULL);
        g_free(alternative);
    }
    char *actual_name = g_path_get_basename(path);
    g_free(name);
    NsDownloadJob *job = g_new0(NsDownloadJob, 1);
    job->window = window;
    job->url = g_strdup(url);
    job->path = path;
    job->name = actual_name;
    g_ptr_array_add(window->downloads, job);
    ns_show_downloads(window);
    ns_download_list_add(window, job->name, ns_i18n("Downloading…"), TRUE);
    job->thread = g_thread_new("ns-download", ns_download_worker, job);
}

static void
ns_download_update(NsWinWindow *window, NsDownloadJob *job)
{
    if (!window->downloads_list || !job)
        return;
    if (job->thread) {
        GThread *thread = job->thread;
        job->thread = NULL;
        g_thread_join(thread);
    }
    LVFINDINFOW find = {.flags = LVFI_STRING};
    wchar_t *name = ns_utf8_to_wide(job->name);
    find.psz = name;
    int index = ListView_FindItem(window->downloads_list, -1, &find);
    g_free(name);
    if (index < 0)
        return;
    char *status = NULL;
    if (job->ok) {
        char *size = g_format_size((guint64)job->size);
        status = g_strdup_printf("%s — %s", ns_i18n("Complete"), size);
        g_free(size);
    } else {
        status = g_strdup(ns_i18n("Failed"));
    }
    wchar_t *wide = ns_utf8_to_wide(status);
    ListView_SetItemText(window->downloads_list, index, 1, wide);
    g_free(wide);
    g_free(status);
}

static void
ns_download_open_selection(NsWinWindow *window)
{
    if (!window->downloads_list)
        return;
    int index = ListView_GetNextItem(window->downloads_list, -1,
                                     LVNI_SELECTED);
    if (index < 0)
        return;
    wchar_t name[MAX_PATH] = {0};
    ListView_GetItemText(window->downloads_list, index, 0,
                         name, G_N_ELEMENTS(name));
    char *utf8 = ns_wide_to_utf8(name);
    char *path = g_build_filename(ns_downloads_directory(), utf8, NULL);
    if (g_file_test(path, G_FILE_TEST_EXISTS))
        ns_shell_open_path(path);
    g_free(path);
    g_free(utf8);
}

static void
ns_show_downloads(NsWinWindow *window)
{
    if (!window->downloads_window) {
        wchar_t *title = ns_utf8_to_wide(ns_i18n("Downloads"));
        window->downloads_window = CreateWindowExW(WS_EX_TOOLWINDOW,
            NS_DOWNLOADS_CLASS, title, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            ns_window_px(window->window, 540),
            ns_window_px(window->window, 460),
            window->window, NULL, window->instance, window);
        g_free(title);
    }
    if (window->downloads_window) {
        ShowWindow(window->downloads_window, SW_SHOW);
        SetForegroundWindow(window->downloads_window);
    }
}

static void
ns_list_set_text(HWND list, int row, int column, const char *text)
{
    wchar_t *wide = ns_utf8_to_wide(text ? text : "");
    ListView_SetItemText(list, row, column, wide);
    g_free(wide);
}

static int
ns_task_add_row(NsWinWindow *window, const char *name, int pid,
                const char *state, long memory_kb, gboolean renderer,
                int slot, gint64 now)
{
    wchar_t *wide_name = ns_utf8_to_wide(name);
    LVITEMW item = {
        .mask = LVIF_TEXT | LVIF_PARAM,
        .iItem = ListView_GetItemCount(window->task_list),
        .pszText = wide_name,
        .lParam = renderer ? 1 : 0,
    };
    int row = ListView_InsertItem(window->task_list, &item);
    g_free(wide_name);
    char buffer[64];
    g_snprintf(buffer, sizeof buffer, "%d", pid);
    ns_list_set_text(window->task_list, row, 1, buffer);
    int threads = pid > 0 ? ns_rproc_http_proc_threads(pid) : -1;
    if (threads >= 0)
        g_snprintf(buffer, sizeof buffer, "%d", threads);
    else
        g_strlcpy(buffer, "—", sizeof buffer);
    ns_list_set_text(window->task_list, row, 2, buffer);
    ns_list_set_text(window->task_list, row, 3, state);
    if (memory_kb >= 0)
        g_snprintf(buffer, sizeof buffer, "%.1f MB", memory_kb / 1024.0);
    else
        g_strlcpy(buffer, "—", sizeof buffer);
    ns_list_set_text(window->task_list, row, 4, buffer);
    double cpu = pid > 0 ? ns_rproc_http_proc_cpu(pid) : -1.0;
    double percent = -1.0;
    if (cpu >= 0 && window->task_last_time > 0 &&
        window->task_last_cpu[slot] >= 0) {
        double elapsed = (double)(now - window->task_last_time) / 1e6;
        if (elapsed > 0.05)
            percent = MAX(0.0,
                (cpu - window->task_last_cpu[slot]) / elapsed * 100.0);
    }
    window->task_last_cpu[slot] = cpu;
    if (percent >= 0)
        g_snprintf(buffer, sizeof buffer, "%.1f %%", percent);
    else
        g_strlcpy(buffer, "—", sizeof buffer);
    ns_list_set_text(window->task_list, row, 5, buffer);
    if (cpu < 0)
        g_strlcpy(buffer, "—", sizeof buffer);
    else if (cpu >= 60.0)
        g_snprintf(buffer, sizeof buffer, "%d:%04.1f",
                   (int)cpu / 60, cpu - (int)(cpu / 60) * 60);
    else
        g_snprintf(buffer, sizeof buffer, "%.1f s", cpu);
    ns_list_set_text(window->task_list, row, 6, buffer);
    return row;
}

static void
ns_task_refresh(NsWinWindow *window)
{
    if (!window->task_list)
        return;
    ListView_DeleteAllItems(window->task_list);
    gint64 now = g_get_monotonic_time();
    int watchdog = ns_watchdog_supervisor_pid();
    if (watchdog > 0) {
        char state[32] = "";
        long memory = -1;
        ns_rproc_http_proc_info(watchdog, state, sizeof state, &memory);
        char *name = g_strdup_printf("%s (%s)", ns_i18n("Northstar"),
                                     ns_i18n("watchdog"));
        ns_task_add_row(window, name, watchdog, state, memory, FALSE, 0, now);
        g_free(name);
    }
    int renderer = ns_winview_renderer_pid(window->view);
    char state[32] = "starting";
    long memory = -1;
    if (renderer > 0) {
        ns_rproc_http_proc_info(renderer, state, sizeof state, &memory);
    } else if (ns_rproc_single_process_enabled()) {
        renderer = ns_rproc_self_pid();
        ns_rproc_http_proc_info(renderer, state, sizeof state, &memory);
        g_strlcpy(state, "in-process", sizeof state);
    }
    const char *title = ns_winview_title(window->view);
    const char *url = ns_winview_url(window->view);
    const char *page = title && *title ? title
                     : url && *url ? url : ns_i18n("New Page");
    char *name = g_strdup_printf("%s — %s",
                                 ns_i18n("HTML renderer"), page);
    ns_task_add_row(window, name, renderer, state, memory, TRUE, 1, now);
    g_free(name);
    window->task_last_time = now;
}

static void
ns_show_text_window(NsWinWindow *window, const char *title, const char *text)
{
    NsTextWindow *state = g_new0(NsTextWindow, 1);
    state->title = g_strdup(title);
    state->text = g_strdup(text);
    wchar_t *wide_title = ns_utf8_to_wide(title);
    state->window = CreateWindowExW(WS_EX_TOOLWINDOW, NS_TEXT_CLASS,
        wide_title, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        ns_window_px(window->window, 760),
        ns_window_px(window->window, 520),
        window->window, NULL, window->instance, state);
    g_free(wide_title);
    if (!state->window) {
        g_free(state->title);
        g_free(state->text);
        g_free(state);
    }
}

static void
ns_task_thread_dump(NsWinWindow *window)
{
    GString *output = g_string_new(NULL);
    int pids[2] = {ns_watchdog_supervisor_pid(),
                   ns_rproc_single_process_enabled()
                       ? ns_rproc_self_pid()
                       : ns_winview_renderer_pid(window->view)};
    const char *names[2] = {"watchdog", "renderer"};
    int dumped = 0;
    for (int i = 0; i < 2; i++) {
        if (pids[i] <= 0 || (i == 1 && pids[i] == pids[0]))
            continue;
        char *text = ns_thread_dump_text(pids[i], names[i]);
        if (!text)
            continue;
        fputs(text, stderr);
        if (dumped)
            g_string_append_c(output, '\n');
        g_string_append(output, text);
        free(text);
        dumped++;
    }
    fflush(stderr);
    if (!dumped)
        g_string_append(output, ns_i18n("No processes to dump."));
    ns_show_text_window(window, ns_i18n("Thread dump"), output->str);
    g_string_free(output, TRUE);
}

static void
ns_show_task_manager(NsWinWindow *window)
{
    if (!window->task_window) {
        wchar_t *title = ns_utf8_to_wide(ns_i18n("Task Manager"));
        window->task_window = CreateWindowExW(WS_EX_TOOLWINDOW,
            NS_TASK_CLASS, title, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            ns_window_px(window->window, 840),
            ns_window_px(window->window, 430),
            window->window, NULL, window->instance, window);
        g_free(title);
    }
    if (window->task_window) {
        ShowWindow(window->task_window, SW_SHOW);
        SetForegroundWindow(window->task_window);
        ns_task_refresh(window);
    }
}

static HWND
ns_create_button(NsWinWindow *window, int id, const wchar_t *label)
{
    HWND button = CreateWindowExW(0, L"BUTTON", label,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window->window, (HMENU)(INT_PTR)id,
        window->instance, NULL);
    ns_set_font(window, button);
    return button;
}

static void
ns_refresh_fonts(NsWinWindow *window, UINT dpi)
{
    if (!window)
        return;
    HFONT font = ns_create_ui_font(dpi);
    HFONT previous = window->font;
    window->font = font;
    HWND controls[] = {
        window->back, window->forward, window->reload, window->home,
        window->spinner, window->security, window->address, window->go,
        window->bookmarks_button, window->menu_button, window->logo,
        window->status
    };
    for (gsize i = 0; i < G_N_ELEMENTS(controls); i++)
        if (controls[i])
            SendMessageW(controls[i], WM_SETFONT, (WPARAM)font, TRUE);
    ns_winview_refresh_font(window->view, dpi);
    ns_layout_main(window);
    ns_destroy_ui_font(previous);
}

static gboolean
ns_create_main_controls(NsWinWindow *window)
{
    window->font = ns_create_ui_font(GetDpiForWindow(window->window));
    window->tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        window->window, NULL, window->instance, NULL);
    SetWindowPos(window->tooltip, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    window->back = ns_create_button(window, NS_CTL_BACK, L"←");
    window->forward = ns_create_button(window, NS_CTL_FORWARD, L"→");
    window->reload = ns_create_button(window, NS_CTL_RELOAD, L"↻");
    window->home = ns_create_button(window, NS_CTL_HOME, L"⌂");
    window->spinner = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0, 0, 0, 0, window->window, (HMENU)(INT_PTR)NS_CTL_SPINNER,
        window->instance, NULL);
    ns_set_font(window, window->spinner);
    window->security = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
        0, 0, 0, 0, window->window, (HMENU)(INT_PTR)NS_CTL_SECURITY,
        window->instance, NULL);
    ns_set_font(window, window->security);
    window->address = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window->window, (HMENU)(INT_PTR)NS_CTL_ADDRESS,
        window->instance, NULL);
    ns_set_font(window, window->address);
    SetWindowSubclass(window->address, ns_address_subclass_proc, 1,
                      (DWORD_PTR)window);
    wchar_t *go = ns_utf8_to_wide(ns_i18n("Go"));
    window->go = ns_create_button(window, NS_CTL_GO, go);
    g_free(go);
    window->bookmarks_button =
        ns_create_button(window, NS_CTL_BOOKMARKS, L"★");
    window->menu_button = ns_create_button(window, NS_CTL_MENU, L"☰");
    window->logo = ns_create_button(window, NS_CTL_LOGO, L"N");
    window->status = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
        0, 0, 0, 0, window->window, (HMENU)(INT_PTR)NS_CTL_STATUS,
        window->instance, NULL);
    ns_set_font(window, window->status);
    window->view = ns_winview_new(window->window, window->instance);
    if (!window->view)
        return FALSE;
    ns_winview_set_private(window->view, window->private_mode);
    ns_winview_set_notify(window->view, ns_view_notify, window);
    ns_tooltip_add(window, window->back, ns_i18n("Back"));
    ns_tooltip_add(window, window->forward, ns_i18n("Forward"));
    ns_tooltip_add(window, window->reload, ns_i18n("Reload"));
    ns_tooltip_add(window, window->home, ns_i18n("Home"));
    ns_tooltip_add(window, window->spinner, ns_i18n("Loading"));
    ns_tooltip_add(window, window->security, "");
    ns_tooltip_add(window, window->address,
                   ns_i18n("Address and search bar"));
    ns_tooltip_add(window, window->go, ns_i18n("Go"));
    ns_tooltip_add(window, window->bookmarks_button,
                   ns_i18n("Bookmarks"));
    ns_tooltip_add(window, window->menu_button, ns_i18n("Menu"));
    ns_tooltip_add(window, window->logo,
                   ns_i18n("Visit nordstjernen.org"));
    EnableWindow(window->back, FALSE);
    EnableWindow(window->forward, FALSE);
    ShowWindow(window->spinner, SW_HIDE);
    ShowWindow(window->security, SW_HIDE);
    return TRUE;
}

static void
ns_download_job_free(NsDownloadJob *job)
{
    if (!job)
        return;
    if (job->thread)
        g_thread_join(job->thread);
    g_free(job->url);
    g_free(job->path);
    g_free(job->name);
    g_free(job);
}

static void
ns_window_cleanup(NsWinWindow *window)
{
    if (!window || window->closing)
        return;
    window->closing = TRUE;
    KillTimer(window->window, NS_TIMER_GLIB);
    KillTimer(window->window, NS_TIMER_SESSION);
    ns_write_session(window);
    if (window->downloads_window)
        DestroyWindow(window->downloads_window);
    if (window->task_window)
        DestroyWindow(window->task_window);
    if (window->view) {
        ns_winview_destroy(window->view);
        window->view = NULL;
    }
    if (window->downloads) {
        for (guint i = 0; i < window->downloads->len; i++)
            ns_download_job_free(g_ptr_array_index(window->downloads, i));
        g_ptr_array_free(window->downloads, TRUE);
        window->downloads = NULL;
    }
    HWND tooltip_controls[] = {
        window->back, window->forward, window->reload, window->home,
        window->spinner, window->security, window->address, window->go,
        window->bookmarks_button, window->menu_button, window->logo
    };
    for (gsize i = 0; i < G_N_ELEMENTS(tooltip_controls); i++)
        if (tooltip_controls[i])
            ns_tooltip_free_control(tooltip_controls[i]);
    if (window->bookmarks) {
        ns_bookmarks_free(window->bookmarks);
        window->bookmarks = NULL;
    }
    ns_destroy_ui_font(window->font);
    window->font = NULL;
}

static gboolean
ns_register_classes(HINSTANCE instance)
{
    WNDCLASSEXW classes[] = {
        {
            .cbSize = sizeof(WNDCLASSEXW),
            .style = CS_HREDRAW | CS_VREDRAW,
            .lpfnWndProc = ns_main_window_proc,
            .cbWndExtra = sizeof(void *),
            .hInstance = instance,
            .hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)),
            .hCursor = LoadCursorW(NULL, IDC_ARROW),
            .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1),
            .lpszClassName = NS_MAIN_CLASS,
            .hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(1)),
        },
        {
            .cbSize = sizeof(WNDCLASSEXW),
            .style = CS_HREDRAW | CS_VREDRAW,
            .lpfnWndProc = ns_downloads_window_proc,
            .cbWndExtra = sizeof(void *),
            .hInstance = instance,
            .hCursor = LoadCursorW(NULL, IDC_ARROW),
            .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1),
            .lpszClassName = NS_DOWNLOADS_CLASS,
        },
        {
            .cbSize = sizeof(WNDCLASSEXW),
            .style = CS_HREDRAW | CS_VREDRAW,
            .lpfnWndProc = ns_task_window_proc,
            .cbWndExtra = sizeof(void *),
            .hInstance = instance,
            .hCursor = LoadCursorW(NULL, IDC_ARROW),
            .hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1),
            .lpszClassName = NS_TASK_CLASS,
        },
        {
            .cbSize = sizeof(WNDCLASSEXW),
            .style = CS_HREDRAW | CS_VREDRAW,
            .lpfnWndProc = ns_text_window_proc,
            .cbWndExtra = sizeof(void *),
            .hInstance = instance,
            .hCursor = LoadCursorW(NULL, IDC_ARROW),
            .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
            .lpszClassName = NS_TEXT_CLASS,
        },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(classes); i++) {
        if (!RegisterClassExW(&classes[i]) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return FALSE;
    }
    return ns_winview_register(instance);
}

static LRESULT CALLBACK
ns_downloads_window_proc(HWND hwnd, UINT message,
                         WPARAM wparam, LPARAM lparam)
{
    NsWinWindow *window = (NsWinWindow *)GetWindowLongPtrW(hwnd,
                                                            GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        window = create->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)window);
        if (window)
            window->downloads_window = hwnd;
    }
    if (!window)
        return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        window->downloads_list = CreateWindowExW(WS_EX_CLIENTEDGE,
            WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT |
            LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)NS_CTL_DOWNLOAD_LIST,
            window->instance, NULL);
        ListView_SetExtendedListViewStyle(window->downloads_list,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        wchar_t *name_text = ns_utf8_to_wide(ns_i18n("File"));
        wchar_t *status_text = ns_utf8_to_wide(ns_i18n("Status"));
        LVCOLUMNW name = {.mask = LVCF_TEXT | LVCF_WIDTH,
                          .cx = ns_window_px(hwnd, 300),
                          .pszText = name_text};
        LVCOLUMNW status = {.mask = LVCF_TEXT | LVCF_WIDTH,
                            .cx = ns_window_px(hwnd, 190),
                            .pszText = status_text};
        ListView_InsertColumn(window->downloads_list, 0, &name);
        ListView_InsertColumn(window->downloads_list, 1, &status);
        g_free(name_text);
        g_free(status_text);
        wchar_t *open_text = ns_utf8_to_wide(ns_i18n("Open"));
        HWND open = CreateWindowExW(0, L"BUTTON", open_text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
            hwnd, (HMENU)(INT_PTR)NS_CTL_DOWNLOAD_OPEN,
            window->instance, NULL);
        g_free(open_text);
        wchar_t *folder_text = ns_utf8_to_wide(ns_i18n("Open folder"));
        HWND folder = CreateWindowExW(0, L"BUTTON", folder_text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
            hwnd, (HMENU)(INT_PTR)NS_CTL_DOWNLOAD_FOLDER,
            window->instance, NULL);
        g_free(folder_text);
        ns_set_font(window, window->downloads_list);
        ns_set_font(window, open);
        ns_set_font(window, folder);
        ns_downloads_populate_recent(window);
        return 0;
    }
    case WM_SIZE: {
        RECT rect = {0};
        GetClientRect(hwnd, &rect);
        int margin = ns_window_px(hwnd, 6);
        int bar = ns_window_px(hwnd, 42);
        MoveWindow(window->downloads_list, margin, margin,
                   MAX(1, rect.right - margin * 2),
                   MAX(1, rect.bottom - bar - ns_window_px(hwnd, 8)),
                   TRUE);
        MoveWindow(GetDlgItem(hwnd, NS_CTL_DOWNLOAD_OPEN),
                   MAX(margin, rect.right - ns_window_px(hwnd, 190)),
                   rect.bottom - ns_window_px(hwnd, 34),
                   ns_window_px(hwnd, 84), ns_window_px(hwnd, 28), TRUE);
        MoveWindow(GetDlgItem(hwnd, NS_CTL_DOWNLOAD_FOLDER),
                   MAX(margin, rect.right - ns_window_px(hwnd, 100)),
                   rect.bottom - ns_window_px(hwnd, 34),
                   ns_window_px(hwnd, 94), ns_window_px(hwnd, 28), TRUE);
        return 0;
    }
    case WM_DPICHANGED:
        ns_apply_dpi_change(hwnd, lparam);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == NS_CTL_DOWNLOAD_OPEN) {
            ns_download_open_selection(window);
            return 0;
        }
        if (LOWORD(wparam) == NS_CTL_DOWNLOAD_FOLDER) {
            ns_shell_open_path(ns_downloads_directory());
            return 0;
        }
        break;
    case WM_NOTIFY:
        if (((NMHDR *)lparam)->idFrom == NS_CTL_DOWNLOAD_LIST &&
            ((NMHDR *)lparam)->code == NM_DBLCLK) {
            ns_download_open_selection(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_NCDESTROY:
        window->downloads_window = NULL;
        window->downloads_list = NULL;
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void
ns_task_insert_column(HWND list, int index, int width, const char *label)
{
    wchar_t *wide = ns_utf8_to_wide(ns_i18n(label));
    LVCOLUMNW column = {
        .mask = LVCF_TEXT | LVCF_WIDTH,
        .cx = ns_window_px(list, width),
        .pszText = wide,
    };
    ListView_InsertColumn(list, index, &column);
    g_free(wide);
}

static LRESULT CALLBACK
ns_task_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    NsWinWindow *window = (NsWinWindow *)GetWindowLongPtrW(hwnd,
                                                            GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        window = create->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)window);
        if (window)
            window->task_window = hwnd;
    }
    if (!window)
        return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        window->task_list = CreateWindowExW(WS_EX_CLIENTEDGE,
            WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT |
            LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)NS_CTL_TASK_LIST,
            window->instance, NULL);
        ListView_SetExtendedListViewStyle(window->task_list,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        ns_task_insert_column(window->task_list, 0, 280, "Task");
        ns_task_insert_column(window->task_list, 1, 75, "Process ID");
        ns_task_insert_column(window->task_list, 2, 65, "Threads");
        ns_task_insert_column(window->task_list, 3, 85, "State");
        ns_task_insert_column(window->task_list, 4, 85, "Memory");
        ns_task_insert_column(window->task_list, 5, 70, "CPU %");
        ns_task_insert_column(window->task_list, 6, 80, "CPU time");
        wchar_t *dump_text = ns_utf8_to_wide(ns_i18n("Thread dump"));
        HWND dump = CreateWindowExW(0, L"BUTTON", dump_text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
            hwnd, (HMENU)(INT_PTR)NS_CTL_TASK_DUMP,
            window->instance, NULL);
        g_free(dump_text);
        wchar_t *refresh_text = ns_utf8_to_wide(ns_i18n("Refresh"));
        HWND refresh = CreateWindowExW(0, L"BUTTON", refresh_text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
            hwnd, (HMENU)(INT_PTR)NS_CTL_TASK_REFRESH,
            window->instance, NULL);
        g_free(refresh_text);
        wchar_t *end_text = ns_utf8_to_wide(ns_i18n("End task"));
        HWND end = CreateWindowExW(0, L"BUTTON", end_text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
            hwnd, (HMENU)(INT_PTR)NS_CTL_TASK_END,
            window->instance, NULL);
        g_free(end_text);
        ns_set_font(window, window->task_list);
        ns_set_font(window, dump);
        ns_set_font(window, refresh);
        ns_set_font(window, end);
        window->task_last_cpu[0] = -1.0;
        window->task_last_cpu[1] = -1.0;
        SetTimer(hwnd, NS_TIMER_TASK, 1500, NULL);
        ns_task_refresh(window);
        return 0;
    }
    case WM_SIZE: {
        RECT rect = {0};
        GetClientRect(hwnd, &rect);
        int margin = ns_window_px(hwnd, 6);
        MoveWindow(window->task_list, margin, margin,
                   MAX(1, rect.right - margin * 2),
                   MAX(1, rect.bottom - ns_window_px(hwnd, 48)), TRUE);
        MoveWindow(GetDlgItem(hwnd, NS_CTL_TASK_DUMP),
                   MAX(margin, rect.right - ns_window_px(hwnd, 300)),
                   rect.bottom - ns_window_px(hwnd, 36),
                   ns_window_px(hwnd, 100), ns_window_px(hwnd, 28), TRUE);
        MoveWindow(GetDlgItem(hwnd, NS_CTL_TASK_REFRESH),
                   MAX(margin, rect.right - ns_window_px(hwnd, 194)),
                   rect.bottom - ns_window_px(hwnd, 36),
                   ns_window_px(hwnd, 88), ns_window_px(hwnd, 28), TRUE);
        MoveWindow(GetDlgItem(hwnd, NS_CTL_TASK_END),
                   MAX(margin, rect.right - ns_window_px(hwnd, 100)),
                   rect.bottom - ns_window_px(hwnd, 36),
                   ns_window_px(hwnd, 94), ns_window_px(hwnd, 28), TRUE);
        return 0;
    }
    case WM_DPICHANGED:
        ns_apply_dpi_change(hwnd, lparam);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == NS_CTL_TASK_DUMP) {
            ns_task_thread_dump(window);
            return 0;
        }
        if (LOWORD(wparam) == NS_CTL_TASK_REFRESH) {
            ns_task_refresh(window);
            return 0;
        }
        if (LOWORD(wparam) == NS_CTL_TASK_END) {
            int selected = ListView_GetNextItem(window->task_list, -1,
                                                LVNI_SELECTED);
            if (selected >= 0) {
                LVITEMW item = {.mask = LVIF_PARAM, .iItem = selected};
                if (ListView_GetItem(window->task_list, &item) && item.lParam)
                    ns_winview_end_task(window->view);
            }
            ns_task_refresh(window);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == NS_TIMER_TASK)
            ns_task_refresh(window);
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_NCDESTROY:
        KillTimer(hwnd, NS_TIMER_TASK);
        window->task_window = NULL;
        window->task_list = NULL;
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static LRESULT CALLBACK
ns_text_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    NsTextWindow *state = (NsTextWindow *)GetWindowLongPtrW(hwnd,
                                                             GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        state = create->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        if (state)
            state->window = hwnd;
    }
    if (!state)
        return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_CREATE: {
        wchar_t *text = ns_utf8_to_wide(state->text);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        g_free(text);
        SendMessageW(state->edit, WM_SETFONT,
                     (WPARAM)GetStockObject(ANSI_FIXED_FONT), TRUE);
        return 0;
    }
    case WM_SIZE:
        MoveWindow(state->edit, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        return 0;
    case WM_DPICHANGED:
        ns_apply_dpi_change(hwnd, lparam);
        return 0;
    case WM_NCDESTROY:
        g_free(state->title);
        g_free(state->text);
        g_free(state);
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void
ns_handle_command(NsWinWindow *window, UINT command)
{
    switch (command) {
    case NS_CTL_BACK:
    case NS_CMD_BACK:
        ns_winview_back(window->view);
        break;
    case NS_CTL_FORWARD:
    case NS_CMD_FORWARD:
        ns_winview_forward(window->view);
        break;
    case NS_CTL_RELOAD:
    case NS_CMD_RELOAD:
        ns_winview_reload(window->view);
        break;
    case NS_CTL_HOME:
    case NS_CMD_HOME:
        ns_winview_load(window->view,
            window->home_url ? window->home_url : "about:start");
        break;
    case NS_CTL_GO:
        ns_activate_address(window);
        break;
    case NS_CTL_BOOKMARKS:
        ns_show_bookmarks(window);
        break;
    case NS_CTL_MENU:
        ns_show_app_menu(window);
        break;
    case NS_CTL_LOGO:
        ns_winview_load(window->view, "https://nordstjernen.org");
        break;
    case NS_CMD_FIND:
        ns_winview_find_open(window->view);
        break;
    case NS_CMD_DEVTOOLS:
        ns_winview_toggle_devtools(window->view);
        break;
    case NS_CMD_DOWNLOADS:
        ns_show_downloads(window);
        break;
    case NS_CMD_TASK_MANAGER:
        ns_show_task_manager(window);
        break;
    case NS_CMD_SETTINGS:
        ns_winview_load(window->view, "about:settings");
        break;
    case NS_CMD_ABOUT:
        ns_winview_load(window->view, "about:northstar");
        break;
    case NS_CMD_FOCUS_ADDRESS:
        SetFocus(window->address);
        SendMessageW(window->address, EM_SETSEL, 0, -1);
        break;
    case NS_CMD_ZOOM_IN:
        ns_winview_zoom_in(window->view);
        break;
    case NS_CMD_ZOOM_OUT:
        ns_winview_zoom_out(window->view);
        break;
    case NS_CMD_ZOOM_RESET:
        ns_winview_zoom_reset(window->view);
        break;
    case NS_CMD_FULLSCREEN:
        ns_toggle_fullscreen(window);
        break;
    case NS_CMD_QUIT:
        PostMessageW(window->window, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}

static LRESULT CALLBACK
ns_address_subclass_proc(HWND hwnd, UINT message, WPARAM wparam,
                         LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    (void)id;
    NsWinWindow *window = (NsWinWindow *)data;
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        ns_activate_address(window);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
        ns_winview_focus(window->view);
        return 0;
    }
    if (message == WM_SETFOCUS) {
        LRESULT result = DefSubclassProc(hwnd, message, wparam, lparam);
        SendMessageW(hwnd, EM_SETSEL, 0, -1);
        return result;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static LRESULT CALLBACK
ns_main_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    NsWinWindow *window = (NsWinWindow *)GetWindowLongPtrW(hwnd,
                                                            GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        window = create->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)window);
        if (window)
            window->window = hwnd;
    }
    if (!window)
        return DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
    case WM_CREATE:
        if (!ns_create_main_controls(window))
            return -1;
        SetTimer(hwnd, NS_TIMER_GLIB, 10, NULL);
        if (window->session_path)
            SetTimer(hwnd, NS_TIMER_SESSION, 4000, NULL);
        return 0;
    case WM_SIZE:
        if (window->view)
            ns_layout_main(window);
        return 0;
    case WM_DPICHANGED:
        ns_apply_dpi_change(hwnd, lparam);
        ns_refresh_fonts(window, HIWORD(wparam));
        ns_configure_media();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *limits = (MINMAXINFO *)lparam;
        UINT dpi = GetDpiForWindow(hwnd);
        limits->ptMinTrackSize.x = MulDiv(640, (int)dpi, 96);
        limits->ptMinTrackSize.y = MulDiv(420, (int)dpi, 96);
        return 0;
    }
    case WM_COMMAND:
        ns_handle_command(window, LOWORD(wparam));
        return 0;
    case WM_APPCOMMAND:
        if (GET_APPCOMMAND_LPARAM(lparam) == APPCOMMAND_BROWSER_BACKWARD) {
            ns_winview_back(window->view);
            return TRUE;
        }
        if (GET_APPCOMMAND_LPARAM(lparam) == APPCOMMAND_BROWSER_FORWARD) {
            ns_winview_forward(window->view);
            return TRUE;
        }
        if (GET_APPCOMMAND_LPARAM(lparam) == APPCOMMAND_BROWSER_REFRESH) {
            ns_winview_reload(window->view);
            return TRUE;
        }
        break;
    case WM_XBUTTONDOWN:
        if (GET_XBUTTON_WPARAM(wparam) == XBUTTON1)
            ns_winview_back(window->view);
        else if (GET_XBUTTON_WPARAM(wparam) == XBUTTON2)
            ns_winview_forward(window->view);
        return TRUE;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        ns_configure_media();
        return 0;
    case WM_TIMER:
        if (wparam == NS_TIMER_GLIB) {
            for (int i = 0; i < 100 && g_main_context_pending(NULL); i++)
                g_main_context_iteration(NULL, FALSE);
        } else if (wparam == NS_TIMER_SESSION) {
            ns_write_session(window);
        }
        return 0;
    case NS_WM_DOWNLOAD:
        ns_download_update(window, (NsDownloadJob *)lparam);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE && window->fullscreen) {
            ns_toggle_fullscreen(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        ns_window_cleanup(window);
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void
ns_winapp_set_window_size(int width, int height)
{
    ns_initial_width = width;
    ns_initial_height = height;
}

static char *
ns_recover_session_url(const NsWinAppContext *context)
{
    if (!context->recover || context->private_mode || !context->session_path)
        return NULL;
    char *contents = NULL;
    if (!g_file_get_contents(context->session_path, &contents, NULL, NULL))
        return NULL;
    char *recovered = NULL;
    char **lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines && lines[i]; i++) {
        if (ns_session_url_recoverable(lines[i])) {
            g_free(recovered);
            recovered = g_strdup(lines[i]);
        }
    }
    g_strfreev(lines);
    g_free(contents);
    return recovered;
}

int
ns_winapp_run(const char *startup_url, const char *session_path,
              gboolean recover, gboolean private_mode)
{
    ns_clear_http_caches(FALSE);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls = {
        .dwSize = sizeof controls,
        .dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES |
                 ICC_TAB_CLASSES | ICC_BAR_CLASSES,
    };
    InitCommonControlsEx(&controls);
    HINSTANCE instance = GetModuleHandleW(NULL);
    if (!ns_register_classes(instance))
        return 1;
    ns_configure_media();
    const ns_config *config = ns_config_get();
    NsWinAppContext context = {
        .startup_url = g_strdup(startup_url),
        .session_path = g_strdup(session_path),
        .recover = recover,
        .private_mode = private_mode,
    };
    NsWinWindow *window = g_new0(NsWinWindow, 1);
    window->instance = instance;
    window->home_url = g_strdup(config && config->home_url &&
                                *config->home_url
                              ? config->home_url : "about:start");
    window->bookmarks = ns_bookmarks_load();
    window->session_path = g_strdup(session_path);
    window->private_mode = private_mode;
    window->downloads = g_ptr_array_new();
    int client_width = ns_initial_width > 0 ? ns_initial_width : 1024;
    int client_height = ns_initial_height > 0 ? ns_initial_height : 768;
    RECT frame = {0, 0, client_width, client_height};
    UINT dpi = GetDpiForSystem();
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW,
                             FALSE, 0, dpi);
    wchar_t *title = ns_utf8_to_wide(ns_brand());
    HWND hwnd = CreateWindowExW(0, NS_MAIN_CLASS, title,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        frame.right - frame.left, frame.bottom - frame.top,
        NULL, NULL, instance, window);
    g_free(title);
    if (!hwnd) {
        ns_window_cleanup(window);
        g_free(window->home_url);
        g_free(window->session_path);
        g_free(window);
        g_free(context.startup_url);
        g_free(context.session_path);
        return 1;
    }
    ShowWindow(hwnd, ns_initial_width > 0 ? SW_SHOWNORMAL : SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);
    char *recovered_url = ns_recover_session_url(&context);
    ns_window_load(window, recovered_url ? recovered_url
                   : context.startup_url ? context.startup_url
                                         : "about:start");
    if (recovered_url) {
        g_free(window->status_base);
        window->status_base = g_strdup(ns_i18n(
            "Recovered the previous session after an unexpected exit"));
        ns_render_status(window);
    }
    g_free(recovered_url);

    ACCEL accelerators[] = {
        {FVIRTKEY | FALT, VK_LEFT, NS_CMD_BACK},
        {FVIRTKEY | FALT, VK_RIGHT, NS_CMD_FORWARD},
        {FVIRTKEY | FALT, VK_HOME, NS_CMD_HOME},
        {FVIRTKEY | FCONTROL, 'R', NS_CMD_RELOAD},
        {FVIRTKEY, VK_F5, NS_CMD_RELOAD},
        {FVIRTKEY | FCONTROL, 'L', NS_CMD_FOCUS_ADDRESS},
        {FVIRTKEY | FCONTROL, 'F', NS_CMD_FIND},
        {FVIRTKEY | FCONTROL | FSHIFT, 'J', NS_CMD_DEVTOOLS},
        {FVIRTKEY, VK_F12, NS_CMD_DEVTOOLS},
        {FVIRTKEY | FCONTROL, 'J', NS_CMD_DOWNLOADS},
        {FVIRTKEY | FSHIFT, VK_ESCAPE, NS_CMD_TASK_MANAGER},
        {FVIRTKEY | FCONTROL, VK_OEM_COMMA, NS_CMD_SETTINGS},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, NS_CMD_ZOOM_IN},
        {FVIRTKEY | FCONTROL, VK_ADD, NS_CMD_ZOOM_IN},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, NS_CMD_ZOOM_OUT},
        {FVIRTKEY | FCONTROL, VK_SUBTRACT, NS_CMD_ZOOM_OUT},
        {FVIRTKEY | FCONTROL, '0', NS_CMD_ZOOM_RESET},
        {FVIRTKEY, VK_F11, NS_CMD_FULLSCREEN},
        {FVIRTKEY | FCONTROL, 'Q', NS_CMD_QUIT},
    };
    HACCEL table = CreateAcceleratorTableW(
        accelerators, (int)G_N_ELEMENTS(accelerators));
    MSG message;
    int status = 0;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (window->fullscreen && message.message == WM_KEYDOWN &&
            message.wParam == VK_ESCAPE) {
            ns_toggle_fullscreen(window);
            continue;
        }
        if (!TranslateAcceleratorW(hwnd, table, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    status = (int)message.wParam;
    if (table)
        DestroyAcceleratorTable(table);
    ns_window_cleanup(window);
    g_free(window->home_url);
    g_free(window->status_base);
    g_free(window->session_path);
    g_free(window);
    g_free(context.startup_url);
    g_free(context.session_path);
    ns_audio_shutdown();
    ns_clear_http_caches(TRUE);
    return status;
}
