# quickjs-ng vs QuickJS: measured comparison

Northstar builds on either of two JavaScript engines (see
[quickjs.md](quickjs.md)): **quickjs-ng 0.17.0**, the default, and Fabrice
Bellard's original **QuickJS 2026-06-04** (`-Djs_engine=quickjs`). This
report builds both from the same tree, runs them through the same tests
and benchmarks, and compares the results.

Measured 2026-10-02 at `da7e522` (1.0.13-dev).

## Verdict

Interim, pending the web-platform-tests run. QuickJS (Bellard) is faster on plain JavaScript: about 11 % on Octane, and 2–30× on integer-keyed `Map`/`Set`, where quickjs-ng has a hashing bug. On DOM-heavy framework workloads (Speedometer) the two are within a few percent. quickjs-ng supports more of the language and is the only engine CI builds.

## Summary

Pending: the summary table is written once the web-platform-tests run has finished.

## Test setup

- **Machine.** 4 vCPU Intel Xeon @ 2.10 GHz (x86-64), 15 GB RAM, Ubuntu
  24.04, Linux 6.18, GCC 13.3.0. No GPU, so pages render through the
  software (cairo) renderer.
- **Builds.** Two build directories from one checkout, each with the
  project's default release flags (`-O3`, LTO, PIE, `NDEBUG`):
  `meson setup builddir` (quickjs-ng) and
  `meson setup builddir-quickjs -Djs_engine=quickjs`. ccache was
  disabled for the timed builds.
- **How pages were driven.** Every page ran in the real browser binary
  in headless mode (`--headless --dump=none`), served over a local HTTP
  server. Timings come from `performance.now()` inside the page and are
  reported through `console.log` (`--debug=js`). Peak memory is the
  process's `VmHWM` (peak RSS). For each test, the two engines ran in
  alternating order (A B, B A, A B …) so drift on the machine hits both
  equally.
- **Repetition.** Octane: 3 runs per benchmark per engine, median.
  Micro-benchmarks: 3 page loads × 5 repetitions per engine, median.
  Speedometer: `scripts/speedometer-bench.sh`, 3 iterations per suite, median. Startup: `hyperfine`, 20 runs (10 cold).

## Results

### Build, binary and integration cost

| | quickjs-ng 0.17.0 | QuickJS 2026-06-04 |
|---|---|---|
| Clean build, wall clock (4 jobs) | 1 m 22 s | 1 m 07 s |
| Clean build, CPU time (user) | 268 s | 203 s |
| Ninja build steps | 398 | 390 |
| Engine objects, compile only (`-j1`) | 3.9 s | 3.8 s |
| Final LTO link of `northstar` | 40.3 s | 37.5 s |
| Warnings (project sources) | 0 | 0 |
| `northstar` binary (stripped) | 10,430,592 B | 10,115,456 B (−3.0 %) |
| `.text` size | 9,376,158 B | 9,065,170 B |
| Engine C sources compiled into the browser | 72,158 lines | 69,024 lines |
| Adapter code compiled in (`src/quickjs_compat.*`) | ~210 lines | ~560 lines (the same ~190 shared lines plus ~375 for the original only) |
| Local patches | 1 (Windows link) | 1 (sort comparator) + a meson build file |
| Built in CI | all four workflows | none |
| Upstream release | v0.17.0, 2026-09-18 | 2026-06-04 |

The quickjs-ng build is slower mainly because its subproject also builds
the `qjs`/`qjsc` command-line tools and `quickjs-libc`, which the browser
never links. Building only the library target would remove most of the
difference.

### Startup, memory and smoke tests

| | quickjs-ng | QuickJS |
|---|---|---|
| `--headless --dump=text about:start`, warm bytecode cache | 261.6 ± 13.7 ms | 258.9 ± 10.1 ms |
| Same, cold (bytecode cache deleted before each run) | 290.9 ± 8.4 ms | 293.5 ± 14.9 ms |
| `data/fixtures/js-dom.html` headless | 141.4 ± 4.1 ms | 138.3 ± 6.7 ms |
| Peak RSS, headless `about:start` | 98.5–99.9 MB | 97.9–98.1 MB |
| JS bytecode cache on disk after startup | 232 KB | 260 KB |
| GUI launch under Xvfb (`js-dom.html`), RSS after 6 s | 175 MB | 165 MB |
| `scripts/dev.sh smoke` (9 fixtures) | 9/9 OK | 9/9 OK |
| `cute-tests/*.html` output | identical | identical |

