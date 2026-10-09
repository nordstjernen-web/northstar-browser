# QuickJS or quickjs-ng

Northstar runs its JavaScript on one of two QuickJS engines, picked at
configure time with the `js_engine` option:

```sh
meson setup builddir                           # default: quickjs-ng
meson setup builddir -Djs_engine=quickjs-ng    # the same, explicitly
meson setup builddir -Djs_engine=quickjs       # Fabrice Bellard's original QuickJS
```

An existing build directory switches with
`meson configure builddir -Djs_engine=quickjs`. Only the selected engine
is fetched and built.

| | `quickjs-ng` (default) | `quickjs` |
|---|---|---|
| Source | [quickjs-ng/quickjs](https://github.com/quickjs-ng/quickjs) | [bellard/quickjs](https://github.com/bellard/quickjs), the original engine |
| Wrap | `subprojects/quickjs-ng.wrap`; a system quickjs-ng is preferred when found | `subprojects/quickjs.wrap`, always the subproject |
| Version | v0.17.0 | release 2026-06-04, pinned by commit `3d5e064` |
| Local patches | Windows link fix | `Array.prototype.sort` always calls the comparator |
| `about:northstar` | `JavaScript (quickjs-ng) 0.17.0` | `JavaScript (QuickJS) 2026-06-04` |
| CI | every workflow | none; build it locally (see below) |

Both builds carry the same Web API surface. The engine — `src/js.c` and its
satellites — is written against the quickjs-ng API; the original engine gets
that API through `src/quickjs_compat.c` instead of a second binding.

[js-engine-comparison.md](js-engine-comparison.md) measures the two builds
against each other: speed, memory, conformance and build cost.

## How the original engine is built

`subprojects/quickjs.wrap` pins the upstream repository at the commit that
made the 2026-06-04 release. Upstream ships only a Makefile, so the wrap's
`patch_directory` lays `subprojects/packagefiles/quickjs/meson.build` over
the checkout. It compiles the library objects the Makefile builds
(`quickjs.c`, `dtoa.c`, `libregexp.c`, `libunicode.c`, `cutils.c`) with the
same defines (`_GNU_SOURCE`, `CONFIG_VERSION`, `-fwrapv`, and
`__USE_MINGW_ANSI_STDIO` on Windows) into a static library. Nothing is
installed.

One source patch rides on top, named by `diff_files`:
`quickjs-sort-calls-comparator.patch`. The original `Array.prototype.sort`
skips the comparator when both values are the same, which V8,
SpiderMonkey, JavaScriptCore and quickjs-ng never do. jQuery 4's
`uniqueSort` counts on that call to spot duplicates, so on the unpatched
engine `$(a).add(a)` and `.closest()` return the same element twice.

To move to a newer release, point `revision` in the wrap at the new release
commit, run `meson subprojects update --reset quickjs`, regenerate the patch
if it no longer applies, and rerun the checks below.

## The adapter: `src/quickjs_compat.h`

Engine code includes `"quickjs_compat.h"`, never `<quickjs.h>`. With
quickjs-ng it only adds the few shims upstream lacks: realm lookup,
ArrayBuffer repointing and class-ID allocation. With the original engine (`NS_QUICKJS_ORIGINAL`, set by
`meson.build`) it also supplies the quickjs-ng API the engine uses:

- **Different signatures.** `JS_IsArray`, `JS_IsError` and `JS_IsBigInt`
  take no context in quickjs-ng; `JS_NewArrayBuffer` takes a realloc-style
  callback and a maximum length. These are macros over adapter functions.
  `JS_NewContext` is wrapped so the adapter can learn the engine's class IDs
  once, from objects made in the first context. The promise-rejection
  trackers take `ns_js_bool`, which is `bool` on quickjs-ng and `JS_BOOL` on
  the original.
- **Argument padding.** The original typed-array constructor reads three
  arguments whatever `argc` says, so `JS_NewTypedArray` is wrapped to pad
  short argument lists with `undefined`.
- **quickjs-ng additions.** `JS_IsArrayBuffer` and `JS_GetTypedArrayType`
  compare the learned class IDs. `JS_NewUint8ArrayCopy`, `JS_GetUint8Array`,
  `JS_ToObject`, `JS_ThrowDOMException`, `JS_IsStrictEqual` and
  `JS_GetVersion` are rebuilt from public calls. `JS_EvalThis2` pads the
  source with newlines, because the original `JS_EvalThis` takes no starting
  line, so errors still name the right line of the HTML document; a script
  that opens with a `#!` line is evaluated unpadded.

The bytecode cache keys entries by source text only, and the two engines'
bytecode is not interchangeable, so the original engine keeps its cache in
`~/.cache/northstar/jsbc/quickjs-<version>/`.

## Known differences on the original engine

- **Language built-ins.** quickjs-ng's own additions are missing:
  `Error.captureStackTrace`, `Error.prepareStackTrace`,
  `Error.stackTraceLimit`, `Array.fromAsync`, `using` declarations with
  `DisposableStack`, `AsyncDisposableStack` and `SuppressedError`, the
  `Iterator.zip`/`zipKeyed` statics and `chunks`, `windows`, `join` and
  `includes` iterator methods, and the immutable-`ArrayBuffer` methods.
  Northstar's own code and polyfills use none of them.
- **Stored data.** The engines serialize values differently, so an
  IndexedDB written by one build reads back as empty values in the other.
- **Error positions.** Line numbers match; column numbers in stack traces
  can differ by a few characters.

## Checking a change

Build both configurations with no warnings, GCC and Clang:

```sh
meson setup builddir && meson compile -C builddir
meson setup builddir-quickjs -Djs_engine=quickjs && meson compile -C builddir-quickjs
NS_BIN=$PWD/builddir-quickjs/src/gtk/northstar ./scripts/dev.sh smoke
```

A change that calls a quickjs-ng function the original engine lacks fails
to compile in `builddir-quickjs`; add the function to
`src/quickjs_compat.c` rather than an `#ifdef` at the call site.
