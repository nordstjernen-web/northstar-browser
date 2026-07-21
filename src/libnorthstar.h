/* Northstar — internal page-engine interface for renderer and headless drivers.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_BROWSER_CORE_H
#define NS_BROWSER_CORE_H

#include <stddef.h>

typedef struct ns_browser ns_browser;

int ns_browser_init(void);

ns_browser *ns_browser_open_viewport(const char *url, int viewport_width,
                                     double viewport_height, int settle_ms);
ns_browser *ns_browser_open_post_viewport(const char *url, int viewport_width,
                                          double viewport_height,
                                          int settle_ms, const void *body,
                                          size_t body_len,
                                          const char *content_type);
char *ns_browser_take_post(ns_browser *browser, size_t *out_len,
                           char **out_content_type);

char *ns_browser_render_text(ns_browser *browser);
char *ns_browser_dump_dom(ns_browser *browser);
char *ns_browser_dump_layout(ns_browser *browser);
char *ns_browser_dump_performance(ns_browser *browser);
int ns_browser_render_image(ns_browser *browser, const char *path);
int ns_browser_page_size(ns_browser *browser, int *out_width, int *out_height);
int ns_browser_set_viewport(ns_browser *browser, int css_width,
                            double css_height);
int ns_browser_render_argb32(ns_browser *browser, int scroll_x, int scroll_y,
                             int width, int height, double scale,
                             unsigned char *out, int stride);

char *ns_browser_link_at(ns_browser *browser, int x, int y);
char *ns_browser_cursor_at(ns_browser *browser, int x, int y);
char *ns_browser_press(ns_browser *browser, int x, int y, int mods);
char *ns_browser_release_click(ns_browser *browser, int *out_changed);
char *ns_browser_key_full(ns_browser *browser, int kind, const char *key,
                          const char *code, int keycode, int mods,
                          int *out_prevented);
int ns_browser_focused_editable(ns_browser *browser);
char *ns_browser_focused_editable_value(ns_browser *browser,
                                        size_t *out_caret,
                                        size_t *out_anchor);
int ns_browser_set_focused_editable_selection(ns_browser *browser,
                                              size_t caret,
                                              size_t anchor);
int ns_browser_hover(ns_browser *browser, int x, int y);
int ns_browser_scroll_at(ns_browser *browser, int x, int y, int dx, int dy);
int ns_browser_scrollbar_press(ns_browser *browser, int x, int y);
int ns_browser_scrollbar_drag(ns_browser *browser, int x, int y);
void ns_browser_scrollbar_release(ns_browser *browser);
int ns_browser_drop_files(ns_browser *browser, int x, int y,
                          const char *const *paths, int n_paths);
char *ns_browser_select(ns_browser *browser, int kind, int x, int y);
int ns_browser_contextmenu(ns_browser *browser, int x, int y);

int ns_browser_find(ns_browser *browser, const char *query,
                    int case_sensitive, int direction, int from_y,
                    int *out_total, int *out_current, int *out_y);
char *ns_browser_console_drain(ns_browser *browser);
char *ns_browser_eval(ns_browser *browser, const char *src);
char *ns_browser_media_at(ns_browser *browser, int x, int y,
                          int *out_is_video, int *out_stream);
char *ns_browser_take_pending_audio(ns_browser *browser);
char *ns_browser_take_pending_nav(ns_browser *browser);
char *ns_browser_take_pending_camera(ns_browser *browser);
void ns_browser_resolve_camera(ns_browser *browser, const char *origin,
                               int allow);
char *ns_browser_take_pending_download(ns_browser *browser);

int ns_browser_tick(ns_browser *browser, int budget_ms);
int ns_browser_animating(ns_browser *browser);
char *ns_browser_title(ns_browser *browser);
char *ns_browser_url(ns_browser *browser);
void ns_browser_set_next_referrer(const char *url);
int ns_browser_security(ns_browser *browser, const char **out_ip);
int ns_browser_bfcache_eligible(ns_browser *browser);
void ns_browser_bfcache_park(ns_browser *browser);
void ns_browser_bfcache_restore(ns_browser *browser, int viewport_width,
                                double viewport_height);
int ns_browser_busy(const ns_browser *browser);
void ns_browser_close(ns_browser *browser);

#endif