Both GUI builds launch, render and stay up. Startup time is the same within
noise: most of it is GTK, fontconfig and the bytecode cache, not the engine.

### Octane 2.0 (pure JavaScript, in the page)

Score per benchmark (higher is better), median of 3, and peak RSS.

| Benchmark | quickjs-ng | QuickJS | QuickJS vs ng | peak RSS ng / QuickJS (MB) |
|---|---:|---:|---:|---:|
| Richards | 648 | 768 | +18.5 % | 47 / 47 |
| DeltaBlue | 639 | 704 | +10.2 % | 47 / 47 |
| Crypto | 775 | 895 | +15.5 % | 46 / 46 |
| RayTrace | 1355 | 1340 | -1.1 % | 46 / 46 |
| EarleyBoyer | 1744 | 1898 | +8.8 % | 60 / 60 |
| RegExp | 360 | 383 | +6.4 % | 50 / 49 |
| Splay | 7669 | 7543 | -1.6 % | 195 / 186 |
| NavierStokes | 1430 | 1985 | +38.8 % | 48 / 48 |
| PdfJS | 3476 | 3722 | +7.1 % | 66 / 67 |
| Mandreel | 3988 | 5992 | +50.3 % | 124 / 118 |
| Gameboy | 5752 | 6464 | +12.4 % | 67 / 68 |
| CodeLoad | 12874 | 12878 | +0.0 % | 103 / 104 |
| Box2D | 3008 | 3034 | +0.9 % | 52 / 52 |
| zlib | did not finish | did not finish | — | 177 / 176 |
| Typescript | 9271 | 9427 | +1.7 % | 178 / 181 |
| **Geometric mean (14)** | **2251** | **2501** | **+11.1 %** | |

zlib never finishes on either engine. A single iteration of its
Emscripten-compiled inflate loop runs longer than the browser's 60-second
per-script budget (`js_eval_budget_ms`), so the watchdog interrupts it
(`InternalError: interrupted`). It is left out of the geometric mean.

QuickJS is ahead on 11 of the 14 benchmarks that finish. Its biggest wins
are on numeric and typed-array code: Mandreel (+50 %), NavierStokes (+39 %)
and Crypto (+16 %). Splay, RayTrace and CodeLoad are even; Splay
allocates the most and is the noisiest.

### Micro-benchmarks: DOM bindings and built-ins

Median ms (lower is better). The `dom.*` rows spend most of their time
crossing into Northstar's C DOM. The `js.*` rows stay inside the engine.
"ng slower by" is the quickjs-ng time over the QuickJS time.

| Test | quickjs-ng | QuickJS | ng slower by |
|---|---:|---:|---:|
| `dom.createElement+append (20k)` | 76.6 | 69.7 | +10 % |
| `dom.setAttribute/getAttribute (100k)` | 104.4 | 97.2 | +7 % |
| `dom.innerHTML parse (2k rows x10)` | 78.3 | 77.3 | +1 % |
| `dom.querySelectorAll (5k nodes x200)` | 64.8 | 68.3 | -5 % |
| `dom.events dispatch (50k)` | 587.3 | 567.3 | +4 % |
| `dom.textContent/style write (50k)` | 560.2 | 558.0 | +0 % |
| `dom.getBoundingClientRect w/ relayout (2k)` | 58.5 | 57.0 | +3 % |
| `dom.tree walk childNodes (10k nodes x50)` | 148.8 | 146.1 | +2 % |
| `js.JSON parse+stringify (2MB x5)` | 564.0 | 444.2 | +27 % |
| `js.string concat/split/join (200k)` | 134.4 | 59.8 | +125 % |
| `js.regexp exec (email-ish, 100k)` | 149.8 | 134.6 | +11 % |
| `js.object property churn (1M)` | 273.8 | 246.0 | +11 % |
| `js.Map/Set ops (500k)` | 1103.5 | 92.5 | +1093 % |
| `js.Array sort numbers (300k)` | 212.7 | 202.1 | +5 % |
| `js.closures + higher-order (map/filter/reduce 1M)` | 210.7 | 230.1 | -8 % |
| `js.class/method dispatch (2M calls)` | 451.3 | 459.7 | -2 % |
| `js.typed array math (Float64Array 4M ops)` | 453.2 | 334.3 | +36 % |
| `js.BigInt factorial(2000) x5` | 5.8 | 2.9 | +100 % |
| `js.try/catch throw (100k)` | 91.3 | 90.0 | +1 % |
| `js.generators/iterators (1M yields)` | 54.3 | 59.5 | -9 % |
| `js.destructuring/spread (500k)` | 341.8 | 307.3 | +11 % |
| `js.eval/new Function compile (2k)` | 17.0 | 16.0 | +6 % |
| `js.Promise chain microtasks (200k)` | 239.5 | 256.7 | -7 % |
| `js.async/await loop (200k)` | 106.0 | 123.4 | -14 % |

