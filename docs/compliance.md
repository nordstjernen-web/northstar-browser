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
timeout — except `css/css-flexbox`, re-measured against a checkout of
2026-08-08 with a 6 s timeout after the CSSOM and flex work in 1.0.7.

**These numbers are a snapshot, not a running total.** Engine work has
landed since — the colour, `border-radius`, flex-baseline and CSSOM fixes,
and everything in `Changelog.md` after 1.0.5 — and none of it has been
re-measured against this table; each change was verified against the
behaviour it names. Treat every row but the flexbox one as a **floor**
rather than a reading: they were all taken while `offsetLeft` and
`offsetTop` returned document coordinates, which `checkLayout` compares
directly, so any area with layout assertions scored lower than the engine
deserved. Re-run the areas below before quoting any of this as current.

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
| `css/css-flexbox` | 1437 / 3535 | 40.7% |

### Re-measured for the registered-custom-property work

Measured at `41c3030` on 2026-08-09, 6 s per-test timeout, against a
**sparse** WPT checkout of the CSS areas served over a plain static HTTP
server. Its file set is smaller than the one the table above used, so
these denominators are not comparable with those rows — only with each
other, and with the same run against `5916e0b`, the commit before this
work, given in the *before* column.

| Area | Before | After | Pass rate |
| --- | --- | --- | --- |
| `css/cssom` | 3245 / 3484 | 3245 / 3484 | 93.1% |
| `css/selectors` | — | 4078 / 4413 | 92.4% |
| `css/css-cascade` | 744 / 911 | 744 / 911 | 81.7% |
| `css/css-properties-values-api` | 64 / 809 | 590 / 1039 | 56.8% |
| `css/css-counter-styles` | 50 / 118 | 67 / 118 | 56.8% |
| `css/css-values` | 683 / 1947 | 689 / 1960 | 35.2% |
| `css/css-color` | 22 / 108 | 22 / 108 | 20.4% |

`css/css-properties-values-api` is where nearly all of the movement is:
`@property` honours its `syntax` descriptor, `CSS.registerProperty`
exists, and a registered property computes its value. Its denominator
grew because files that used to stop at the first missing API now reach
subtests they never got to. The two areas that read as unchanged were
checked file by file rather than in aggregate — no file in either moved.

Read `html/semantics/scripting-1` with its denominator in view: roughly
a quarter of its 474 files load modules or iframes over the network and
time out under load, so the total swings by tens of subtests between
runs of the same binary (1944, 1962 and 1980 across three runs). A
change smaller than about thirty subtests there is noise.

`html/dom` is the largest single area in WPT and the one most ordinary
pages depend on. `css/css-flexbox` is still the weakest layout area and
the most consequential for real pages. `css/css-color` is nearly as low,
but almost all of its failures are the one structural gap under *Known
gaps* rather than scattered bugs.

## Known gaps

### Flex layout is still the weakest area, but much less so

`css/css-flexbox` was re-measured after the `offsetLeft`/`offsetTop` and
flex static-position work described in `Changelog.md`: **1437 of 3535
subtests, 40.7%**, up from 653 (18.5%) on the same checkout and the same
denominator. That single jump also says something about the table above —
`offsetLeft` and `offsetTop` returned document coordinates instead of
offsetParent-relative ones, and `checkLayout`, the harness most of WPT's
layout tests are written against, compares exactly those. Every number in
the table measured before that fix understates the engine by an unknown
amount, and every layout area is worth re-running before it is quoted.

What is correct: a row of `flex: 1 1 auto` / `flex: 2 1 0` / fixed-width
items resolves to the same geometry a browser produces,
`flex-direction: column` with `align-items` and `justify-content` places
items correctly, `align-items: baseline` aligns baselines,
`flex-wrap: wrap-reverse` reverses the cross axis, and an
absolutely-positioned child takes its static position from the
container's `justify-content` and its own `align-self`.

What remains, in the order it costs subtests:

- **Vertical writing modes.** Flex layout is horizontal-only: `row` is
  always the x axis. `abspos/position-absolute-013` alone is 216 of the
  remaining failures and is almost entirely `vertical-rl`/`vertical-lr`.
- **`flex-direction: column` with `flex-wrap: wrap`.** `layout_flex_column`
  ignores wrapping altogether, so the `align-content-vert-*` files fail
  as a group.
- **Negative free space.** `negative-overflow-*` overflows the container
  in the wrong direction.
- Percentage resolution against an indefinite container, nested flex
  containers, and the intrinsic-size contribution of a flex container to
  its parent.

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

### Scroll snap does not reach the document scroller

`scroll-snap-type` works on a scroll container — a box with its own
overflow — and `ns_box_scroll_snap` is reached from the wheel handler and
from the `scrollTop`/`scrollLeft` setters. The document scroller is not
one of those boxes: it belongs to the window, so the property has no
effect on `html` or `body`, which is where a page most often puts it.
`scroll-padding` on the container and `scroll-margin` on an item are
honoured wherever snapping happens at all. Closing this means giving the
document scroller a box to snap against, not extending the snap code.

### `html/semantics/document-metadata`

The low score here is dominated by files that time out rather than fail
assertions — `<link>` loading, `rel=preload`, and `referrerpolicy`
behaviours that need resource-timing signals the headless driver does
not settle on. The subtests that do run mostly pass.

## Recent fixes

Engine changes are recorded in [`../Changelog.md`](../Changelog.md), which
is the single list. This page tracks where the engine stands and what is
structurally missing; it is not a second changelog.

## Keeping this current

Re-run the areas in the table and update it in the same commit as any
engine change that moves them. When a fix lands, name the tests it was
verified against in the commit message — the numbers above are only
useful if they can be reproduced from the commit that claims them.
