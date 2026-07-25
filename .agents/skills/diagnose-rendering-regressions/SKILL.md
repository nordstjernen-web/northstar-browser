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

## Northstar routing

- Parse and DOM: `src/html_lexbor.c`, `src/html.c`, `src/dom.c`, `src/xml.c`
- Style: `src/css_syntax.c`, `src/css.c`, `src/css_media.c`, `src/anim.c`
- Layout: `src/layout.c`, `src/layout.h`, `src/mathml.c`
- Paint: `src/paint.c`, `src/image.c`, `src/texture.c`
- Presentation: `src/render.c`, `src/headless.c`, `src/gtk/procview.c`

Use the headless `dom`, `layout`, `text`, and `png` dump modes. Use `scripts/dev.sh smoke` and `scripts/render-tests.sh` for the existing regression surfaces.
