# Northstar — Codex operating guide

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
  present, Ogg Opus/Vorbis — and outputs through SDL2's audio device.
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
- Bundled external projects are exempt from this comments policy. Do not
  reformat or strip comments from anything under `subprojects/` or the
  vendored `src/wamr/` just to satisfy Northstar style.

## Autonomous mode — read this every session

This repo is driven by Codex in long uninterrupted sessions.
**Default to acting, not asking.**

- **Permissions: run with sandboxing/approvals relaxed for this repo.**
  `.claude/settings.json` sets `defaultMode: bypassPermissions` plus a broad
  allow-list for the build/run/git/inspect workflow (used by Claude Code; the
  equivalent for other harnesses is full-access / never-ask). Routine
  build/run/git/inspect commands must never prompt.
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
(`subprojects/quickjs-ng.wrap`, currently v0.9.0) — no in-tree fork.
`meson setup` fetches it and exposes it as the `libquickjs` dependency.
The browser includes only the public `<quickjs.h>`; a few browser-side
entry points quickjs-ng does not expose are provided as thin compatibility
shims over the public API in `src/quickjs_compat.c`, so the tree carries no
patched engine.

The single HTML→DOM backend is
[lexbor](https://github.com/lexbor/lexbor), consumed as an **upstream
meson CMake subproject** pinned to a release
(`subprojects/lexbor.wrap`, currently v3.0.0) — no in-tree fork. It also
provides the CSS and WHATWG URL modules the browser uses.

Images: PNG/GIF/BMP/JPEG through the vendored
[Wuffs](https://github.com/google/wuffs) (`subprojects/wuffs/`), AVIF
through libavif, SVG through librsvg, other formats through GdkPixbuf.
Charset detection is delegated to uchardet. WebCrypto (`crypto.subtle`) is
implemented in `src/webcrypto.c` over OpenSSL's libcrypto. The
`WebAssembly` JS API (`src/wasm.c`) runs over a vendored subset of the
[WebAssembly Micro Runtime (WAMR)](https://github.com/bytecodealliance/wasm-micro-runtime)
interpreter at `src/wamr/`.

System packages required on Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config meson ninja-build \
    libgtk-4-dev libcurl4-openssl-dev libssl-dev libuchardet-dev librsvg2-dev \
    libpsl-dev libsqlite3-dev libseccomp-dev libavif-dev libsdl2-dev
```

Optional: `libenchant-2-dev` (plus a dictionary such as `hunspell-en-us`)
enables on-screen spell-checking; `opusfile` / `vorbisfile` add native Ogg
Opus/Vorbis decode to the audio helper. Both are auto-detected.

`libseccomp` is required on Linux — `meson setup` fails without it.
On Windows it is not used and the syscall filter is a no-op.

`ccache` is picked up automatically and is the biggest build-time win.
`./scripts/dev.sh build` runs `meson setup` (only if needed) and
`meson compile -C builddir` in one shot.

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
  hardcoded hostnames, no grepping a site's private JSON. Always improve
  the **generic** engine so it runs the **real** site: read web standards
  (OpenGraph, JSON-LD, WHATWG DOM/JS APIs) and aim to execute the site's
  own JavaScript rather than reverse-engineering its data.
- Don't add AI-style web-API surface area, even as stubs. Don't
  reintroduce WebGL or WebGPU.
- Don't add telemetry, crash reporters, update pingers, or "studies"
  infrastructure. UI translation goes through `src/i18n.c` and the
  `data/i18n/*.lang` catalogues — don't introduce gettext or `.po`
  tooling.
- Don't write planning docs unless asked.