Peak RSS for the whole page: 563 MB (quickjs-ng) vs 556 MB (QuickJS).

- **DOM-bound work is engine-neutral.** Both engines call the same C
  bindings, so the `dom.*` rows agree to within ±10 %.
- **QuickJS is much faster on strings, JSON, BigInt and typed arrays.**
  String concat/split/join is 2.2× faster, JSON 1.27×, BigInt 2× and
  `Float64Array` math 1.36×.
- **quickjs-ng is faster on async code.** `async`/`await` (14 %),
  promise chains (7 %), generators (9 %) and closures passed to
  `map`/`filter`/`reduce` (8 %) all run faster on quickjs-ng.
- **Integer-keyed `Map`/`Set` are pathologically slow on quickjs-ng.** See
  the next section.

### quickjs-ng: integer keys in `Map` and `Set`

| 500 k operations | quickjs-ng | QuickJS | ratio |
|---|---|---|---|
| `Map.set(i % 5000, …)`: repeated small-integer keys | 769 ms | 33 ms | 24× |
| `Set.add(i % 7000)` | 832 ms | 26 ms | 32× |
| `Set.add(i)`: 500 k distinct integers | 335 ms | 105 ms | 3.2× |
| `Map.set(i, i)`: 500 k distinct integers | 323 ms | 94 ms | 3.4× |
| `Map.set(keys[i % 5000], …)`: string keys | 40 ms | 45 ms | 0.9× |
| `Map.get` with string keys | 40 ms | 40 ms | 1.0× |

The standalone `qjs` built from the same quickjs-ng subproject shows the
same 776 ms, so this comes from the engine itself, not from Northstar.

The cause is `map_hash_key()` in quickjs-ng's `quickjs.c`. It turns an
integer key into its float64 bit pattern, hashes it as
`(lo32 ^ hi32) * 3163`, and then keeps the **low** bits for the bucket.
A small integer stored as a double has an all-zero low word and many
trailing zero bits in the high word. Multiplying by an odd constant keeps
those zero bits, so most small-integer keys land in a few buckets, and
lookups walk long chains. Bellard's QuickJS mixes all 64 bits
(`map_hash64`) and takes the bucket from the **high** bits, so it does not
have this problem.

This is an upstream bug and should be reported to quickjs-ng. Real code
hits it often: ids, indices and counters are common `Map` keys, and
frameworks keep `Set`s of numeric ids.

### Speedometer 3.1 (TodoMVC workloads)

`scripts/speedometer-bench.sh`-style runs: each TodoMVC suite loads as the
top-level page and replays Speedometer's own Adding100Items,
CompletingAllItems and DeletingAllItems steps. The table shows total ms
per suite (lower is better). The score is the geometric mean of
1000/total (higher is better). `javascript-es6-webpack-complex` does not
load on either build.

