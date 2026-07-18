# Northstar — Claude operating guide

Northstar is a web browser written from scratch in **C**, using **GTK 4**
for the UI and **libcurl** for networking. Targets **Linux** (primary) and
**Windows**.

See `README.md` for the product vision. Northstar is the GPL /
open-source edition of the [Nordstjernen
project](https://github.com/nordstjernen-web/nordstjernen). It carries
**no upstream browser engine** — the HTML/CSS/JS/layout engine is
hand-written, not forked from Gecko, WebKit, or Blink.

## Minimalist GPL edition — scope (authoritative)

This is the minimalist desktop edition. The following are **not** part of
this codebase and must not be reintroduced without an explicit request:
tabs and the process-per-tab architecture (rendering is always
single-process, in the shell process), a per-tab renderer executable,
WebGL, WebGPU, inline video decoding and the video helpers, the local-AI
(llama.cpp) feature, the inline PDF viewer (poppler), in-tree WebP
decoding, and the Android, Java, macOS and iOS builds and the embeddable
`libnorthstar` library API. The build targets Linux (primary) and Windows;
only the `linux.yml` (gcc) and `windows.yml` CI workflows remain.

## Design constraints

- Minimalistic, compact, secure. Source should be readable and
  maintainable by a single human.
- HTML5 + modern CSS + modern JavaScript, supported pragmatically as
  far as is feasible without bloat.
- **No** AI-style web APIs, **no** WebGL, **no** WebGPU.
- **Media.** There is no inline video: a `<video>` element lays out but
  does not decode in this edition. Audio does play, through the
  unsandboxed `northstar-audio` helper (`src/audio/main.c`), which decodes
  in-tree — the vendored CC0 [minimp3](https://github.com/lieff/minimp3)
  (`src/audio/minimp3.h`) for `.mp3`, the vendored MIT
  [pl_mpeg](https://github.com/phoboslab/pl_mpeg) (`subprojects/plmpeg/`)
  for MP2, and, when the optional `opusfile`/`vorbisfile` libraries are
  present, Ogg Opus/Vorbis — and outputs through SDL2's audio device
  (WASAPI/CoreAudio/ALSA), mixing and resampling itself. The renderer
  emits `open`/`play`/`pause`/`seek`/`stop`/`loop`/`volume` commands that
  ride the render-response `X-Audio` side-channel to the shell, which
  spawns and pumps the helper (`src/gtk/procview.c`).
- Images decode in-tree: PNG, GIF, BMP and JPEG through
  [Wuffs](https://github.com/google/wuffs); AVIF through libavif; SVG
  through librsvg; any other format a GdkPixbuf loader is installed for as
  a last-resort fallback.
- UI strings are English-source and translated to the operating-system
  language at startup through the in-tree catalogue lookup (`src/i18n.c`,
  `data/i18n/*.lang`); English is the fallback for any string a catalogue
  does not cover. No gettext dependency.
- Does not phone home, does not telemeter the user.

## Comments policy

**The code is self-explaining. Don't write code comments.**

- Each source file gets exactly one short header comment at the top
  naming the file and (at most) one sentence on what it does. That's
  it.
- No inline `/* … */` or `//` comments inside functions, in struct
  declarations, around tricky branches, or anywhere else. Rename a
  variable or extract a function instead.
- No "section banner" comments (`/* ---------- helpers ---------- */`).
  Group code by file or function instead.
- No `TODO`/`FIXME`/`XXX` markers — file a real task instead.

## Autonomous mode — read this every session

This repo is driven by Claude in long uninterrupted sessions.
**Default to acting, not asking.**

- **Permissions: run in `bypassPermissions` mode.** `.claude/settings.json`
  sets `defaultMode: bypassPermissions` plus a broad allow-list for the
  build/run/git/inspect workflow, so routine commands must never prompt.
  If the session is still prompting, it was started in a more restrictive
  mode — start with `--dangerously-skip-permissions` (or pick bypass in the
  trust dialog). Don't burn turns getting individual commands approved.
- **Don't ask "do you want me to proceed?", "should I continue?",
  "ready to commit?"** — just do it. The user interrupts if they
  disagree.
- **Don't summarize after every step.** One-line status is enough.
- **Don't pause for path/file/branch confirmation when context is
  unambiguous.** Grep, pick, proceed.
- **Commit and push aggressively.** Small commits, push to
  `origin/main` as soon as a logical unit lands.
- **Run for hours.** Diagnose, fix, retry. Only stop on genuine
  external blockers. When stopping: one line on what's blocked.
- **Never ask the user to run the build.** Run it yourself.
- **Local machine is the build *and* run oracle.** The repo can be
  driven from either a Linux box (GTK 4 / libcurl / meson / clang +
  an X session at `DISPLAY=:0`) or a Windows 11 box via MSYS2
  MINGW64 (same toolchain, same meson/ninja invocation; the binary
  is `./builddir/src/gtk/northstar.exe`). Every commit must pass
  `meson compile -C builddir` locally before pushing. Smoke-launch
  the browser (in the background, then kill it) on material changes
  — that's the per-change correctness gate, not CI.
- **CI is enabled.** The Linux (gcc) and Windows workflows run on
  every push to `main` and every PR targeting `main`, plus manual
  `workflow_dispatch`. Local Linux is still the primary correctness
  gate before pushing; CI provides cross-platform sanity coverage.

## Build / verify locally

The intended build system is **meson + ninja**. From a clean checkout:

```sh
meson setup builddir
meson compile -C builddir
./builddir/src/gtk/northstar
```

The JavaScript engine is
[quickjs-ng](https://github.com/quickjs-ng/quickjs), consumed as an
**upstream meson subproject** pinned to a release
(`subprojects/quickjs-ng.wrap`, currently v0.15.1) — no in-tree fork.
`meson setup` fetches it and exposes it as the `libquickjs`
dependency. The browser includes only the public `<quickjs.h>`. A
few browser-side entry points that stock quickjs-ng does not expose —
caller/function realm lookup, module private values, an
import-attributes module loader, in-place ArrayBuffer repointing,
UTF-16 string creation, native-function marking, and
`JS_ThrowDOMException` — are provided as thin compatibility shims over
the public API in `src/quickjs_compat.c` (`src/quickjs_compat.h`), so
the tree carries no patched engine. The shims degrade gracefully
(e.g. realm lookups resolve to the current realm), which suits this
single-realm, no-JIT edition.

### HTML engine: Lexbor

The single HTML→DOM backend is
[lexbor](https://github.com/lexbor/lexbor), consumed as an **upstream
meson CMake subproject** pinned to a release
(`subprojects/lexbor.wrap`, currently v3.0.0) — no in-tree fork.
`meson setup` builds only its static library (`lexbor_static`, with
warnings suppressed as third-party code) via meson's CMake module and
exposes it as the `liblexbor` dependency. The browser uses the
standard `<lexbor/...>` headers.

### Image decoding: Wuffs

PNG, GIF, BMP, and JPEG bytes are decoded through
[Wuffs](https://github.com/google/wuffs), a memory-safe
transpiled-to-C image-decoder library. The single-file release is
vendored at `subprojects/wuffs/wuffs-v0.4.c` and built as a static
subproject. `src/image_wuffs.c::ns_image_decode_wuffs` is tried
first; it returns NULL for any other format, in which case
`src/image.c::ns_image_decode_bytes` falls back to libavif (AVIF),
then GDK-Pixbuf (for TIFF / ICO / other installed loaders) and,
last, to librsvg for SVG.

### URL parsing: lexbor URL module

The `ns_url_*` helpers in `src/net.c` route URL resolution, origin
extraction, and host extraction through `lxb_url_parse` /
`lxb_url_serialize` from lexbor's WHATWG URL module. No separate URL
library or build option — it's part of the same `liblexbor_static.a`
that the HTML parser uses.

### Charset detection: uchardet

Required dependency (Debian/Ubuntu `libuchardet-dev`,
Fedora/RHEL `uchardet-devel`). `ns_html_decode_body` hands the
response body to [uchardet](https://www.freedesktop.org/wiki/Software/uchardet/)
to identify the charset, then `g_convert`s to UTF-8. No
hand-rolled BOM / HTTP-charset / meta-charset sniffing — uchardet
handles all of that internally. The Latin-1 fallback only fires
if uchardet can't classify the bytes at all.

### Web Cryptography: OpenSSL libcrypto

Required dependency (Debian/Ubuntu `libssl-dev`, Fedora/RHEL
`openssl-devel`, openSUSE `libopenssl-devel`). `crypto.subtle` (the
WebCrypto SubtleCrypto surface) is implemented in `src/webcrypto.c`
directly over OpenSSL's EVP/`OSSL_PARAM` APIs — hashing, HMAC, AES
(GCM/CBC/CTR/KW), RSA (PKCS1/PSS/OAEP), ECDSA/ECDH (P-256/384/521),
Ed25519/X25519, PBKDF2 and HKDF. The QuickJS `CryptoKey` class and `subtle.*` argument
marshalling live in `src/js.c`. OpenSSL is already linked transitively
through libcurl's TLS backend on Linux and Windows/MSYS2; `meson`
depends on `libcrypto` explicitly so the headers resolve.

System packages required on Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config meson ninja-build \
    libgtk-4-dev libcurl4-openssl-dev libssl-dev libuchardet-dev librsvg2-dev \
    libpsl-dev libsqlite3-dev libseccomp-dev libavif-dev libsdl2-dev
```

Optional: `libenchant-2-dev` (plus a dictionary such as `hunspell-en-us`)
enables on-screen spell-checking of editable text. It is auto-detected —
the build works without it and simply does no spell-checking. The
`opusfile` / `vorbisfile` dev packages, likewise optional, add native Ogg
Opus/Vorbis decode to the audio helper.

On Fedora/RHEL:

```sh
sudo dnf install gcc pkgconf meson ninja-build gtk4-devel libcurl-devel \
    openssl-devel uchardet-devel librsvg2-devel libpsl-devel sqlite-devel \
    libseccomp-devel libavif-devel SDL2-devel
```

On openSUSE:

```sh
sudo zypper install gcc pkgconf meson ninja gtk4-devel libcurl-devel \
    libopenssl-devel libuchardet-devel librsvg-devel libpsl-devel sqlite3-devel \
    libseccomp-devel libavif-devel libSDL2-devel
```

`libseccomp` is required on Linux — `meson setup` fails without it.
On Windows it is not used and the syscall filter is a no-op.

### Fast iteration (recommended for AI/Claude loops)

`ccache` is the single biggest build-time win and meson picks it up
automatically. With `ccache` installed, a clean `meson setup builddir
&& meson compile -C builddir` drops from ~35s to ~1s once the cache
is warm — in-tree libraries (lexbor, quickjs) hit the cache
and re-link in negligible time. Install once:

```sh
sudo apt install ccache       # Debian/Ubuntu
sudo dnf install ccache       # Fedora/RHEL
```

Optionally use the `lld` linker for faster final links
(`CC_LD=lld meson setup builddir`). Not required.

`./scripts/dev.sh build` runs `meson setup` (only if needed) and
`meson compile -C builddir` in one shot — use it instead of typing
the two commands separately.

## Definition of done

A change is done when:

1. It compiles cleanly (no new warnings) with the configured GCC and
   Clang flags.
2. The browser launches and the affected UI path works manually.
3. The change is committed and pushed to `origin/main`.

Note: this project has **no automated test suite** and no plans to
add one. Verify behavior by running the browser. Don't add unit /
integration / property / fuzz tests, don't add a `tests/` directory,
don't add `meson test` targets.

## Don't

- Don't introduce Mozilla/Gecko code, WebKit code, or any other
  upstream browser engine source. The hand-written engine stays
  hand-written — Northstar is not a fork of a browser engine.
- **Don't add site-specific hacks.** No per-site rendering shims, no
  hardcoded hostnames, no grepping a site's private JSON (e.g.
  `ytInitialPlayerResponse`, `mediaDefinitions`, `ytimg`, `movie_player`).
  Always improve the **generic** engine so it runs the **real** site: read
  web standards (OpenGraph, JSON-LD, WHATWG DOM/JS APIs) and aim to execute
  the site's own JavaScript rather than reverse-engineering its data. When a
  page renders wrong, fix the engine capability it exercises — never
  special-case the host. The standards-based media metadata extractor in
  `src/html_lexbor.c` is the pattern; the deleted YouTube scraper was the
  anti-pattern.
- Don't add AI-style web-API surface area, even as stubs. Don't
  reintroduce WebGL or WebGPU.
- Don't add telemetry, crash reporters, update pingers, or "studies"
  infrastructure. UI translation goes through `src/i18n.c` and the
  `data/i18n/*.lang` catalogues — don't introduce gettext or `.po`
  tooling.
- Don't write planning docs unless asked.
