---
name: fix-web-platform-compat
description: Diagnose and fix standards-compatibility gaps in a browser engine across HTML, DOM, JavaScript bindings, events, CSSOM, forms, fetch, storage, and Web APIs. Use for Web Platform Test failures, missing or incorrect APIs, lifecycle differences, or standards behavior that breaks ordinary sites.
---

# Fix Web Platform Compatibility

Read the repository instructions, architecture notes, build guide, and current diff. Preserve unrelated changes.

## Define the contract

1. Reduce the problem to a minimal page or the narrowest relevant standards test.
2. Read the authoritative WHATWG, W3C, TC39, Web IDL, or API specification. Use other browsers only as supporting evidence.
3. Write down the observable contract: conversions, exceptions, property descriptors, identity, ordering, event timing, realm behavior, security checks, and side effects.
4. Record the current result before editing. Separate harness failures and timeouts from assertion failures.

## Find the owning layer

Route the first incorrect behavior to parsing, DOM state, language binding, CSS processing, networking, storage, or a dedicated API module. Follow data and lifetime across layer boundaries instead of adding a surface-level special case.

For native language bindings, match the engine's conventions for ownership, exceptions, prototypes, descriptors, realms, callbacks, and invalidated native handles. Prefer public dependency APIs and existing compatibility shims; do not patch fetched dependencies for browser-side behavior.

## Apply the fix

Implement the smallest generic behavior required by the specification. Do not add site-specific workarounds, false feature-detection stubs, or no-op APIs that claim unsupported behavior. Keep deliberate product-scope exclusions intact.

## Verify the result

Run the original case and the smallest targeted standards-test slice. Compare before and after counts, then run nearby tests that exercise the same primitive. Exercise the behavior through the real browser path, run deterministic smoke cases, and complete the documented build and launch gates.

Aggregate pass and fail totals hide compensating movement: a change can gain subtests in one file while losing them in another and still look flat. Diff the per-test results, not the totals, and account for every file that moved in either direction.

Build the baseline from the published head with your change alone applied. Comparing against a working tree that carries unrelated edits attributes their wins and losses to you; when a suite moves in an area your change cannot influence, rebuild the head plus only your diff before drawing any conclusion.

## Northstar routing

- Parsing and encoding: `src/html.c`, `src/html_lexbor.c`, `src/xml.c`
- DOM state and mutation: `src/dom.c`, `src/dom.h`
- QuickJS bindings and most Web APIs: `src/js.c`, `src/js_internal.h`
- CSS and CSSOM: `src/css_syntax.c`, `src/css.c`, `src/css_media.c`, `src/js.c`
- Fetch, URL, cache, and policy: `src/net.c`, `src/cache.c`, `src/csp.c`
- Dedicated APIs: the corresponding `src/*.c` module
- JavaScript fallbacks: `data/js/polyfills.js`, only when native integration is unnecessary

Use `scripts/wpt-fast.sh` for scored WPT work: it serves the checkout, runs the headless `--wpt` harness in parallel, writes a `wptreport.json`, and prints per-standard scores. Point it at the checkout with `--fast-root=DIR` or `NS_WPT_FAST_ROOT` when it is not at `~/wpt-fast`, and pass paths to limit the run to the subtrees you care about. `scripts/wpt-run.sh` covers a stock WPT checkout.

Do not open WPT tests over `file://`. They load `/resources/testharness.js` from the server root, which does not resolve, and the run reports a harness timeout that looks like an engine hang. Let the script serve the checkout, or serve it yourself before pointing the browser at `http://` URLs.

Only testharness.js tests are scored; reftests and crashtests do not run under the headless harness. A visual difference therefore needs `scripts/render-tests.sh` or a layout dump, not a WPT score.

Use `scripts/dev.sh smoke` for deterministic baselines and the repository build workflow for final verification. Do not introduce a new test framework or `tests/` directory.
