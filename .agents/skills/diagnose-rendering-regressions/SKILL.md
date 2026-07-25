---
name: diagnose-rendering-regressions
description: Diagnose and fix Northstar HTML/CSS rendering defects, including incorrect cascade, computed values, box generation, flex/grid/block/table layout, fragmentation, painting, hit testing, and viewport behavior. Use for visual regressions, bad geometry, incorrect wrapping or positioning, or pages that render differently after an engine change.
---

# Diagnose Rendering Regressions

Read `AGENTS.md` and `docs/architecture.md`. Preserve unrelated working-tree changes.

## Reproduce

1. Reduce the defect to deterministic local HTML when practical. Keep temporary cases under `.tmp/`; do not add an automated test suite.
2. Build with `$build-northstar` when the binary is stale.
3. Capture the same page and viewport as DOM, layout, text, and PNG with the headless binary. On Windows use `builddir/src/gtk/northstar.exe`; elsewhere use `builddir/src/gtk/northstar`.
4. Compare at least one neighboring viewport when responsiveness, wrapping, flex, grid, or media queries are involved.

Use the existing flags:

```text
--headless --url=URL --dump=dom
--headless --url=URL --dump=layout
--headless --url=URL --dump=text
--headless --url=URL --dump=png:PATH --viewport=WIDTH --viewport-height=HEIGHT
```

## Locate the defect

Trace the first incorrect stage:

- Parse or DOM: `html_lexbor.c`, `html.c`, `dom.c`, `xml.c`
- Cascade and computed values: `css_syntax.c`, `css.c`, `css_media.c`, `anim.c`
- Box construction and geometry: `layout.c`, `layout.h`, `mathml.c`
- Display list and rasterization: `paint.c`, `image.c`, `texture.c`
- Presentation or input coordinates: `render.c`, `headless.c`, `src/gtk/procview.c`

Check coordinate meaning, containing blocks, formatting contexts, logical versus physical axes, flat-tree membership, out-of-flow participation, fragmentation, overflow, and zoom before changing code.

## Fix and verify

Implement the smallest generic standards-based fix. Do not add hostname checks, site-specific shims, unsupported features, or inline code comments.

Verify the focused case, run `./scripts/dev.sh smoke` from a POSIX shell when available, render the relevant manual fixtures with `scripts/render-tests.sh`, and finish with `$build-northstar`. Update `Changelog.md` for a user-visible correction.
