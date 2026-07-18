# Building and running Northstar

Northstar builds with **meson + ninja** and a C compiler (GCC or Clang).
The primary target is Linux; Windows is supported via MSYS2 MINGW64.

## Linux dependencies

Debian / Ubuntu:

```sh
sudo apt install build-essential pkg-config meson ninja-build \
    libgtk-4-dev libcurl4-openssl-dev libssl-dev libuchardet-dev librsvg2-dev \
    libpsl-dev libsqlite3-dev libseccomp-dev libavif-dev libsdl2-dev
```

Fedora / RHEL:

```sh
sudo dnf install gcc pkgconf meson ninja-build gtk4-devel libcurl-devel \
    openssl-devel uchardet-devel librsvg2-devel libpsl-devel sqlite-devel \
    libseccomp-devel libavif-devel SDL2-devel
```

openSUSE:

```sh
sudo zypper install gcc pkgconf meson ninja gtk4-devel libcurl-devel \
    libopenssl-devel libuchardet-devel librsvg-devel libpsl-devel sqlite3-devel \
    libseccomp-devel libavif-devel libSDL2-devel
```

`libseccomp` is required on Linux — `meson setup` fails without it. On
Windows it is unused and the syscall filter is a no-op.

**Optional, auto-detected:** `libenchant-2-dev` (+ a dictionary such as
`hunspell-en-us`) enables on-screen spell-checking; `opusfile` /
`vorbisfile` dev packages add native Ogg Opus/Vorbis decode to the audio
helper. The build works without them.

## Build

```sh
meson setup builddir
meson compile -C builddir
./builddir/src/gtk/northstar
```

`meson setup` fetches the pinned upstream subprojects **lexbor** (HTML/CSS/
URL parser) and **quickjs-ng** (JavaScript) and exposes them as the
`liblexbor` / `libquickjs` dependencies. WAMR, Wuffs, pl_mpeg and minimp3
are vendored in-tree. No in-tree fork of any engine is carried.

`./scripts/dev.sh build` runs `meson setup` (only when needed) and
`meson compile -C builddir` in one step.

## Fast iteration

`ccache` is the biggest build-time win and meson picks it up
automatically; a warm-cache rebuild drops from ~35 s to ~1 s. Install it
once (`apt install ccache` / `dnf install ccache`). Optionally use the
`lld` linker for faster final links (`CC_LD=lld meson setup builddir`).

## Meson options

| Option | Default | Effect |
|--------|---------|--------|
| `gtk` | `auto` | Build the GTK 4 desktop shell. Disable for an engine-only build. |
| `wasm` | `auto` | Build the WebAssembly JS API over vendored WAMR. |
| `audio` | `auto` | Build the `northstar-audio` playback helper (needs SDL2). |
| `ipc_experiment` | `false` | Build the HTTP-vs-binary IPC comparison benchmark. |
| `build_date` | *(configure date)* | Build-date stamp shown in the About dialog. |

Set with `-Dname=value`, e.g. `meson setup builddir -Dwasm=disabled`.

## Headless mode (scripting / testing)

The browser can render without a display, which is how the render-test
fixtures are exercised and how behaviour can be scripted:

```sh
# Dump a page to PNG
./builddir/src/gtk/northstar --headless \
    --url="https://example.com/" --dump="png:/tmp/out.png" --viewport=1000

# Evaluate JavaScript against a loaded page and print the result
./builddir/src/gtk/northstar --headless \
    --url="file:///tmp/page.html" --eval="document.title"
```

Useful flags: `--dump=png:PATH` / `--dump=layout:-` / `--dump=text:-`,
`--eval=EXPR`, `--viewport=W`, `--viewport-height=H`, `--settle-ms=N`.

Running as `root` is refused for safety; set `NS_ALLOW_ROOT=1` only in a
throwaway container. `NS_NO_SANDBOX=1` / `NS_NO_SECCOMP=1` disable the
sandbox layers for debugging — never in normal use.

## Render-test fixtures

`scripts/render-tests.sh [out-dir]` serves `data/render-tests/*.html` over
a local HTTP server and renders each to a PNG for visual inspection. These
are reference renderings for spotting regressions by eye, not automated
assertions — this project has **no automated test suite** by design. Verify
behaviour by running the browser.

## Definition of done

A change is complete when it (1) compiles cleanly with no new warnings
under the configured GCC/Clang flags, (2) launches and the affected UI path
works when exercised manually or headless, and (3) is committed. See
[`../CLAUDE.md`](../CLAUDE.md) for the full contributor workflow and the
comments/scope policy.
