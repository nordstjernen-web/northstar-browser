Northstar web browser (open source GPL edition)
==================================================

Northstar is a minimalist web browser, written from scratch in C,
focused on supporting the HTML and CSS standards. This is a pared-down
desktop build that targets **Linux** (and Windows), with a deliberately
small feature set.

**HTML Standards:** Behaviour is measured against the spec text, section
by section, not against another browser.

**Security:** the page engine runs behind a seccomp + Landlock syscall
sandbox on Linux · no JIT.

**Minimalism:** one window, one page, one process. The engine is a
compact body of C — small enough for one person to read and audit
end-to-end.

## What this edition is

This edition strips Northstar down to a single-window, single-page,
single-process desktop browser, based on the
[Nordstjernen project](https://github.com/nordstjernen-web/nordstjernen).

Audio still plays (MP3, MP2, Ogg Opus/Vorbis via the `northstar-audio`
helper), images still decode (PNG/GIF/BMP/JPEG via Wuffs, plus AVIF and
SVG), and the JavaScript, CSS, networking, WebAssembly and WebCrypto
engines are unchanged.

## Browser features

- **HTML/CSS** via the lexbor parser — modern cascade, flex, grid,
  transforms, gradients, `@keyframes`.
- **JavaScript** on the QuickJS interpreter — DOM, Shadow DOM, observer
  APIs, Canvas 2D (`Path2D`, `ImageBitmap`, `DOMMatrix`), WebCrypto
  (`crypto.subtle` over OpenSSL).
- **Custom elements** — autonomous and customized built-in elements.
- **Navigation API** — `window.navigation` for single-page routing.
- **Networking** over HTTP/2 with libcurl — HTTP/3 when the linked
  libcurl provides it — HSTS, CSP, subresource-integrity (SRI) checks,
  partitioned cookies.
- **Safe browsing** — before a top-level navigation is fetched, its host
  is checked against a local SHA-256 blocklist. The check runs entirely
  on-device.
- **Media** — images (PNG, GIF, BMP, JPEG, AVIF, SVG); audio (`<audio>`)
  plays through the `northstar-audio` helper. `<video>` lays out but
  does not decode in this edition.
- **MathML** — a minimalist presentation-MathML renderer.
- **Spell checking** — optional, via the Enchant library.
- **WebAssembly** — the full JS API over a vendored WAMR interpreter.
- **Single window / single process** — the browser shows one page in one
  window, and the page engine runs in the shell process (no per-tab
  renderer processes).
- **UI** — bookmarks, find-in-page, save-to-PDF, JS console, settings,
  headless mode.

## Build

```sh
sudo apt install build-essential pkg-config meson ninja-build \
    libgtk-4-dev libcurl4-openssl-dev libssl-dev libuchardet-dev librsvg2-dev \
    libpsl-dev libsqlite3-dev libseccomp-dev libavif-dev libsdl2-dev
meson setup builddir && meson compile -C builddir
./builddir/src/gtk/northstar
```

WAMR, Wuffs, pl_mpeg and minimp3 are vendored in-tree. lexbor and
quickjs-ng are fetched by `meson setup` as pinned upstream subprojects
(see `subprojects/*.wrap`).

## Dependencies

Northstar is an independent engine — no upstream browser code.

**Fetched at `meson setup`** (pinned upstream meson subprojects, `subprojects/*.wrap`):

| Component | Role |
|-----------|------|
| [lexbor](https://github.com/lexbor/lexbor) v3.0.0 | HTML5 → DOM parser, CSS, and the WHATWG URL module |
| [quickjs-ng](https://github.com/quickjs-ng/quickjs) v0.9.0 | JavaScript engine — no JIT |

**Vendored in-tree** (built from the main tree, no submodules):

| Component | Role |
|-----------|------|
| [WAMR](https://github.com/bytecodealliance/wasm-micro-runtime) (subset) | WebAssembly interpreter |
| [Wuffs](https://github.com/google/wuffs) v0.4 | Memory-safe image decoding — PNG, GIF, BMP, JPEG |
| [pl_mpeg](https://github.com/phoboslab/pl_mpeg) (MIT) | MP2 audio decode for the audio helper |
| [minimp3](https://github.com/lieff/minimp3) (CC0) | MP3 decode for the audio helper |

**Required system libraries:** GTK 4 (≥ 4.14), GLib/Pango/Cairo/gdk-pixbuf,
libcurl (≥ 8.5), OpenSSL (libcrypto), uchardet, libpsl, SQLite, librsvg
(≥ 2.54), libavif, SDL2, and libseccomp (Linux only).

**Optional** (auto-detected): opusfile / vorbisfile (Ogg audio for the
audio helper), Enchant (spell-checking), fontconfig / pangoft2.

## License

Northstar is free software, licensed under the **GNU General Public
License, version 3 or later** — see [LICENSE](LICENSE).

Project home: <https://nordstjernen.org> · Copyright 2026 Andreas Røsdal.
