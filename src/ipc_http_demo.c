/* Nordstjernen — demo driver for the HTTP-control + shm-frame renderer IPC.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "rproc_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned
checksum(const unsigned char *p, int stride, int w, int h)
{
    unsigned sum = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w * 4; x++)
            sum = sum * 31u + p[(size_t)y * stride + x];
    return sum;
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: ipc-http-demo <http-renderer-path>\n");
        return 2;
    }
    const char *path = argv[1];
    const char *url =
        "data:text/html,<h1>HTTP IPC demo</h1>"
        "<p><a href='https://example.com/page'>a link</a> in a paragraph.</p>"
        "<script>console.log('hello from the page');window.answer=42;</script>";

    printf("1. spawn renderer (HTTP control + shm frames): %s\n", path);
    ns_rproc_http *r = ns_rproc_http_spawn_shm(path, 1280, 800);
    if (!r) {
        fprintf(stderr, "   spawn failed\n");
        return 1;
    }

    printf("2. POST /open\n");
    ns_rproc_http_page pg;
    if (ns_rproc_http_open(r, url, 1280, 800, 0, &pg) != 0 || !pg.ok) {
        fprintf(stderr, "   open failed\n");
        ns_rproc_http_close(r);
        return 1;
    }
    printf("   ok=%d  page=%dx%d  title=\"%s\"\n", pg.ok, pg.page_width,
           pg.page_height, pg.title ? pg.title : "");
    ns_rproc_http_page_clear(&pg);

    printf("3. POST /render  (pixels arrive in shared memory, not the body)\n");
    ns_rproc_http_frame fr;
    if (ns_rproc_http_render(r, 1280, 800, 0, 0, 1.0, &fr) == 0 && fr.ok)
        printf("   frame %dx%d stride=%d animating=%d  pixel-checksum=%08x\n",
               fr.width, fr.height, fr.stride, fr.animating,
               checksum(fr.pixels, fr.stride, fr.width, fr.height));
    else
        printf("   render failed\n");

    printf("4. POST /eval\n");
    char *e1 = ns_rproc_http_eval(r, "1 + 2");
    char *e2 = ns_rproc_http_eval(r, "window.answer");
    char *e3 = ns_rproc_http_eval(r, "document.title");
    printf("   1 + 2            -> %s\n", e1 ? e1 : "(null)");
    printf("   window.answer    -> %s\n", e2 ? e2 : "(null)");
    printf("   document.title   -> %s\n", e3 ? e3 : "(null)");
    free(e1);
    free(e2);
    free(e3);

    printf("5. POST /console\n");
    char *log = ns_rproc_http_console_poll(r);
    printf("   console          -> %s", log && *log ? log : "(empty)\n");
    free(log);

    printf("6. POST /link  (hit-test the page for the <a> element)\n");
    int found = 0;
    for (int y = 0; y < 160 && !found; y += 2) {
        for (int x = 0; x < 400 && !found; x += 4) {
            char *href = ns_rproc_http_link_at(r, x, y);
            if (href && *href) {
                printf("   link at (%d,%d) -> %s\n", x, y, href);
                found = 1;
            }
            free(href);
        }
    }
    if (!found)
        printf("   (no link found)\n");

    printf("7. POST /find  (\"link\")\n");
    int total = 0, current = 0, sy = 0;
    ns_rproc_http_find(r, "link", 0, 0, 0, &total, &current, &sy);
    printf("   matches=%d current=%d scroll_y=%d\n", total, current, sy);

    printf("8. POST /quit\n");
    ns_rproc_http_close(r);
    printf("done.\n");
    return 0;
}
