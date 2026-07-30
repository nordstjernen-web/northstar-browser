# HTML and CSS compliance

Where Northstar's hand-written engine stands against the HTML and CSS
specifications, how the numbers here were produced, and which gaps are
known.

Northstar carries no upstream browser engine. Every behaviour on this
page is implemented in `src/` — the parser glue in `src/html_lexbor.c`,
the cascade and value parsing in `src/css.c`, layout in `src/layout.c`,
paint in `src/paint.c`, and the DOM/CSSOM bindings in `src/js.c`. That
means compliance is not inherited from anywhere; it is measured.

## Reference specifications

- [HTML](https://html.spec.whatwg.org/) — parsing, the element
  definitions, scripting, forms.
- [DOM](https://dom.spec.whatwg.org/) — node trees, mutation, events.
- [CSSOM](https://drafts.csswg.org/cssom/) and
  [CSSOM View](https://drafts.csswg.org/cssom-view/) — `getComputedStyle`,
  `CSSStyleDeclaration`, stylesheet objects.
- [CSS Cascade](https://drafts.csswg.org/css-cascade/),
  [CSS Values](https://drafts.csswg.org/css-values/),
  [CSS Color](https://drafts.csswg.org/css-color/),
  [Selectors](https://drafts.csswg.org/selectors/).

## How the numbers are produced

The engine runs [web-platform-tests](https://github.com/web-platform-tests/wpt)
through headless mode. `--wpt` injects a completion hook before any
document script runs, waits for `testharness.js` to finish, and reports
each subtest on stdout.

```sh
git clone --depth 1 https://github.com/web-platform-tests/wpt.git ~/wpt
cd ~/wpt && ./wpt make-hosts-file | sudo tee -a /etc/hosts
cd ~/wpt && ./wpt serve &

cd northstar-browser
scripts/dev.sh build
scripts/wpt-run.sh --wpt-root=$HOME/wpt --no-serve dom/nodes css/cssom
```

`scripts/wpt-run.sh` enumerates the tests under each path, serves them,
runs each through the browser, and aggregates per-file and per-subtest
results; `--results=FILE` writes one JSON line per test for analysis.
Only `testharness.js` tests run — no reftests, no wdspec, no
`*.worker.js`, since this edition has no WebDriver and no worker
renderer process.

Two caveats when reading any of this:

- **Subtest counts move.** Fixing an early failure often lets a file
  reach subtests it previously never got to, so the denominator grows
  along with the numerator. Compare passing counts, not just percentages.
- **Some files are timing-sensitive.** A test that loads iframes and
  calls `done()` from `onload` can time out under load and report zero
  subtests. Re-run a single file before treating it as a regression.

## Current scores

Measured at `7b38d66` against a WPT checkout of 2026-07-29, 8 s per-test
timeout. "Files" counts test files where every subtest passed and the
harness reported OK.

The table predates the colour, `border-radius`, flex-baseline and CSSOM
fixes listed under "Recent fixes" below, which have not been re-measured
against it — each was verified against the behaviour it names rather
than against a WPT run. Re-run the areas before quoting these numbers as
current.

| Area | Subtests | Pass rate |
| --- | --- | --- |
| `html/dom` | 60099 / 60879 | 98.7% |
| `dom/nodes` | 11465 / 11680 | 98.2% |
| `dom/events` | 738 / 781 | 94.5% |
| `css/cssom` | 3230 / 3508 | 92.1% |
| `css/css-cascade` | 951 / 1111 | 85.6% |
| `css/selectors` | 4775 / 5749 | 83.1% |
| `html/semantics/text-level-semantics` | 27 / 38 | 71.1% |
| `html/semantics/forms` | 3270 / 4857 | 67.3% |
| `css/css-values` | 3468 / 5879 | 59.0% |
| `css/css-color` | 5219 / 11006 | 47.4% |
| `css/css-backgrounds` | 474 / 1055 | 44.9% |
| `css/css-transforms` | 310 / 705 | 44.0% |
| `html/semantics/document-metadata` | 68 / 171 | 39.8% |
| `html/semantics/scripting-1` | 1304 / 1980 | 65.9% |
| `css/css-flexbox` | 780 / 3905 | 20.0% |

Read `html/semantics/scripting-1` with its denominator in view: roughly
a quarter of its 474 files load modules or iframes over the network and
time out under load, so the total swings by tens of subtests between
runs of the same binary (1944, 1962 and 1980 across three runs). A
change smaller than about thirty subtests there is noise; the one
recorded below is an order of magnitude larger.

`html/dom` is the largest single area in WPT and the one most ordinary
pages depend on. `css/css-flexbox` is the weakest and the most
consequential for real pages — see below. `css/css-color` is nearly as
low, but almost all of its failures are the two structural gaps
described below rather than scattered bugs.

### Effect of the fixes on this branch

Same runner, same WPT checkout, the commit before this work versus this
branch. The CSS rows were taken at `7b38d66`, before the two `<script>`
fixes landed; the `scripting-1` row at `d747520`, before the NUL
follow-up that adds a further 57 subtests on one of its files. Both
"after" figures are therefore slightly conservative.

| Area | Before | After |
| --- | --- | --- |
| `html/semantics/scripting-1` | 981 | 1304 |
| `css/css-cascade` | 639 | 951 |
| `css/css-backgrounds` | 421 | 474 |
| `css/css-transforms` | 265 | 310 |
| `css/css-color` | 5156 | 5219 |
| `css/css-values` | 3445 | 3468 |
| `css/css-flexbox` | 760 | 780 |
| `css/cssom` | 3229 | 3230 |

No area regressed. `dom/nodes` is not in that table because its
denominator is not stable between runs: the before run reached 12351
subtests with 3 files timing out and the after run 11680 with 18, the
machine being busier. On the metric that does not move, files where
every subtest passed, it went from 182 to 184, and its failure count
from 213 to 209.

## Known gaps

### Flex layout is the weakest area

At 20% this is the lowest score in the table and, unlike `css/css-color`,
it is about layout rather than serialization — so it is the gap most
likely to make a real page render wrong. Simple cases are correct: a
row of `flex: 1 1 auto` / `flex: 2 1 0` / fixed-width items resolves to
the same geometry a browser produces, `flex-direction: column` with
`align-items` and `justify-content` places items correctly, and
`align-items: baseline` now aligns baselines rather than tops. The
failures are concentrated in the harder parts of the algorithm —
percentage resolution against an indefinite container, wrapping with
`align-content`, nested flex containers, and the intrinsic-size
contribution of a flex container to its parent. This deserves attention
before any further colour work.

### `getComputedStyle` does not force a style flush

Reading a computed value returns whatever the last completed style pass
produced. Everything a rule depends on that is only known after layout —
container queries, most obviously — therefore reads stale until a render
has run, even though the same rule paints correctly. A page that sets a
class and reads a computed value in the same task sees the old value.
Making the read flush style, as the CSSOM requires, is a change to how
the render pipeline is driven rather than to the CSSOM bindings.

### Computed values keep no colour space

Every colour is reduced to 8-bit sRGB as soon as it is parsed, and
`getComputedStyle` serializes it as `rgb()`/`rgba()`. CSS Color 4
requires the computed value to keep the space it was authored in, so
`lab(20 0 10)` must read back as `lab(20 0 10)` and
`color(display-p3 .1 .2 .3)` as itself. The colours *render* correctly —
the conversions to sRGB are exact — but every serialization subtest in
`css/css-color` fails. Fixing this means carrying a space-tagged colour
through the style system and the paint path, not a parser change.

### Inverse trigonometry does not produce an angle

`asin()`, `acos()`, `atan()` and `atan2()` return a plain number in
radians rather than an `<angle>`. Where the result is used directly as
an angle — `rotate(atan(1))` — the angle parser converts it and the
result is right; where it takes part in arithmetic with an angle, as in
`calc(atan(1) / 45deg)`, the calculation is rejected. Fixing it properly
means carrying a unit type through `calc()`, not a change to these four
functions.

### Experimental CSS Values features

`if()`, `random()`, `calc-mix()` and `attr()` with a type argument are
unimplemented. These are `.tentative.html` tests for features still
being specified; they account for roughly 550 subtests in
`css/css-values` and are deliberately not a priority.

### `html/semantics/document-metadata`

The low score here is dominated by files that time out rather than fail
assertions — `<link>` loading, `rel=preload`, and `referrerpolicy`
behaviours that need resource-timing signals the headless driver does
not settle on. The subtests that do run mostly pass.

## Recent fixes

Changes on this branch, each verified against the tests named:

- **Dynamic `:has()` invalidation.** The incremental restyle index used
  the final subject of a selector containing `:has()` as its mutation
  key. For `div:has(+ .test) #subject`, that indexed `#subject` instead
  of the `div` whose match actually changes. It now indexes the compound
  that owns `:has()` and invalidates its descendant and following-sibling
  dependent region. The 78 files under `css/selectors/invalidation/`
  gain 311 passing subtests with no regression; the four largest affected
  files pass all 1,029 subtests. Typical mutation flushes in the largest
  file recompute 8–10 styles while reusing the rest of the tree.
- **One colour-argument scanner, and relative colour syntax.** The six
  colour functions each carried their own argument loop and each
  accepted whatever its loop happened not to reject — `rgb(1 2 3 4 5)`,
  `rgb(0,0,0,0,0)`, `rgb(10, 20 30)`, `rgb(10 20 30, 0.5)` and
  `hsl(120 50% 50% extra)` all parsed, and a colour split over two lines
  in a stylesheet parsed as nothing at all, because the loops skipped
  only spaces. One scanner now enforces the legacy comma form and the
  modern whitespace-with-slash form as alternatives and rejects trailing
  text. `lch()` scaled a percentage chroma by 1.25, `lab()`'s factor,
  instead of 1.5. Relative colour syntax — `rgb(from <color> r g b)` and
  the `from` form of the other seven functions, with `calc()` over a
  channel keyword — is implemented by converting the origin colour into
  the destination space and substituting the channel keywords before the
  value is parsed again.
- **Unclosed functions no longer empty a style block.** CSS
  tokenization closes an open function at end of input, so
  `el.style.color = "rgb(1,2,3"` should set the colour. Northstar
  spliced the raw text into the style attribute, where the unclosed
  paren swallowed every declaration after it and the whole block was
  then thrown away as invalid.
- **`border-radius`.** Percentage radii were used as pixel counts —
  `border-radius: 50%` painted a 50-pixel corner — and a `calc()` radius
  was dropped. The elliptical forms did not work: the two-value corner
  longhand was rejected, `10px / 20px` kept only the horizontal radii,
  and through the CSSOM the declaration was rejected whole because the
  shorthand was validated against the single-length grammar of the
  legacy property.
- **Shorthands that share a name with a longhand.** `el.style.overflow =
  "hidden auto"` and `el.style.gap = "10px 20px"` silently dropped the
  declaration while the same value worked in a stylesheet. Validation
  now falls back to the declaration-block parse, which covers every
  shorthand of that shape. `grid-area` gained its four longhands and
  single-value `overflow` its two.
- **`align-items: baseline`.** Flex items aligned on their tops, because
  nothing in the box tree recorded where a box's first baseline was.
- **Numeric computed values.** `hypot()` returned a plain number, so
  `hypot(3px, 4px)` was rejected by every property wanting a length;
  `z-index: calc(infinity)` computed to `inf`; numbers past six
  significant digits serialized in exponent form; and a transform built
  from 3D functions serialized as a 2D `matrix()` whenever the resulting
  matrix happened to be flat.
- **Insertion steps for `ChildNode`/`ParentNode`.** `append`,
  `prepend`, `before`, `after`, `replaceWith` and `replaceChildren` did
  not run the insertion steps, so a `<script>` inserted through any of
  them never executed and custom elements never got
  `connectedCallback`. Cloning a `<template>` and handing it to
  `replaceWith` — the ordinary way to stamp a template — dropped every
  script in it.
- **Complete computed-style resolution.** `getComputedStyle` enumerated
  218 properties but returned `""` for 126 of them, because the
  initial-value fallback was a hand-written `strcmp` chain covering
  about fifty longhands. It is now a table covering everything the
  enumeration reports; `currentcolor`-initial properties resolve to the
  element's `color`, and inherited properties walk to the nearest styled
  ancestor. `css/css-cascade` went from 639 to 951 passing subtests.
- **`<position>` normalization.** `background-position` and
  `object-position` kept their specified text as the computed value, and
  the four-value edge-offset form was mis-split (`right 30% top 60px`
  gave x=100% y=30% instead of x=70% y=60px). Both shorthands now share
  one splitter. `transform-origin` and `perspective-origin` serialized
  as `translate(0%, 0%)`, which is not a valid value for either
  property; they now resolve against the border box and serialize as
  lengths.
- **The `color()` function.** `color(srgb …)`, `srgb-linear`,
  `display-p3`, `a98-rgb`, `prophoto-rgb`, `rec2020`, `xyz`, `xyz-d50`
  and `xyz-d65` parse and convert to sRGB, with number, percentage and
  `none` components and an optional alpha.
- **`<script>` type handling.** Only two of the sixteen JavaScript MIME
  type essences the spec lists were accepted, so a script labelled
  `application/ecmascript`, `text/jscript`, `text/livescript` or any of
  the `text/javascript1.0`–`1.5` series was silently skipped — and a
  skipped script leaves no trace. The type is now stripped of
  surrounding whitespace and matched against the full list, a
  `language` attribute with no `type` beside it contributes
  `text/` + its value, and the comparison runs over the attribute's real
  byte length so an embedded NUL no longer truncates
  `type="text/javascript\0"` into a match.
  `script-type-and-language-js.html` goes from 91 to 456 of 456
  subtests, and `html/semantics/scripting-1` as a whole from 981 to
  1304.

## Keeping this current

Re-run the areas in the table and update it in the same commit as any
engine change that moves them. When a fix lands, name the tests it was
verified against in the commit message — the numbers above are only
useful if they can be reproduced from the commit that claims them.