| Suite | quickjs-ng (ms) | QuickJS (ms) | QuickJS vs ng |
|---|---:|---:|---:|
| TodoMVC-Angular-Complex-DOM | 334.5 | 345.4 | -3 % |
| TodoMVC-Angular | 192.6 | 198.0 | -3 % |
| TodoMVC-Backbone-Complex-DOM | 1213.0 | 1191.2 | +2 % |
| TodoMVC-Backbone | 312.4 | 255.3 | +22 % |
| TodoMVC-jQuery-Complex-DOM | 2454.2 | 2135.9 | +15 % |
| TodoMVC-jQuery | 1380.4 | 1356.2 | +2 % |
| TodoMVC-Lit-Complex-DOM | 1791.1 | 1798.2 | -0 % |
| TodoMVC-Lit | 684.9 | 665.9 | +3 % |
| TodoMVC-Preact-Complex-DOM | 214.0 | 217.2 | -1 % |
| TodoMVC-Preact | 91.0 | 87.3 | +4 % |
| TodoMVC-React-Complex-DOM | 557.5 | 571.2 | -2 % |
| TodoMVC-React-Redux-Complex-DOM | 658.1 | 656.2 | +0 % |
| TodoMVC-React-Redux | 516.9 | 510.7 | +1 % |
| TodoMVC-React | 438.4 | 431.5 | +2 % |
| TodoMVC-Svelte-Complex-DOM | 221.8 | 202.7 | +9 % |
| TodoMVC-Svelte | 79.8 | 78.2 | +2 % |
| TodoMVC-Vue-Complex-DOM | 321.5 | 320.8 | +0 % |
| TodoMVC-Vue | 182.4 | 181.1 | +1 % |
| TodoMVC-JavaScript-ES5-Complex-DOM | 699.8 | 1381.2 | -49 % |
| TodoMVC-JavaScript-ES5 | 919.0 | 504.8 | +82 % |
| TodoMVC-JavaScript-ES6-Webpack | 562.1 | 514.4 | +9 % |
| TodoMVC-WebComponents-Complex-DOM | 2412.4 | 2317.0 | +4 % |
| TodoMVC-WebComponents | 822.8 | 808.7 | +2 % |
| **Score (geomean of 1000/total, 23 suites)** | **1.998** | **2.047** | **+2.5 %** |

### Language features (ES2019–ES2026 plus extensions)

A page that probes 68 features, each with a small evaluated snippet, in
the browser (so Northstar's own polyfills count).

| | quickjs-ng | QuickJS |
|---|---|---|
| All 68 probes | **62** | 57 |
| ES2019–ES2026 (46 probes) | **45** | 43 |
| `Array.fromAsync` (ES2026) | yes | no |
| `using` / `DisposableStack` (ES2026) | yes | no (`SyntaxError`) |
| `Iterator.zip` (stage 3) | yes | no |
| `Error.captureStackTrace`, `stackTraceLimit`, `prepareStackTrace` | yes | no |
| `Atomics.waitAsync` | no | no |
| Locale-aware `localeCompare` (`'é' < 'f'`) | no | no |
| Proper tail calls | no | no |
| Maximum recursion depth, `function f(){f()}` | 5,648 frames (`RangeError`) | 7,445 frames (`InternalError`) |
| Error stack format | `at f (url:line:col)` | same; columns differ |

Both engines have Set methods, iterator helpers, `RegExp` `v` flag and
modifiers, duplicate named groups, `Float16Array`, `RegExp.escape`,
`Promise.try`, `Math.sumPrecise`, `Error.isError`, the `Uint8Array`
base64/hex methods, `Map.prototype.getOrInsert` and `JSON.rawJSON`.
Neither has `Temporal`. `Intl` exists on both, through Northstar.

`Error.captureStackTrace` matters in practice: many npm libraries call it
whenever it exists. On the original engine they take their fallback path.

### web-platform-tests

Run in progress; results will be added here.

## Reproducing

```sh
meson setup builddir && meson compile -C builddir
meson setup builddir-quickjs -Djs_engine=quickjs && meson compile -C builddir-quickjs

./scripts/dev.sh smoke
NS_BIN=$PWD/builddir-quickjs/src/gtk/northstar ./scripts/dev.sh smoke

NS_BIN=$PWD/builddir/src/gtk/northstar ./scripts/speedometer-bench.sh
NS_BIN=$PWD/builddir-quickjs/src/gtk/northstar ./scripts/speedometer-bench.sh

scripts/wpt-run.sh --wpt-root=$HOME/wpt --no-serve dom/nodes url ...
NS_BIN=$PWD/builddir-quickjs/src/gtk/northstar scripts/wpt-run.sh ...
```

The Octane suite came from <https://github.com/chromium/octane>. Each
benchmark ran as its own page: `base.js` plus the benchmark's files,
driven by `BenchmarkSuite.RunSuites` from the `load` event, with the
results logged to the console. The micro-benchmark and feature-probe pages
were written for this comparison. They are not in the tree; the
measurements above list what each one does.
