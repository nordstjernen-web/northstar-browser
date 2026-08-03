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
timeout.

**These numbers are a snapshot, not a running total.** Engine work has
landed since — the colour, `border-radius`, flex-baseline and CSSOM fixes,
and everything in `Changelog.md` after 1.0.5 — and none of it has been
re-measured against this table; each change was verified against the
behaviour it names. Re-run the areas below before quoting any of this as
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
change smaller than about thirty subtests there is noise.

`html/dom` is the largest single area in WPT and the one most ordinary
pages depend on. `css/css-flexbox` is the weakest and the most
consequential for real pages. `css/css-color` is nearly as low, but
almost all of its failures are the two structural gaps under *Known
gaps* rather than scattered bugs.

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

Engine changes are recorded in [`../Changelog.md`](../Changelog.md), which
is the single list. This page tracks where the engine stands and what is
structurally missing; it is not a second changelog.

## Keeping this current

Re-run the areas in the table and update it in the same commit as any
engine change that moves them. When a fix lands, name the tests it was
verified against in the commit message — the numbers above are only
useful if they can be reproduced from the commit that claims them.
