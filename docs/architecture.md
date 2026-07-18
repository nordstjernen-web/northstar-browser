# Northstar architecture

This document maps the Northstar codebase: the process model, the
page-load pipeline, and which source file owns which job. It describes the
**minimalist GPL edition**, which is single-process and hand-written (no
forked browser engine). For the security posture of each layer see
[`../SECURITY.md`](../SECURITY.md).

## Process model

This edition runs the engine **in one process**. There is no per-tab or
per-origin renderer process; every page shares one address space.

```
 watchdog supervisor  (watchdog.c)
    │  spawns + restarts on crash/hang
    ▼
 browser process  (src/gtk/ shell + engine, single-process)
    │   ├─ GTK 4 UI: window, tabs, omnibox, menus   (src/gtk/*.c)
    │   ├─ engine: fetch → parse → style → layout → paint
    │   └─ QuickJS runtime(s), one per page/tab
    │
    └─ spawns child: northstar-audio  (src/audio/main.c)
           <audio> decode (minimp3 / pl_mpeg / opus / vorbis) → SDL2
           driven over stdin/stdout; commands ride the X-Audio channel
```

- **Watchdog supervisor** (`watchdog.c`) — a normal GUI launch first
  becomes a tiny supervisor that runs the real shell as a child and
  restarts it on crash or hang. It initialises no network, sandbox, or
  UI. Headless/tooling modes are never supervised.
- **Browser process** — the GTK 4 shell (`src/gtk/`) hosts the engine
  directly. `ns_rproc_single_process_enable()` (`rproc_inproc.c`) wires
  the in-process render path so no renderer subprocess is spawned.
- **Audio helper** (`northstar-audio`, `src/audio/main.c`) — the only
  child process. It decodes `<audio>` in-tree and outputs through SDL2.
  `<video>` lays out but is not decoded in this edition.

A single internal HTTP/JSON request protocol (`renderer_serve.c`,
`rproc_http.c`) still describes each render as a request/response; in
single-process mode both ends live in the one process
(`rproc_inproc.c`), and the same protocol is what a headless dump drives.

## Page-load pipeline

Each navigation flows through these stages. The engine is synchronous
once bytes are in hand (`engine.c`, `render.c` orchestrate it; the GUI
and headless drivers share the same path).

| Stage | File(s) | Job |
|-------|---------|-----|
| 1. Fetch | `net.c`, `netutil.c`, `cache.c` | libcurl async fetch (HTTP/2, HTTP/3 when available), TLS verification, redirect clamp, response-size cap, HSTS, Alt-Svc, per-site cookie jar and HTTP cache. |
| 2. Safety gate | `safebrowsing.c`, `csp.c` | Top-level host checked against the local SHA-256 blocklist; Content-Security-Policy parsed and enforced; Subresource-Integrity verified. |
| 3. Parse | `html_lexbor.c`, `html.c`, `xml.c` | Bytes → DOM via lexbor (WHATWG HTML). `xml.c` handles XHTML/namespaced XML. Charset via uchardet. |
| 4. DOM | `dom.c` | The document tree and its mutation API, shared by layout and the JS bridge. |
| 5. Style | `css.c`, `anim.c`, `font.c` | Stylesheet parse, selector matching, the cascade, computed values. `anim.c` runs transitions and `@keyframes`; `font.c` loads `@font-face` web fonts. |
| 6. Layout | `layout.c`, `mathml.c` | Box tree and fragmentation: block/inline, flex, grid, tables, multicol, positioned boxes. `mathml.c` lays out presentation MathML. |
| 7. Paint | `paint.c`, `image.c` | Builds and rasterises the Cairo display list. `image.c` decodes images on demand. |
| 8. Present | `src/gtk/procview.c`, `headless.c` | GUI blits the surface into the GTK widget; headless dumps it to PNG or a text/layout tree. |

## JavaScript and web APIs

| Area | File(s) |
|------|---------|
| Core engine binding (QuickJS), DOM/JS bridge, most Web APIs | `js.c` |
| Compatibility shims over the public QuickJS API | `quickjs_compat.c` |
| Canvas 2D, `Path2D`, `ImageBitmap`, `DOMMatrix` | `js_canvas.c` |
| `Date`, `Intl`, `performance`, realm helpers | `js_date.c`, `js_intl.c`, `js_perf.c`, `js_realm.c` |
| `crypto.subtle` (WebCrypto over OpenSSL) | `webcrypto.c` |
| WebAssembly JS API (over vendored WAMR) | `wasm.c` |
| `WebSocket`, `EventSource` (SSE) | `ws.c`, `eventsource.c` |
| Forms: validation, serialization, submission | `forms.c` |
| Text selection on the rendered page | `selection.c` |

The DOM/JS bridge invalidates opaque node pointers on free and
re-validates them on every call, so DOM mutation cannot dangle a
JS-held handle. Pure-JS polyfills live in `data/js/polyfills.js` and are
embedded at build time (`src/meson.build`).

## Storage and state (SQLite / files)

| Concern | File |
|---------|------|
| Flat key/value configuration | `config.c` |
| HTTP cache (SQLite index + on-disk bodies) | `cache.c` |
| IndexedDB | `idb.c` |
| Browsing history | `history.c` |
| Bookmarks | `bookmarks.c` |
| Sealed secrets (PBKDF2-SHA256 + AES-256-GCM) | `secretbox.c` |

On-disk state lives under the XDG config/data/cache directories with
owner-only permissions. See [`../SECURITY.md`](../SECURITY.md) for the
partitioning and permission model.

## Images

`image.c` decodes lazily, trying decoders in order:

1. **Wuffs** (`image_wuffs.c`) — PNG, GIF, BMP, JPEG (memory-safe,
   transpiled-to-C).
2. **libavif** (`image_avif.c`) — AVIF.
3. **GdkPixbuf** — TIFF, ICO (`image_ico.c`), and any other installed
   loader.
4. **librsvg** — SVG.

## Security-relevant modules

| File | Role |
|------|------|
| `security.c` | Refuse privileged startup, Linux Landlock + seccomp sandbox, Windows process mitigations. |
| `csp.c` | Content-Security-Policy parse and enforcement. |
| `safebrowsing.c` | Local phishing/malware blocklist + interstitial. |
| `secretbox.c` | Authenticated secret sealing. |

## Third-party components

Fetched by `meson setup` as pinned subprojects: **lexbor** (HTML/CSS/URL),
**quickjs-ng** (JS), **Wuffs** (images), **pl_mpeg** (MP2 audio).
Vendored in-tree: **WAMR** (WebAssembly, `src/wamr/`), **minimp3** (MP3,
`src/audio/minimp3.h`). See [`../THIRD-PARTY-LICENSES.md`](../THIRD-PARTY-LICENSES.md).

## UI translation

UI strings are English-source and translated at startup by an in-tree
catalogue lookup (`i18n.c`, `data/i18n/*.lang`). English is the fallback
for any string a catalogue does not cover. There is no gettext dependency.
