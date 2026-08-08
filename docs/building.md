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

`meson setup` resolves three upstream projects through
`subprojects/*.wrap`, and exposes them as the `liblexbor` / `libquickjs` /
`ns-pango` dependencies:

| Dependency | Pinned to | Resolution |
|------------|-----------|------------|
| [lexbor](https://github.com/lexbor/lexbor) — HTML/CSS/URL parser | `v3.0.0` | A system lexbor ≥ 3.0.0 is used when `pkg-config` or CMake finds one; otherwise the wrap is cloned and its static library built through meson's CMake module. |
| [quickjs-ng](https://github.com/quickjs-ng/quickjs) — JavaScript | `v0.16.1` | System package first, the wrap as fallback. |
| [ns-pango](https://github.com/nordstjernen-web/ns-pango) — text itemization, shaping, line breaking | a commit | Always the subproject. There is no system copy to find: the fork renames every symbol precisely so it can coexist with the system Pango that GTK loads. |

WAMR, Wuffs, pl_mpeg and minimp3 are vendored in-tree and need no network.
No in-tree fork of any browser engine is carried.

So the first build needs network access for whichever of those three it
cannot satisfy locally — ns-pango always, the other two unless the system
supplies them. A tarball that must build offline has to embed them;
`debian/README.source` carries the recipe.

`./scripts/dev.sh build` runs `meson setup` (only when needed) and
`meson compile -C builddir` in one step.

## Updating a pinned dependency

Editing the `revision =` line in a wrap is not enough on its own: meson
keeps the checkout it already has. Move the pin, then reset the checkout
to it.

```sh
$EDITOR subprojects/ns-pango.wrap        # revision = <new tag or commit>
meson subprojects update --reset ns-pango
meson compile -C builddir
```

`--reset` hard-resets the subproject to the wrap's revision and works on
the shallow clones these wraps ask for, in either direction. Deleting
`subprojects/<name>/` and reconfiguring does the same thing more slowly.

Three local patches ride on top of the fetched sources, named by
`diff_files` in the wraps and living in `subprojects/packagefiles/`: one
for lexbor and two for quickjs-ng. Regenerate them against the new
sources when a pin moves, or drop one upstream has taken.

The vendored copies are updated by replacing the files. Wuffs
(`subprojects/wuffs/wuffs-v0.4.c`) and minimp3 (`src/audio/minimp3.h`) are
byte-identical to their upstream releases, so syncing either is a copy.
pl_mpeg carries one Northstar change — a bounds fix for half-pel motion
compensation, which read a row past the plane — marked in place in the
vendored header; keep it when syncing. WAMR (`src/wamr/`) is a subset of
upstream: `core/` plus `ns_wamr.c`, the narrow accessors the WebAssembly
JS API needs, which are Northstar's own.
[`../THIRD-PARTY-LICENSES.md`](../THIRD-PARTY-LICENSES.md) lists every
component and every patch, and is the file to update when any of this
moves.

Verify a dependency move the way any other change is verified: a clean
`meson compile` with no new warnings, `./scripts/dev.sh smoke` for layout
drift, and the browser launched on the paths the dependency touches. A
text-layout change wants more than that, because the shaping cache can be
wrong only for the second paragraph that shares a word with the first:
render `data/render-tests/` twice, once with `NS_PANGO_SHAPE_CACHE=0` and
once with the cache on, and require the layout dumps to be identical, then
check that `NS_PANGO_SHAPE_CACHE=verify` reports no mismatch over the same
pages.

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

Useful flags: `--dump=png:PATH` / `--dump=layout` / `--dump=text` /
`--dump=dom`, `--eval=EXPR`, `--viewport=W`, `--viewport-height=H`,
`--settle-ms=N`.

Two dump modes write PDF and differ in how much they know about paper.
`--dump=pdf:PATH` writes the page as one long unpaginated sheet, the same
output as *Save Page as PDF…*. `--dump=print:PATH` runs the print path
instead — `@media print` matching, `@page` sizing, and the break
properties cutting the document into sheets — so it is how pagination is
checked without a printer attached:

```sh
./builddir/src/gtk/northstar --headless \
    --dump="print:/tmp/paged.pdf" data/render-tests/print-pagination.html
```

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
