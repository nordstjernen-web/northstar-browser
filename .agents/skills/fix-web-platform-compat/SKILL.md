---
name: fix-web-platform-compat
description: Diagnose and fix Northstar web-platform compatibility gaps in HTML parsing, DOM, JavaScript bindings, events, CSSOM, forms, networking, storage, Web APIs, and polyfills. Use for WPT failures, missing or incorrect browser APIs, lifecycle differences, or standards behavior that breaks real sites.
---

# Fix Web Platform Compatibility

Read `AGENTS.md` and `docs/architecture.md`. Preserve unrelated working-tree changes.

## Establish expected behavior

1. Reduce the problem to a small page or the narrowest WPT file or subtree.
2. Read the relevant primary standard from WHATWG, W3C, TC39, or the API's authoritative specification.
3. Identify observable requirements: conversion rules, exceptions, property descriptors, ordering, event timing, identity, and side effects.
4. Reject site-specific workarounds and API stubs that only satisfy feature detection.

## Route the change

- HTML bytes, encoding, and parsing: `html.c`, `html_lexbor.c`, `xml.c`
- DOM storage and mutation: `dom.c`, `dom.h`
- QuickJS bindings and most Web APIs: `js.c`, `js_internal.h`
- Canvas, crypto, Wasm, sockets, and streams: their dedicated `src/*.c` modules
- CSS syntax, cascade, media, and CSSOM: `css_syntax.c`, `css.c`, `css_media.c`, `js.c`
- Fetch, URL, cookies, cache, and policy: `net.c`, `cache.c`, `csp.c`
- JavaScript fallbacks only when native integration is unnecessary: `data/js/polyfills.js`

Use only public QuickJS APIs or the existing thin shims in `quickjs_compat.c`. Follow nearby ownership, exception, prototype, realm, and DOM-pointer validation patterns. Do not patch fetched subprojects.

## Verify

Run the smallest targeted WPT command available through `scripts/wpt-run.sh` or `scripts/wpt-fast.sh`. Distinguish harness failures and timeouts from assertion failures. If polyfills changed, run the existing polyfill verification path through the build.

Then exercise the focused page headlessly, run `./scripts/dev.sh smoke` from a POSIX shell when available, and finish with `$build-northstar`. Do not add a new automated test framework or `tests/` directory. Update `Changelog.md` for user-visible compatibility gains.
