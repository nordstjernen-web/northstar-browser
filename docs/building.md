# Building and running Northstar

Northstar builds with **meson + ninja** and a C compiler (GCC or Clang).
The primary target is Linux; macOS and Windows are also supported.

## Linux dependencies

Debian / Ubuntu:

```sh
sudo apt install build-essential pkg-config meson ninja-build cmake \
    libgtk-4-dev libcurl4-openssl-dev libssl-dev libuchardet-dev \
    libharfbuzz-dev libfribidi-dev libcairo2-dev libfontconfig-dev \
    libfreetype-dev libpsl-dev libsqlite3-dev libseccomp-dev libsdl2-dev \
    zlib1g-dev
```

Fedora / RHEL:

```sh
sudo dnf install gcc pkgconf meson ninja-build cmake gtk4-devel libcurl-devel \
    openssl-devel uchardet-devel harfbuzz-devel fribidi-devel cairo-devel \
    fontconfig-devel freetype-devel libpsl-devel sqlite-devel \
    libseccomp-devel SDL2-devel zlib-devel
```

openSUSE:

```sh
sudo zypper install gcc pkgconf meson ninja cmake gtk4-devel libcurl-devel \
    libopenssl-devel libuchardet-devel harfbuzz-devel fribidi-devel \
    cairo-devel fontconfig-devel freetype2-devel libpsl-devel sqlite3-devel \
    libseccomp-devel libSDL2-devel zlib-devel
```

`libseccomp` is required on Linux — `meson setup` fails without it. On
macOS and Windows it is unused and the syscall filter is a no-op.

**Optional, auto-detected:** `libavif-dev` (AVIF images — it pulls in a
full AV1 decoder for a format that is rare on the web, so
`-Davif=disabled` drops it), `libenchant-2-dev` (+ a dictionary such as
`hunspell-en-us`) enables on-screen spell-checking; `opusfile` /
`vorbisfile` dev packages add native Ogg Opus/Vorbis decode to the
in-process mixer. The build works without them.

## macOS dependencies

With Homebrew:

```sh
brew install meson ninja pkg-config cmake gtk4 curl openssl@3 uchardet libpsl \
    sqlite sdl2 zlib
```

Export `PKG_CONFIG_PATH="$(brew --prefix curl)/lib/pkgconfig:$(brew --prefix openssl@3)/lib/pkgconfig"`
before configuring. The macOS build uses the same setup and compile commands
shown below.

## Windows dependencies

Install MSYS2 MINGW64, then install the packages listed by
`.github/workflows/windows.yml`. Run Meson from the MINGW64 shell so its
compiler and `pkg-config` resolve the MinGW libraries.

## Build

```sh
meson setup builddir
meson compile -C builddir
./builddir/src/gtk/northstar
```

`meson setup` fetches three pinned upstream subprojects — **lexbor**
(HTML/CSS/URL parser), **quickjs-ng** (JavaScript) and **ns-pango** (text
itemization, shaping and line breaking) — and exposes them as the
`liblexbor` / `libquickjs` / `ns-pango` dependencies. WAMR, Wuffs, pl_mpeg
and minimp3 are vendored in-tree. No in-tree fork of any browser engine is
carried.

The build needs network access the first time, to clone those three. A
tarball that must build offline has to embed them; `debian/README.source`
carries the recipe.

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
| `gtk` | `auto` | Build the GTK 4 desktop shell. Disable for an engine-only build that needs no GTK 4 at all. |
| `wasm` | `auto` | Build the WebAssembly JS API over vendored WAMR. Disable on platforms WAMR does not support. |
| `audio` | `auto` | Enable in-process audio playback (needs SDL2). |
| `avif` | `auto` | Decode AVIF through libavif. Disabling drops a full AV1 decoder; AVIF images then fail to decode. |
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

## Smoke and render-test fixtures

`./scripts/dev.sh smoke` renders each fixture in `data/fixtures/` headless
and diffs it against the checked-in baseline in `data/baseline/`, reporting
drift. Text fixtures use `--dump=text`; the `geo-*` ones use
`--dump=layout`, which is text-free and fixed-size so the diff is
font-stable. After an intended change, refresh with
`./scripts/dev.sh baselines` (or `dev.sh baseline <target>` for one).

`scripts/render-tests.sh [out-dir]` serves `data/render-tests/*.html` over
a local HTTP server and renders each to a PNG for visual inspection.

Neither is an automated test suite, and this project has none by design:
the baselines catch drift, the PNGs are read by eye, and correctness is
verified by running the browser.

## Definition of done

A change is complete when it (1) compiles cleanly with no new warnings
under the configured GCC/Clang flags, (2) launches and the affected UI path
works when exercised manually or headless, and (3) is committed. See
[`../CLAUDE.md`](../CLAUDE.md) for the full contributor workflow and the
comments/scope policy.
