---
name: diagnose-rendering-regressions
description: Diagnose and fix visual, geometry, hit-testing, and viewport regressions in a browser rendering engine. Use when HTML or CSS produces incorrect styles, box trees, layout, fragmentation, paint output, coordinates, or screenshots, especially when a change has altered previously correct rendering.
---

# Diagnose Rendering Regressions

Read the repository instructions, architecture notes, build guide, and current diff. Preserve unrelated changes.

## Establish the evidence

1. Reduce the defect to deterministic local HTML when practical. Record the URL, viewport, scale, fonts, timing, configuration, and expected result.
2. Capture comparable evidence at successive pipeline stages: DOM, computed style, box or layout tree, paint output, and screenshot.
3. Compare the failing viewport with a nearby viewport when wrapping, media queries, flex, grid, or fragmentation is involved.
4. Identify the first stage that becomes incorrect. Do not compensate in a later stage for an earlier-stage defect.

## Trace the pipeline

Check these layers in order:

- Input decoding, HTML/XML parsing, and DOM construction
- Selector matching, cascade, inheritance, computed values, and animation
- Box generation, formatting-context selection, and containing blocks
- Intrinsic sizing, line breaking, flex/grid/table algorithms, positioning, and fragmentation
- Display-list construction, clipping, transforms, compositing, and rasterization
- Presentation scaling, scrolling, hit testing, and input-coordinate conversion

At each layer verify ownership of coordinates and sizes, logical versus physical axes, in-flow versus out-of-flow participation, flat-tree membership, overflow, invalidation, and zoom.

## Apply the fix

Implement the smallest standards-based correction at the first incorrect layer. Avoid site-specific behavior, hostname checks, unsupported feature expansion, and unrelated refactoring. Follow the repository's code and comment policy.

## Verify the result

Re-run the focused case with identical inputs, then check adjacent viewports and nearby layout modes. Run the repository's existing deterministic smoke cases and relevant visual fixtures. Compile and launch the application using the documented platform workflow. Update user-facing change notes when required.

Measure your change against the published head with your change alone applied. A working tree carrying someone else's edits produces a before/after comparison that credits or blames you for their work; when a comparison shows movement in an area your change cannot reach, that is the likely cause, and the way to settle it is to rebuild the head plus only your diff.

## Northstar routing

- Parse and DOM: `src/html_lexbor.c`, `src/html.c`, `src/dom.c`, `src/xml.c`
- Style: `src/css_syntax.c`, `src/css.c`, `src/css_media.c`, `src/anim.c`
- Layout: `src/layout.c`, `src/layout.h`, `src/mathml.c`
- Paint: `src/paint.c`, `src/image.c`, `src/texture.c`
- Presentation: `src/render.c`, `src/headless.c`, `src/gtk/procview.c`

Use the headless dump modes to read each stage: `--dump=dom`, `--dump=layout`, `--dump=text`, and `--dump=png:FILE`. `--dump=layout` prints the box tree with margin-box origins and content sizes and is the fastest way to find the first wrong stage.

Drive and inspect a live page with `--url=`, `--viewport=`, `--settle-ms=`, `--debug=js,net,error`, `--eval=<expr>` for a value after settling, and `--act='<action>; <action>'` for input before it. Actions include `click X,Y`, `type TEXT`, `key NAME`, `scroll`, `wait MS`, `eval JS`, and `evalfile PATH`.

Two traps in that harness. Actions are split on `;`, so inline JS in an `eval` action is silently truncated at its first statement separator — put anything longer than one expression in a file and use `evalfile`. And `type` inserts text without keyboard events, so use `key` when the page's behavior depends on `keydown`.

When layout looks wrong but the box tree looks right, read the computed values through `--eval` with `getComputedStyle`: it distinguishes a bad computed value from a bad layout of a good one.

Use `scripts/dev.sh smoke` for the deterministic baselines and `scripts/render-tests.sh` to render `data/render-tests/` for visual inspection.
