Changelog:
=========
Significant changes in each release:

1.0.8:
======
* A custom property can say what it holds. `@property` was parsed for its
  `inherits` and `initial-value` descriptors and nothing else: the `syntax`
  descriptor was read past, so the one thing the rule exists to declare —
  the grammar its value has to match — was never checked, and
  `CSS.registerProperty` was absent entirely, which is how most of the
  libraries that use registered properties reach for them. Both now go
  through one grammar: `src/css_prop_syntax.c` parses a `<syntax>` string
  into its alternatives and multipliers and matches a value against it,
  including the arithmetic, so `calc(7in - 12px)` is a `<length>` and
  `calc(5px + 10%)` is not, and so is the computational-independence rule
  that makes `10em` a legal length in a stylesheet but not as an initial
  value. A rule missing `syntax` or `inherits`, or carrying an
  `initial-value` its own syntax rejects, is now dropped rather than half
  honoured, and a declaration whose value does not match the registered
  syntax falls back to the initial or inherited value instead of being
  taken at face value. `CSS.registerProperty` throws the errors the API is
  specified to throw — `SyntaxError` for a name, syntax or initial value it
  cannot accept, `InvalidModificationError` for a second registration — and
  the CSSOM grew `CSSPropertyRule`, so `name`, `syntax`, `inherits` and
  `initialValue` read back off the rule.
* `@counter-style` rules with a name no counter style may take — `none`, a
  CSS-wide keyword, or one of the six predefined styles the spec forbids
  overriding — are dropped instead of entering the stylesheet, and
  `CSSCounterStyleRule` reports its `name`.
* The CSS tokenizer decides what starts an identifier the way the Syntax
  specification does. A `-` was treated as the beginning of a name whatever
  followed it, so the subtraction in `calc(7in - 12px)` tokenized as an
  identifier rather than an operator; a hyphen now only starts a name when
  a name character, a second hyphen or an escape follows it, and the same
  rule governs the unit after a number and the name after `@`.
* The page itself snaps. `scroll-snap-type` reached 1.0.7 on scroll
  containers only, which left out the arrangement almost every page that
  asks for snapping actually uses: full-height sections down the document,
  with the property on `html` or `body` and nothing overflowing in between.
  The document scroller is the one scroller that is not a box — the shell
  owns its offsets in a `GtkAdjustment`, and the engine only learns them as
  the coordinates it is asked to paint from — so the snap positions its
  descendants offer were never consulted. The solver no longer derives the
  snapport from the scrolling box: it takes one, so the viewport can supply
  its own, and the renderer resolves the proposed offset against the root
  element's `scroll-snap-type` on the way into a frame and hands the snapped
  one back to the shell, which is the same channel `scrollIntoView` and
  fragment navigation already return a scroll position through. Every source
  of document scrolling therefore snaps — the wheel, the scrollbar, the
  keyboard — because each of them ends in a frame rendered at a new offset.
  `scroll-padding` on the root insets the viewport snapport as it insets a
  box's, `mandatory` and `proximity` keep their meanings, and the horizontal
  axis rides back alongside the vertical one. The root element is `html`,
  and `body` is honoured as a source too, which is how the root's `overflow`
  is already read here and what the pages that set it there expect.
  Verified on `data/render-tests/scroll-snap-viewport.html`, four
  hundred-viewport-height sections in a 955-pixel viewport: proposed offsets
  of 100, 400, 600, 1000 and 1400 all resolve to 955, 2000 to 1910, and
  anything past the end to 2865, while `scroll-snap.html`, whose snapping is
  all inside boxes, and the ordinary layout pages resolve to no snap at all
  and scroll exactly as before.

1.0.7:
======
* The `about:start` splash carries the release number and a better sky and
  earth. The sky was a two-stop vertical ramp drawn as one filled rectangle
  per scanline, which is both the slowest way to write a gradient and the
  one that bands worst; it is now a multi-stop atmosphere rendered as an
  array, with forward scattering around the sun, a haze band along the
  horizon and a scatter of stars that fades out as the sky brightens
  towards it. The north star hangs in the dark of the zenith, cool against
  the warm sun on the other side of the frame, and twinkles on its own
  period so the two never pulse together. The earth had been a soft blur:
  its terrain came from one 1024² noise field sampled through three fixed
  mip taps, so coastlines dissolved and nothing on land read as relief.
  It is built at 2048² now from a continent field, a coastline field and
  ridged noise for mountain ranges, shaded from the height gradient, and
  sampled with a real trilinear filter across the whole mip chain — so
  there are coastlines with a shallow shelf inside them, snow that sits on
  the peaks rather than across whole regions, and deserts and forests that
  follow a moisture field. Aerial perspective now decays with distance the
  way it should, which had been inverted: the haze thinned towards the
  horizon and thickened over the ground nearest the viewer. The land is
  settled, in the manner of a turn-based strategy map: capitals are
  scattered across it at a minimum separation and the ground divided
  between them by a Voronoi partition whose distances are warped by noise,
  so frontiers wander the way drawn ones do rather than meeting at
  straight bisectors. Each nation takes the lowest colour none of its
  neighbours holds, and wears it as a wash over its territory, a bolder
  line along its frontiers and a lighter one down its coast, with a
  capital and up to four towns marked inside. It rides as its own
  premultiplied overlay above the terrain and fades out with distance, so
  the markers do not turn into confetti where the texture repeats near the
  horizon, and passing cloud covers them the way cloud covers everything
  else.
* `offsetLeft` and `offsetTop` are measured from the offsetParent again.
  Both returned a document coordinate — and one built from the margin box
  rather than the border box — so an element inside any positioned
  ancestor reported where it sat on the page instead of where it sat in
  its parent. CSSOM View asks for the distance from the offsetParent's
  padding edge, with a statically positioned `body` or root the exception
  every engine makes, and that is what they return now. A great many
  pages measure this way, and so does the `checkLayout` harness that most
  of WPT's layout tests are written against: the reading of
  `css/css-flexbox` was flat at 653 subtests before the fix below and
  after it, because the fix could not be seen through this one. With both
  in, `css/css-flexbox` goes from 653 to 1437 of the same 3535 subtests,
  18.5% to 40.7%.
* An absolutely positioned child of a flex container is placed where the
  flexbox specification says. It landed at the container's content-box
  origin whatever the container asked for; CSS Flexbox 4.1 says its
  static position comes from `justify-content` and its own `align-self`,
  as though it were the only flex item, and `flex-direction: *-reverse`,
  `flex-wrap: wrap-reverse` and `direction: rtl` each turn the axis they
  govern around. Vertical writing modes are not covered — flex layout
  itself is horizontal-only here.
* `flex-wrap: wrap-reverse` puts the first line last. Lines wrapped, but
  the cross axis was never turned around, so the first line stayed at the
  top and `align-content: flex-start` stayed at the top with it. The
  lines are now mirrored within the container after `align-content` has
  placed them, and each item is mirrored within its line, which reverses
  `align-items: flex-start`/`flex-end` along with them.
* CSS Scroll Snap. `scroll-snap-type` on a scroll container, with
  `scroll-snap-align` on the things inside it, moves the container onto
  the nearest snap position once a scroll lands — from the wheel, and
  from `scrollTop`/`scrollLeft`. `scroll-padding` on the container and
  `scroll-margin` on an item inset the snapport and outset the snap area,
  both as shorthands and per side; `mandatory` always snaps, `proximity`
  only from within half a page. A wheel tick shorter than the gap between
  two snap positions still moves the reader forward rather than falling
  back to the one behind. This is scroll containers only: the document
  scroller belongs to the window, not to a box, so `scroll-snap-type` on
  `html` or `body` does nothing yet.
* The browser prints. `Ctrl+P`, or *Print…* in the menu, lays the page out
  for paper and hands the sheets to the operating system's own print
  dialog through `GtkPrintOperation` — CUPS on Linux, the Win32 printer
  dialog on Windows, and the Cocoa panel on macOS — so no printing code
  is written per platform and no new dependency is added. The engine gets
  the parts of CSS that a printer needs: `@media print` now matches (the
  media type was hardcoded to `screen`, so a page's print stylesheet was
  simply ignored), `@page` sets the sheet size from a name (`A4`,
  `letter`, `legal`, `ledger`, the A/B series), from one or two lengths,
  or from `portrait`/`landscape`, along with its margins, and
  `break-before`, `break-after` and `break-inside` — with the legacy
  `page-break-*` spellings mapping onto them, `always` becoming `page` —
  decide where a sheet may end. A sheet is cut at a forced break if there
  is one before the page is full; otherwise the cut is pulled up above
  any box it would have split, which is every leaf box, every line of a
  paragraph, and anything asking for `break-inside: avoid`. Printing
  restores the on-screen layout afterwards, so the page a reader is
  looking at does not reflow under them. `--dump=print:FILE` renders the
  same pagination to a multi-page PDF without a printer, which is how
  `data/render-tests/print-pagination.html` was checked: three A4 sheets,
  every card whole, the `@media print` paragraph swapped in, and the
  forced break starting sheet three.
* Text layout is ns-pango `2f975d8`, and a paragraph now measures the same
  whether or not another paragraph shaped its words first. The shaping
  cache decided where a run could be cut by reasoning about Unicode — a
  space either side of the boundary, or an ideograph — but Unicode does
  not know what a font does, and Liberation Sans and Liberation Serif,
  which fontconfig hands out for Arial, Helvetica and Times New Roman,
  kern the space against the letter that follows it and put the
  adjustment on the space. A cached piece ending in one therefore carried
  a width that belonged to whatever word had followed it that time, and
  served it in front of another: "Type of" came out 1024 units narrow
  once "Type A" had been laid out, and which paragraph was wrong depended
  on what the process had rendered earlier. HarfBuzz answers this
  directly — asked for unsafe-to-concat flags it marks the clusters whose
  glyphs depend on the text beyond them — so a piece is now stored only
  when the shaper cleared both of its cuts, and an item whose pieces do
  not all survive that is stored whole instead. `NS_PANGO_SHAPE_CACHE=0`
  now switches off the item and break caches along with the shape cache,
  and `verify` mode compares a served item against a fresh shaping field
  by field rather than skipping anything longer than a word. Verified
  here by rendering the 45 pages in `data/render-tests/` with the cache
  on and with it off: identical layout on every one, and no mismatch
  under `verify`.
* The build documentation says how a dependency is actually resolved and
  how to move one. `docs/building.md` and `README.md` both said `meson
  setup` fetches lexbor, quickjs-ng and ns-pango, when the first two take
  a system copy whenever the build finds one new enough and only fall
  back to the wrap — and ns-pango is the one that is always the
  subproject, because the renamed symbols are the whole reason it can sit
  beside the system Pango that GTK loads. README also listed the vendored
  components and then named only lexbor and quickjs-ng as fetched,
  leaving ns-pango in neither list. `docs/building.md` gains a section on
  updating a pin: that editing `revision =` does not move the checkout on
  its own, that `meson subprojects update --reset <name>` does and works
  on the shallow clones these wraps ask for, where the three `diff_files`
  patches live and when to regenerate them, which vendored copies are
  byte-identical to upstream and which two are not, and what to verify
  afterwards.
* The JavaScript engine is quickjs-ng v0.16.1, up from v0.15.1. The two
  patches the tree carries — the Windows link fix and the removal of the
  identical-object shortcut in `Array.prototype.sort`, which skipped a
  comparator the specification says must run — still apply and were
  regenerated against the new sources so they land without fuzz.
  `JS_NewArrayBuffer` grew a `max_len` parameter and now takes a
  reallocating callback in place of a freeing one, so the WebAssembly
  memory object passes a zero maximum and no callback, which is the
  fixed-length, externally-owned buffer it was already asking for.
* A page's ES modules load again. Reading `document.implementation`
  before `DOMImplementation` existed on the global left a TypeError
  pending on the context: the getter looked up the constructor's
  `prototype` without first checking that the constructor was there, and
  discarded the failure without clearing it. Nothing noticed until
  quickjs-ng v0.16.1 began reporting a pending exception at the next
  module boundary, at which point the stray error surfaced as the
  rejection of every `import` on the page — static, dynamic, `data:` and
  `blob:` alike — because the shape-normalising bootstrap reads that
  getter one statement before it defines the constructor. The lookup now
  guards the constructor the way every other prototype lookup in the file
  already does.
* The bytecode cache carries a new format stamp. quickjs-ng's own
  bytecode version moved with the upgrade, so entries written by an
  earlier build describe a layout the new engine will not read; they were
  already rejected safely and recompiled, but the stamp is what the cache
  has to discard them outright instead of paying for a failed read on
  every load until they age out.
* A translucent background on an inline box is translucent. Inline runs
  are painted through ns-pango attributes, and while a text colour
  carrying alpha already emitted a matching foreground-alpha attribute, a
  background colour emitted only the opaque three-channel one, so the
  alpha was dropped on the floor: `code { background: rgb(0 0 0 / 14%) }`
  painted a solid slab. The same colour on a block, an inline-block or a
  flex item was correct, which is why this survived — the wrong path is
  the one a dark theme's inline chips take. Both places that build the
  attribute list now emit `background_alpha` alongside the colour, exactly
  as the foreground path does.
* The documentation says what the code does. Several claims had outgrown
  the tree: `docs/architecture.md` gave a storage and security role to
  `secretbox.c`, a file that does not exist, and omitted ns-pango
  entirely; `docs/README.md` and `AGENTS.md` still said `<video>` laid out
  without decoding; `CLAUDE.md` still routed unrecognised images through a
  GDK-Pixbuf fallback removed in 1.0.6; `SECURITY.md` located the
  single-process startup path in `src/gtk/appmain.c`, which is
  `src/appmain.c`, and claimed no video codec attack surface at all — the
  pl_mpeg decoder is one, so it is now described with its bounds rather
  than denied. Cross-document `postMessage` is documented as working in
  both directions, with what it does and does not guarantee, now that it
  does. `THIRD-PARTY-LICENSES.md` gained the missing notice for ns-pango,
  which is LGPL and statically linked, and a list of the patches applied
  to upstream sources; it lost libepoxy from the statically-linked
  section, since nothing links it. `docs/debian.md` is gone — a dated
  packaging status and a list of accounts to register, whose one durable
  recipe already lives in `debian/README.source` — and so is
  `docs/preloading.md`, three hundred lines on the fetch path of one
  subsystem, reduced to the sentence in the pipeline table that says a
  subresource is fetched once. `AGENTS.md` is a pointer to `CLAUDE.md`
  rather than a second copy of it that drifts, and `docs/compliance.md`
  no longer repeats this changelog.
* A message a frame posts to its parent reaches the parent's listeners.
  Delivery decided whether the recipient was the top window by comparing
  it against whichever realm was executing, and while a frame's script
  runs that is the frame's own realm, so a message correctly addressed to
  the parent was judged to belong to some other window and handed to a
  dispatch path where the top window's `message` listeners are not
  registered. The message was built, cloned and queued, and then went
  nowhere; posting from the top window to itself worked, which is why
  this survived. The comparison is now against the main realm, which does
  not move. Freenet's River runs in a sandboxed frame and reaches the
  network by asking the shell around it to hold the WebSocket on its
  behalf, so every request it made was dropped in silence.
* A framed document is governed by its own Content-Security-Policy, not
  by the one that came with the page framing it. A policy was kept once
  per browser, so whatever header arrived last decided what every
  document was allowed to load. Freenet's River is served as a shell page
  whose own policy names no script host at all -- correct, because every
  script it runs is inline -- wrapped around an iframe holding the actual
  chat app, whose policy does allow its origin; the app's module was
  judged against the shell's policy and refused, and the page never got
  past the header bar. Each framed document now carries the policy from
  its own response and `<meta>`, and a resource is judged against the
  policy of the document holding it, which also survives the app adding
  a stylesheet from a timer long after the frame finished loading.
* `fetch()` resolves with a real `Response`. The object had the right
  properties and methods but not the prototype, so `instanceof Response`
  was false and `constructor.name` was `Object`. Code that branches on
  the type rather than duck-typing took the wrong path: wasm-bindgen's
  loader treats a non-`Response` as an already-compiled module and hands
  it straight to `WebAssembly.instantiate`, which is why River's
  WebAssembly failed with a `BufferSource or Module` type error instead
  of loading.
* `window` inside a frame is a `Window`. Frames get their own realm whose
  global was left an ordinary object, so `window instanceof Window` was
  false there while it was true at the top level. `web_sys::window()`
  makes exactly that test, so every Rust and WebAssembly UI framework hit
  it: Dioxus panicked with ``access to `window` `` the moment River
  started, taking the whole app down with an unreachable trap.
* `--version` prints the version and exits. The flag was never
  recognised, so it fell through to an ordinary startup: the browser
  armed the watchdog, opened a window and left the caller with exit 255
  and no version anywhere. It is now answered before the sandbox, the
  watchdog and GTK are touched, which is also what lets it work from the
  Windows bundle -- the launcher there attaches to the parent console for
  it the way it already did for `--headless` and `--print-config`.

1.0.6:
======
* A grid item placed by area name is aligned to its row. Items placed
  through `grid-template-areas` went down a layout path that never read
  `align-items` or `align-self`: each was put at the top of its row at its
  own height and left there, where the default is to stretch. lichess's
  lobby is one grid row holding the pool, the game list and the start
  buttons; the buttons' column stayed 179 pixels tall beside a 600-pixel
  neighbour and the player counts, pinned to the bottom of that column,
  came to rest on top of the buttons. A stretched item is now given its
  row height before it lays out rather than grown afterwards, so what is
  inside it lands in the right place too.
* A single flex line is as tall as the container says. A row flex
  container with a definite height has a line exactly that tall, and an
  item that stretches gets that height whether its content fits or not.
  The line was sized to the taller of the container and its content
  instead, so one over-tall item dragged the whole line past the height
  the author asked for -- lichess's 59-pixel site header held blocks that
  grew to 86 and hung out of the bar across the page behind it.
* A percentage inside `min()`, `max()` and `clamp()` is measured against
  the box rather than the viewport. These functions were folded to a
  single pixel value while the stylesheet was being parsed, when the only
  basis available was the viewport width, so the comparison ran against
  the wrong number and the answer was frozen before the element it
  belonged to was known: in a 400-pixel column `min(300px, 50%)` came out
  300 instead of 200. `width: min(100%, 60rem)` now measures what it says.
* A dialog opened from script renders, and a modal one is centred.
  `showModal()` and `show()` set the open attribute without telling the
  style engine, so the element kept the `display: none` it was matched
  with at parse time -- it reported open, matched `[open]`, and had no box
  at all. Only a dialog carrying `open` in the markup ever appeared. The
  user-agent sheet now also carries the modal rule the HTML specification
  defines, and an out-of-flow box asking for an intrinsic height is no
  longer stretched between its top and bottom offsets, so a modal lands in
  the middle of the viewport the way it does in a browser.
* A mouse or pointer event carries the window it was dispatched in.
  `UIEvent.view` is that window and every event the engine synthesised
  for a click, a drag or a hover reported null, which is a value no
  browser produces.
* A grid track can be measured in any length unit. `grid-template-columns`
  understood px, %, fr, em and rem, and quietly dropped every track it
  could not read, which moved each remaining track one place to the left.
  lichess.org asks for five columns with a `1vw` margin at either end;
  three arrived, its `<main>` landed in a column with no room in it, and
  the whole site laid out zero pixels wide down a 26000-pixel page. Track
  lengths now go through the same reader as every other length, and a
  track list that still cannot be read is discarded whole rather than
  closed up, because a missing list leaves the columns to
  `grid-template-areas` while a shifted one leaves nothing standing.
* A positioned box answers the pointer in the layer it paints in. Hit
  testing ranked a box against its own siblings and nothing else, so a
  fixed, high `z-index` overlay never rose above content in another branch
  of the page even though the painter drew it on top: lichess's game-setup
  dialog was visible and its "Play with the machine" button was not
  clickable, because the news feed behind it took the click. Positioned
  boxes are now collected and tried in the order the painter flushes them,
  and a modal dialog in the top layer is tried before the document.
* An `<svg>` that paints nothing hands the pointer to what is under it.
  SVG hit-tests as `visiblePainted` -- a shape answers where it draws and
  nowhere else -- but the engine lays an `<svg>` out as one replaced box
  and let that box answer for its whole rectangle. chess.com stretches an
  `<svg>` of rank and file labels over the board, so a piece could be
  picked up and never put down: the labels swallowed the pointerup that
  ends the drag.
* An SVG `font-size` attribute survives the cascade. A presentation
  attribute is author style at the very bottom of the cascade, so a real
  declaration beats it but inheritance must not; the renderer read the
  attribute and then overwrote it from the computed style, which always
  has a font-size because font-size is inherited. Every `<text>` drew at
  the page's font size scaled by the viewBox -- chess.com's board
  coordinates came out four times their size and spilled across the board.
* A worker shares the storage of the page that started it. The worker
  runtime was built without a storage partition, and IndexedDB reads that
  partition to find the origin's databases, so every `indexedDB` call
  inside a worker threw "Storage is unavailable" and chess.com reported
  its opening database as unusable.
* `prefers-color-scheme` reports what the desktop is actually wearing. The
  answer came from `gtk-application-prefer-dark-theme`, a GTK 3 property a
  GTK 4 dark theme does not set, so every dark desktop was told "light":
  a site's dark stylesheet never applied, and the browser's own pages sat
  white inside a dark window. The scheme is now judged from the luminance
  of the foreground colour the theme resolves, which holds for any theme
  rather than the ones that happen to set the old property, and it is
  re-evaluated when the theme changes under a running window. The internal
  pages -- start, about, settings, history and the error page -- gain the
  dark half they never had.
* An error page appears whenever a navigation has nothing to render. Only
  https:// failures got one, so a missing `file://` path came back 404 with
  an empty body and rendered as a blank white page. The page is now built
  for any scheme that ends in a transport error or a 4xx/5xx with no body
  of its own, and the classifier no longer blames the network for
  everything it fails to recognise: an unmatched message falls through to
  the status code, and a file URL is described as a file rather than as an
  unreachable server.
* The status line behaves like one. It held a permanent row beneath the
  page and read "Done" for the life of every visit; hovering a link wrote
  the URL there and leaving the link wrote nothing, so the last link the
  pointer touched stayed on screen indefinitely. It now clears when the
  pointer leaves a link and when a load finishes, and floats over the
  bottom-left corner of the page only while it has something to say.
* The toolbar reports the page zoom. Zooming said "Zoom 121%" for a moment
  and then left no trace, so a window could sit at any magnification with
  nothing admitting it -- and the steps were successive tenths, which is
  where 121% came from. A reading appears beside the address bar while the
  page is scaled, resets the zoom when clicked, and the steps follow the
  usual ladder: 90, 100, 110, 125, 150.
* The bookmark button says whether the page is bookmarked, with a star that
  fills once it is on the list; the popover's action becomes "Remove this
  bookmark" rather than silently doing nothing a second time. Escape closes
  the find bar, which its own tooltip already promised. The title bar no
  longer reads "Northstar 1.0.6 — Northstar 1.0.6" on a page with no title.
  The bookmarks popover sizes to its contents instead of reserving 280
  blank pixels, and the downloads window says when it is empty.
* Text shaping is cached a word at a time, not a run at a time, and a
  paragraph's items are cached too. The line breaker cuts a run wherever a
  line ends and shapes the piece again, so a paragraph measured
  unconstrained and then laid out at a real width shared no cache entry
  with itself -- the hit rate sat at half. Words do not move when the width
  does, which is why Firefox and Chrome both cache text a word at a time,
  and now so does this. A miss still shapes the whole run in one HarfBuzz
  call and only then divides the result, so no glyph is shaped in smaller
  company than before. Over a 200 KB page, shape-cache misses fall from
  5265 to 155 and stay there at every window width, HarfBuzz drops from
  28.6% of the process to 1.5%, and the layout phase goes from 159 ms to
  112 ms. Itemising -- bidi, script, emoji and width runs, and the font
  lookup behind each character -- is cached on the same principle, keyed on
  the context's serial so that a web font arriving invalidates it. Nothing
  moves on screen: five test pages, one of them mixing Latin, Arabic,
  Hebrew, Devanagari, Thai, CJK and emoji, render byte-identically.
* An IndexedDB write no longer walks the origin's whole storage directory.
  Every `put` recomputed the origin's quota by opening each `.sqlite` file
  beside the current one, asking it for `page_count` and closing it again --
  a directory scan and a fresh SQLite connection per record written, inside
  the write transaction. A page that stores a burst of records stalled the
  browser in the filesystem for as long as the burst lasted: starting a game
  on chess.com left the main thread inside `CreateFile` and it never came
  back. Cache the siblings' total for five seconds; the current database is
  still measured live, so the limit is enforced as before.
* The font metrics behind the `ex`, `ch`, `cap` and `ic` units are measured
  once per font rather than once per length. Resolving any of them shaped
  four probe glyphs through a fresh layout, and a stylesheet asks for the
  same font once per element its rule matches, so a page that sizes fields
  in `ch` paid for it on every one of them. The answer depends on nothing
  but the family, size, weight and slant, so it is kept until the font map
  changes underneath it -- 1.3% fewer instructions over a page whose
  elements each carry their own font, and nothing moves: the layout dump
  and the rendered PNG are byte-identical.
* A single-line text field shows as many characters as its box has room
  for. The visible window came from the `size` attribute alone, so CSS that
  widened the control -- `flex-grow`, `width: 100%` -- stretched only the
  painted frame: the start page's search box scrolled its text away after
  twenty-four characters with room for sixty. A control narrower than its
  `size` attribute clips inside its border now instead of painting past it.
* A `#fragment` stays anchored while the page finishes loading. The scroll
  offset was computed once, from whatever layout existed at navigation
  time, so images above the target had not been decoded yet and the view
  landed hundreds of pixels short of the heading it was asked for. The
  target is re-located whenever layout moves it, until the document goes
  quiet or the reader scrolls away.
* A popover menu takes the height its items need. GTK builds a section's
  separator after the popover has negotiated its size, so the surface came
  out one separator short and the scrolled window inside it swallowed the
  difference -- the last item of the context menu was always clipped.
* The toolbar menu covers what a reader reaches for: New Window, Zoom In,
  Zoom Out, Reset Zoom, Full Screen, History and the two save entries join
  it, grouped into window, view, page and tools sections, each naming its
  keyboard shortcut. Reload, a toolbar button one pixel away, leaves.
* Escape in the address bar restores the URL of the page on screen. It used
  to return focus to the page but leave whatever had been typed in the bar,
  where it named a page the window was not showing. With the page already
  focused, Escape stops the load. The connection indicator moves inside the
  entry, where the padlock belongs.
* Text layout no longer recomputes a paragraph's line, word and sentence
  breaks every time it lays that paragraph out. ns-pango gains a cache of
  what `default_break` -- UAX #14 plus UAX #29 -- produces for a run of
  bytes, which was 12.6% of every instruction the text engine executed and
  ran once per layout: a browser builds a layout over the same paragraph
  for min-content, for max-content, for the real width and again to paint
  it. On a 200 KB page 1120 distinct paragraphs are broken once and served
  2492 more times from the cache. Asking the shape cache no longer
  allocates either -- the key it was given per shaping call, hit or miss,
  cost a malloc, a free and two atomic refcounts on the font at a 98.7% hit
  rate -- and ASCII is classified for emoji segmentation without touching
  the interval tables. The whole headless page load runs 5.75% fewer
  instructions; the text engine alone, 20% fewer. Rendering is unchanged:
  the glyph-level dump and the rendered PNG of a mixed-script page are
  byte-identical either side.
* The text caches are sharded sixteen ways, so laying text out on several
  threads is worth doing. Every thread gets its own fontmap, because
  fontconfig's is unlocked, and so its own font objects -- which the cache
  keys on -- so all of them missed, all took the one lock exclusively to
  insert, and all queued behind each other: on four cores one table was
  worth 1.9x over no cache on one thread and only 1.2x on four. Sharding
  takes four-thread throughput a quarter higher, from about 79 000 to about
  98 000 layouts/s over the fork's corpus, and costs the single-threaded
  path nothing. Northstar's own layout still runs on the main loop thread,
  so this is headroom for parallel layout rather than a speedup today.
  `--debug=net` reports the break cache alongside the shape cache.
* `document.styleSheets` includes the sheets a page links to, with their
  `href` and their rules. Only inline `<style>` blocks had a populated
  `CSSStyleSheet`; a `<link rel=stylesheet>` produced one with a null href
  and an empty `cssRules`, because the sheet was built from the element's
  own text content and a link has none. nrk.no went from 13 reachable rules
  to 1895. The engine keeps a reference to the CSS it already fetched for
  the cascade, so nothing is downloaded or stored twice.
* `getComputedStyle(el).cssFloat` reports the used float. The accessor read
  the declaration block directly rather than going through whichever
  `getPropertyValue` the object carries, so on a computed style -- which has
  its own -- it always came back as the empty string, while the equivalent
  `getPropertyValue('float')` answered correctly.
* An inline-block, inline-flex or inline-grid sits on the text baseline by
  the baseline of its own last line box, the way CSS 2.1 asks, instead of
  resting its bottom margin edge there and then being placed at the top of
  the line regardless. A badge or button written inline with a sentence was
  drawn several pixels high, its own text floating above the words on either
  side, and the line box was made taller than it needed to be to cover the
  overshoot -- duck.ai's "Alle chatter er private" was the visible case.
* Instantiating a module through the `WebAssembly` JS API runs the module's
  start section and nothing else. WAMR, built for a standalone runtime, also
  called `_initialize`, `__wasm_call_ctors` and `__post_instantiate` from
  inside `wasm_runtime_instantiate` -- but on the web those are ordinary
  exports the JS glue calls itself, after it has pointed its heap views at
  the instance's memory. Running them first meant an Emscripten module tore
  down on its own first WASI call: chess.com's analysis engine died in
  `environ_sizes_get` before `new WebAssembly.Instance` had returned.
* A finished keyframe animation keeps the value `animation-fill-mode`
  says it should. The engine sampled the last keyframe correctly, then threw
  the sample away: every getter the painter calls required the animation to
  still be running, so at the moment it ended the box snapped back to its
  specified value. The common `opacity: 0` plus a `fade ... forwards`
  animation therefore faded in and vanished again within one frame, and the
  content stayed invisible for the life of the page -- chess.com's bot
  gallery, which is exactly that pattern, was a set of empty boxes.
* `AbortSignal` is an interface object, not a bare namespace. It was a plain
  object carrying `abort()`, `timeout()` and `any()`, and the signals an
  `AbortController` hands out did not inherit from it, so `signal instanceof
  AbortSignal` -- the guard every fetch wrapper writes -- threw "invalid
  'instanceof' right operand" instead of answering. chess.com's RPC client
  turned that TypeError into a 500 and never issued its first request.
  `MessagePort` gains the same treatment in the window: it existed only in
  workers, so ports came back with no prototype at all.
* A grid container's max-content width is the sum of its columns, not the
  width of its widest item. Anything that shrink-wraps a grid -- a float, a
  table cell, an inline-grid, `width: max-content` -- was sized as if the
  columns were stacked, so the tracks overflowed the box they were given.
  bbc.com's "LIVE" flag is a floated two-column grid, and the headline
  beside it started inside the flag rather than after it.
* Flex and grid items measure their intrinsic sizes in the font they will
  actually be drawn in. The base size of a flex item and the min-content
  floor of a flex or grid item were measured against the item's own style
  rather than the style its text inherits, so text in a web font was sized
  by the fallback face. An item then got a base size a pixel or two under
  what the real font needs and wrapped mid-phrase however much room the
  container had -- dn.no's nav pills broke "DN Helg" and "DN i VM" across
  two lines inside a box wide enough for either.
* An absolutely positioned box with `width: auto` gets the shrink-to-fit
  width CSS 2.1 asks for -- its max-content size clamped to the available
  space and floored at min-content -- measured by shaping the text. It used
  to be guessed from a character count at 0.65em each, so every tooltip,
  dropdown, badge and popover came out at a width unrelated to its
  contents: a seven-character label was sized 81px where the text needs 63.
* Event-listener objects follow the Web IDL callback-interface algorithm.
  `handleEvent` is looked up for every dispatch, non-callable values and
  throwing getters are reported as uncaught listener exceptions, and generic
  `EventTarget` objects no longer discard object listeners. The focused DOM
  event test moves from three passing subtests out of six to all six.
* Checkbox and radio activation keeps the state required by HTML's legacy
  pre-activation and canceled-activation steps. `indeterminate` is a real
  cloned input state, a canceled radio click restores the previously checked
  group member, synthetic `click()` events are untrusted and cannot recurse on
  the same element, and the resulting `input` and `change` events are not
  cancelable. Three focused input tests move from 52/80 to 80/80 subtests.
* `CSSStyleSheet.insertRule()` and `deleteRule()` enforce their required
  arguments, while the deprecated but web-visible `addRule()` and
  `removeRule()` methods mutate both constructed and document sheets. The
  CSSStyleSheet interface test moves from 8/17 to 17/17 subtests.
* An SVG with a `viewBox` but no width or height is sized the way CSS
  says: its ratio fitted inside the 300x150 default object size. The
  decoder used to rasterise it into a square, so the artwork was
  letterboxed and then stretched into the page's box -- Wikipedia's
  wordmark came out as a smear, and chess.com's sidebar logo under-scaled
  under `background-size: contain` and vanished entirely under `cover`.
* Workers get the APIs the polyfills already implement. A worker ran only
  the first part of the polyfill bundle, so IndexedDB, the streams, Blob
  and caches were missing inside one while the window had them all --
  thirteen globals differed between the two scopes. chess.com's opening
  book worker failed on `indexedDB is not defined` and took the play
  page's initialisation with it; that page now loads without a script
  error. `AbortController` and `AbortSignal` are installed in workers
  too, so a worker can cancel a fetch.
* A page on an origin that does not speak QUIC no longer stalls for the
  whole connect timeout. Whenever libcurl was built with HTTP/3, every
  request asked for it, so the first hop to an origin that silently drops
  UDP on 443 waited out the 15-second navigation connect timeout (6 for a
  subresource) and returned a timeout rather than falling back. The
  timeout also counted as a connection failure, which parked the host in
  the unreachable cache for two minutes and failed every subsequent
  request to it -- so acid3.acidtests.org took 15 seconds to answer and
  then lost all of its subresources. Requests now ask for HTTP/2 and are
  upgraded to HTTP/3 by the alt-svc cache, the way an origin advertises
  it; `NS_FORCE_HTTP3=1` still asks for HTTP/3 outright. Acid3 loads in
  2.2 seconds instead of 15.5, and three seconds in it has run 65 of its
  tests rather than 12.
* `min-content` and `max-content` grid tracks size to their content.
  Both parsed to the same kind as `auto`, so such a column stretched into
  the free space instead of shrinking to what it holds. Two defects sat
  behind that: a box with a definite width contributed nothing to
  min-content, because the measurement only ever looked at its children;
  and an intrinsic track was measured against the space left after every
  `minmax()` had taken its maximum, which scaled it to zero. On
  chess.com's play page the board column now has a width and the side
  panel sits beside the board rather than on top of it. A `min-content`
  track is also never scaled down to make the tracks fit, since its items
  cannot render narrower than it: the grid overflows instead, where before
  the following columns slid underneath content that kept its own width.
  A grid container's own min-content is the sum of its columns rather than
  its widest child, which is what a grid nested in a grid needs: on
  chess.com the board's inner grid measured its widest column instead of
  all four, so the board hung out of its container and over the panel.
* Grid items can be placed on named lines. A line named in the track
  list was parsed as a track, rejected, and dropped, so `grid-column:
  main` resolved to nothing and the item was auto placed. Names are now
  recorded and resolved, an area called `foo` also defines `foo-start`
  and `foo-end`, an end line repeating the start's name means the next
  line with that name, and an item with a column but no row keeps its
  column. chess.com's play page draws its board again.
* `OfflineAudioContext` renders audio instead of silence. The whole Web
  Audio surface was shape without substance: every `create*` method
  returned the same generic node, `connect()` returned its argument
  without recording an edge, `start()` and `stop()` did nothing, and
  `startRendering()` resolved a buffer of zeros. `AudioBuffer` could not
  hold samples at all -- `getChannelData` minted a fresh zeroed array on
  every call, so writing to it discarded the write. Nodes now carry their
  kind, `connect()` records the graph, sources honour their scheduled
  start and stop, buffers keep one array per channel, and the graph is
  rendered by `src/webaudio.c`: oscillators (sine, square, sawtooth,
  triangle, with detune), gain, a dynamics compressor with soft knee and
  attack/release, the RBJ biquad types, delay, wave shaping, constant
  sources and buffer playback with rate and looping. The canonical
  oscillator-into-compressor pipeline that used to sum to exactly zero
  now returns real samples. Rendering is mono, summed into every channel
  of the destination buffer, and `AudioParam` automation curves are still
  ignored -- a parameter reads as its current value for the whole render.
  The context, its nodes and its buffers also brand themselves, so
  `Object.prototype.toString` reports `OfflineAudioContext`,
  `OscillatorNode` and the rest rather than `Object`.
* The `about:start` splash is redrawn for the release: it reads
  "A fine open source web browser." beneath the version, and a small pink
  pig flies among the clouds, drifting and flapping on the same loop the
  clouds and the sun's rays already ride. `scripts/gen-splash.sh` renders
  it, and now runs where ImageMagick 7 is the only ImageMagick: the glow
  layers are flattened onto black before they are screened, so the blend
  no longer depends on how a given version treats a transparent source,
  the long MVG primitives are read from files rather than the command
  line, and `gifsicle` is optional.
* Table cells centre their content vertically again. A cell with no
  `vertical-align` of its own fell back to the initial `baseline`, so in a
  row taller than the cell's own line the text sat at the top. Every
  browser's user-agent sheet gives cells `middle`, which is what pages
  written as tables expect: on Hacker News the title beside the logo sat
  two pixels high. Cells now default to `middle`, and an author rule or a
  `valign` attribute still overrides it.
* An image is drawn inside its own borders, padding and margin. The
  painter placed the bitmap at the box's margin-box origin and gave it the
  content size, so anything between that origin and the content box was
  painted over: a bordered image covered its own top and left borders, and
  a margin shifted the picture instead of the box. On Hacker News the logo
  is an 18-pixel image with `border:1px white solid`, and its top and left
  edges were missing. Replaced content now starts where the content box
  starts, the way inline SVG and MathML already did, and the placeholder,
  alt text and drop shadow follow it.
* The toolbar reads like a browser toolbar again: back and forward are
  green, reload is blue, and a red stop button sits between reload and
  home, appearing only while a page is loading. Stop is real -- it marks
  the in-flight frame stale so the page stops changing, ends the loading
  state and drops the busy cursor -- though it does not abort the network
  request behind it.
* The window's title bar is shorter, which gives the page the height back.
  The home button is set off from the address bar rather than sitting flush
  against it, and the security shield beside the address is drawn smaller so
  it reads as an indicator rather than another toolbar button.
* `about:northstar` lists the user agent, resolved the way a request
  resolves it, so it shows what is actually sent rather than a constant.
* Dynamic `:has()` selectors now update after class, attribute and child-list
  mutations. The incremental restyle index used the selector's final subject
  as the mutation key, so `div:has(+ .test) #subject` indexed `#subject`
  instead of the `div` whose match changes. It now indexes the compound that
  owns `:has()` and invalidates its descendant and following-sibling dependent
  region. The selector-invalidation WPT subset gains 311 passing subtests with
  no regression, while typical mutations in its largest file still recompute
  only 8–10 styles rather than the whole document.
* `border-radius: 50%` -- the way a page makes a circular avatar --
  painted a 50-pixel corner. The painter read the radius out of the
  computed value and ignored its unit, so every percentage radius became
  a pixel count and a `calc()` radius was dropped altogether. Radii now
  resolve against the border box, the horizontal one against its width
  and the vertical against its height. The elliptical forms work as
  well: `border-top-left-radius: 10px 20px` used to be rejected and
  `border-radius: 10px / 20px` kept only the horizontal radii, while
  through the CSSOM the whole declaration was thrown away. A corner now
  carries both radii and the painter draws elliptical corners, with
  overlapping radii scaled down together in the proportion the spec
  prescribes.
* Relative colour syntax is implemented -- `rgb(from <color> r g b)` and
  the `from` form of `hsl()`, `hwb()`, `lab()`, `lch()`, `oklab()`,
  `oklch()` and `color()`, including `calc()` over a channel keyword, as
  in `hsl(from red calc(h + 120) s l)`. sRGB to Lab, HSL and HWB
  conversions and the inverse of each predefined-space transform are
  added; the inverses are derived from the matrices already used in the
  forward direction, so a `color(display-p3 from ...)` round trip cannot
  drift.
* The six colour functions each had their own argument loop, and each
  accepted whatever its loop happened not to reject: `rgb(1 2 3 4 5)`,
  `rgb(0,0,0,0,0)`, `rgb(10, 20 30)`, `rgb(10 20 30, 0.5)` and
  `hsl(120 50% 50% extra)` all parsed, and a colour split over two lines
  in a stylesheet failed to parse at all because the loops skipped only
  spaces. One scanner now serves all six: it enforces the legacy comma
  form and the modern whitespace-with-slash form as alternatives rather
  than a free mixture, and rejects anything after the arguments. Two
  value bugs fell out of the merge -- `lch()` scaled a percentage chroma
  by the factor `lab()` uses for its axes, so `lch(50% 20% 40)` gave
  C=25 where CSS Color 4 specifies 30, and `hwb()` had a ternary whose
  branches were identical.
* CSS tokenization closes an open function at end of input, so
  `el.style.color = "rgb(1,2,3"` sets the colour and leaves the rest of
  the inline style alone. Northstar spliced the raw text into the style
  attribute instead, where the unclosed paren swallowed every
  declaration after it: setting one property through the IDL attribute
  or through `setProperty` discarded the whole block, and `cssText`
  stored text that degraded further on each read and rewrite.
* `align-items: baseline` and `align-self: baseline` fell through to
  flex-start, so a label beside a larger heading sat with its top flush
  instead of its text on one line. An inline box now keeps the baseline
  of its first line and a flex item takes the first baseline found among
  its in-flow children, synthesizing one at the bottom margin edge when
  there is no line box in it.
* `el.style.overflow = "hidden auto"` and `el.style.gap = "10px 20px"`
  silently dropped the declaration while the same value in a stylesheet
  worked, because both names carry a property id of their own and the
  CSSOM validated them against that single-value grammar instead of the
  expansion the stylesheet parser performs. `grid-area` never set
  `grid-row-start` and its three siblings, and `overflow` with a single
  value left `overflow-x` and `overflow-y` reading `visible` on an
  element that was in fact clipping. The four-value shorthands --
  `margin`, `padding`, `border-width`, `border-color`, `border-style` --
  ignored a fifth value rather than rejecting the declaration.
* `hypot()` always returned a plain number, so `hypot(3px, 4px)` was
  rejected by every property that wants a length. An integer property
  given an overflowing calculation kept the raw double, so
  `z-index: calc(infinity)` computed to `inf` and `calc(NaN)` to `nan`;
  both now clamp as CSS Values requires. Numbers large enough to reach
  exponent notation serialized through `%g` as `1.23457e+06px`, wrong
  and lossy. A transform built from 3D functions serialized as a 2D
  `matrix()` whenever the resulting matrix happened to be flat, and the
  specified value of `translate3d()` and `scale3d()` dropped its Z
  component entirely.
* The CSS cascade indexes `:is()` and `:where()` subjects, and stops
  allocating per selector test. Profiling ten real sites put the cost of a
  page load in the cascade rather than in layout: on github.com a relayout
  spent 1.5-2.2 seconds matching selectors against 1635 elements, twice per
  relayout because container queries cascade a second time, against roughly
  0.5 s for layout itself. Counting candidates showed why -- 83% of the
  rules tested against each element came from the index's catch-all bucket,
  and the biggest group in it was rules whose subject is only `:is(...)` or
  `:where(...)`, which carry no name of their own to file under. An element
  can only match such a rule by matching one of the arms, so when every arm
  ends in an id, class or tag the rule is now filed under each arm's key
  instead of the catch-all; an arm without one (a bare pseudo-class, `*`)
  still falls back. On github.com that removes 25% of all candidate tests
  and 24% of the selector matches. Separately, the selector cache -- which
  exists so the container-query pass can reuse the first pass's results --
  allocated a key and a value for every test, about a million allocations
  per pass, none of which the first pass can ever hit, since a cascade never
  probes the same rule, selector and element twice. Entries now come from a
  bump arena with the result stored inline. On a page built to exercise
  this, 3200 elements against 3200 rules of which a third are `:is()` or
  `:where()` unions, the initial cascade falls 52% (1561 ms to 753 ms,
  median of nine) and the whole headless run 16%. Pages whose CSS does not
  use those selectors are unchanged: a text-heavy page's cascade moves from
  2.4 ms to 2.2 ms and its run time distributions overlap. Rendering is unchanged -- 32 layout and text dumps are
  byte-identical to the previous build -- and the `css` and `dom`
  web-platform-test subset gains 826 subtest passes, from tests that
  previously ran out of time, with no subtest regressing.
* Text is laid out through ns-pango, a fork of Pango carried as a meson
  subproject, instead of the system Pango. Pango keeps no cache that
  outlives a `PangoLayout`, so the same bytes were shaped by HarfBuzz
  once to measure a run and again to paint it, and a table cell was
  shaped for `min-content`, for `max-content` and once more to lay out.
  The fork adds a process-wide cache of finished glyph strings keyed on
  everything HarfBuzz reads -- font, bidi level, gravity, script,
  language, analysis and show flags, text transform, OpenType features
  and the item bytes -- and caches `pango_context_get_metrics` per font
  description, which resolving `line-height: normal` asks for on every
  inline run. Every symbol in the fork is renamed, because GTK loads
  the system Pango into the same process and GObject aborts when two
  libraries register the same type name. Layout time on a page that
  renders as well as lays out falls 25% on a text-heavy page and 38% on
  a table-heavy one; laying out alone, where nothing is shaped twice
  except for intrinsic sizing, the table page falls 29% and text is
  unchanged. Shaping results are unchanged: a corpus covering RTL and
  bidi, CJK, the white-space modes, intrinsic sizing, letter- and
  word-spacing, tabs, ellipsis, multi-column, inline atomics,
  decorations, small-caps and font features renders byte-identically,
  and the cache's own verification mode reports no difference between
  cached and freshly shaped runs anywhere in it, nor over 220 real
  web-platform-test pages. The `css` and `dom` test subset -- 1761 tests,
  35929 subtests -- reports no subtest that passed before and fails now.
* The DOM insertion methods run the insertion steps. `append`,
  `prepend`, `before`, `after`, `replaceWith` and `replaceChildren`
  moved nodes into the tree without the work `appendChild`,
  `insertBefore`, `replaceChild` and `insertAdjacentElement` already
  did, so a `<script>` inserted through any of them never executed and
  a custom element never got its `connectedCallback`. Cloning a
  `<template>`'s content and handing it to `replaceWith` -- the
  ordinary way to stamp a template -- therefore dropped every script in
  it. `innerHTML` still marks its scripts already-started, so it keeps
  not executing them.
* `getComputedStyle` resolves every property it enumerates. It listed
  218 properties but returned the empty string for 126 of them, because
  the initial-value fallback was a hand-written `strcmp` chain covering
  about fifty longhands -- so reading `flex-grow`, `align-items`,
  `max-width` or `object-fit` off an element that never set them gave
  "" rather than the initial value. Properties whose initial value is
  `currentcolor` resolve to the element's computed `color`, and an
  inherited property with no entry of its own walks up to the nearest
  styled ancestor, so elements outside the styled tree report inherited
  values instead of "".
* Computed `<position>` values are normalized. `background-position`
  and `object-position` kept their specified text, so `10% center`
  stayed `10% center` instead of resolving to `10% 50%`, and the
  four-value edge-offset form was mis-split: `right 30% top 60px`
  produced x=100% y=30% rather than x=70% y=60px. Both shorthands now
  share one splitter that resolves an edge keyword against its offset,
  and assemble their computed value from the two longhands.
* `transform-origin` and `perspective-origin` serialized as
  `translate(0%, 0%)`, which is not a valid value for either property.
  They resolve against the border box and serialize as lengths, so
  `left top` reads back as `0px 0px` and `center` on a 100x50 box as
  `50px 25px`.
* The CSS `color()` function parses. `color(srgb ...)` and the
  `srgb-linear`, `display-p3`, `a98-rgb`, `prophoto-rgb`, `rec2020`,
  `xyz`, `xyz-d50` and `xyz-d65` spaces convert to sRGB, with number,
  percentage and `none` components and an optional alpha. Previously
  the whole declaration was dropped as invalid.
* `docs/compliance.md` records where the engine stands against the HTML
  and CSS specifications, how to reproduce the web-platform-tests
  scores, and the known structural gaps.

1.0.5:
======
* Extended-container WebP images decode. Wuffs accepts only a bare
  `RIFF....WEBP` holding one `VP8 ` or `VP8L` chunk, and rejects the
  `VP8X` container outright -- which is what every encoder emits for a
  lossy image with transparency, so those failed with "could not decode
  image" rather than rendering. The container is now unwrapped to the
  bitstream Wuffs understands, and the `ALPH` chunk is decoded here:
  uncompressed alpha directly, lossless alpha by prefixing a synthesized
  `VP8L` header onto the stream and reading the green channel back out,
  then unfiltering with the horizontal, vertical or gradient predictor.
  The recovered alpha plane is bit-exact against libwebp for every
  filter and both compression methods.
* The still frame lifted out of an animated WebP decodes. It was
  reassembled without RIFF's even-size padding, so a chunk of odd length
  produced an odd `RIFF` size that Wuffs rejects before reading anything,
  and it prepended an `ALPH` chunk that Wuffs cannot parse at all.
* A single-frame MPEG-1 clip displays instead of failing. Both decode
  paths treated a frame list as an animation only when it held more than
  one frame, and discarded a shorter one to retry through the still-image
  decoder -- which has no MPEG-1 support, so a one-frame video decoded
  correctly and was then thrown away. A list of one frame is now kept and
  shown as a still; `ns_image_is_animation` still requires two, so nothing
  starts ticking for it.
* `<video>` responds to the media element API. A decoded clip reports its
  real `duration` and a `readyState` of `HAVE_ENOUGH_DATA`, `paused`
  reflects whether the frames are actually advancing, `play()` and
  `pause()` start and stop them, and assigning `currentTime` moves the
  displayed frame -- seeking to 0.20 s in a 25 fps clip shows frame 5, not
  merely a changed number. Reading `currentTime` reports the live position.
  Playback state lives on the decoded clip, which the image cache keys by
  URL, so two `<video>` elements sharing one source share playback.
  A clip still starts playing on load and loops: it is always silent, so
  this matches what Chrome allows for muted video, and Northstar has
  neither video controls nor click-to-play to start it otherwise.
* `readyState` on `<video>` and `<audio>` is no longer always zero. The
  polyfill that gives `<track>` elements a `readyState` replaced the
  property on the media prototype and delegated to the native getter it
  had captured -- but it looked that getter up as an own property of the
  element's immediate prototype, where it does not live, so the captured
  descriptor was null and every media element reported zero regardless of
  state. The lookup now walks the prototype chain.
* `<video>` plays MPEG-1. The tree already vendored pl_mpeg for its MP2
  audio decoder and switched the video half off with one call
  (`plm_set_video_enabled(plm, 0)`), so the decoder for an ISO standard
  whose patents have expired was being compiled and discarded. `video.c`
  turns it back on: an MPEG-1 Program Stream or elementary video stream is
  recognised by its start code, and every frame is decoded to the same
  `ns_image_pixel_frame` list an animated GIF produces. The image cache's
  fetch, frame timing, repaint scheduling and eviction then serve video
  with no new machinery, and `paint_video` draws the frame where it drew a
  placeholder. A `<video>` sizes to its intrinsic dimensions and keeps its
  aspect ratio when given only `width` or `height`, and `canPlayType`
  answers for `video/mpeg`. No new dependency: pl_mpeg moves from the
  audio helper's link line to the engine's.
  Decoding is up front rather than streamed, so a clip is bounded by
  `NS_VIDEO_MAX_FRAMES` and 256 MB of decoded pixels and a longer one
  plays its prefix. MPEG-1 is not a format the modern web serves; this is
  video for local and self-hosted clips, not for streaming sites.
* The vendored pl_mpeg no longer reads past a frame plane. Half-pel motion
  compensation samples `s[si + 1]`, `s[si + dw]` and `s[si + dw + 1]`, but
  `plm_video_process_macroblock` bounds only `s[si]`, so a macroblock on
  the bottom row reads up to one row plus one byte beyond the plane it
  samples -- absorbed by the next plane for interior planes, and off the
  end of the allocation for the last one. Fuzzing the decoder under
  AddressSanitizer with mutated streams reported it as a heap-buffer
  overflow read. The three frames are allocated as one chunk, which is now
  padded by that overshoot and zeroed, so the read stays inside the
  allocation and a corrupt stream decodes deterministically. Valid video is
  unaffected: no bound is tightened, so no macroblock that decoded before
  is rejected now. The overshoot was unreachable until this release
  because the video decoder was switched off.
* Animated images decode as animations on the engine's own fetch path.
  `ns_image_decode_body` routed GIF and APNG to the animation decoder, but
  the two fetch handlers in `engine.c` -- the ones headless rendering and
  the browser's own image pass use -- called `ns_image_decode_bytes`
  instead, which only ever returns a still frame. An animated GIF fetched
  through those paths therefore froze on frame one. Both now go through
  `ns_image_cache_insert_encoded`, and the still-versus-animated decision
  lives in one function rather than three copies that had already drifted.
* Animated PNG plays. Wuffs already decoded APNG frames -- the animation
  loop that GIF uses is format-agnostic -- but two things kept it from
  running: the callers only routed GIF magic to the animation path, and
  the animation decoder itself hardcoded the GIF signature check and the
  GIF decoder, so a PNG handed to it was rejected before it started. Both
  are now driven by the same format detection the still path uses. An
  APNG is recognised the way the spec defines it, by an `acTL` chunk
  appearing before the first `IDAT`, so an ordinary still PNG never pays
  for the animation decoder.

* WebP images decode. The vendored Wuffs already carried its WEBP and
  VP8 modules, and the build already enabled them, so lossy VP8,
  lossless VP8L and alpha all decode through the same memory-safe path
  as PNG, GIF, BMP and JPEG -- no new dependency, no new decoder, and
  `image/webp` now appears in the `Accept` header so content-negotiating
  servers will send it. Wuffs decodes only still WebP: an animated file
  is reduced to its first frame by walking the RIFF container for the
  first `ANMF` chunk and re-wrapping its `VP8 `/`VP8L` payload (with any
  `ALPH`) as a still image. Without that, advertising `image/webp` would
  have made pages worse, because a server picking between animated WebP
  and animated GIF on the strength of the header would have started
  sending a format that rendered as nothing.

* gdk-pixbuf no longer decodes page images. Every format the web
  actually uses is already handled in-tree -- ICO, then Wuffs for PNG,
  GIF, BMP and JPEG, then libavif, then the in-engine SVG renderer --
  so the pixbuf fallback had been reduced to TIFF, TGA, PPM and ICNS,
  none of which Chrome or Firefox render either. What it cost was the
  ability to know what parses untrusted bytes: `gdk_pixbuf_get_formats`
  enumerates loader plugins installed on the user's machine, so the set
  of decoders reachable from a web page was decided at runtime, varied
  per system, and could not be audited from the build. Those loaders
  also sit outside the memory-safety guarantee the Wuffs decoders were
  chosen for. The decode chain now ends after SVG: an unsupported
  format fails to decode instead of falling through to a plugin. GTK 4
  still depends on gdk-pixbuf for its icon theme, so a desktop build
  links it either way -- what goes away is the browser feeding it. An
  engine-only build (`-Dgtk=disabled`) now drops the dependency
  outright. `ns_image_pixbuf_supports_mime` is renamed
  `ns_image_supports_mime`, since it no longer speaks for a plugin set.

* libavif is optional. It was a hard `dependency()`, so a tree without
  it would not configure at all, even though every AVIF call site was
  already behind `NS_HAVE_AVIF` and `image_avif.c` was already compiled
  conditionally. The new `avif` meson feature defaults to `auto`, so a
  host that has libavif still decodes AVIF exactly as before;
  `-Davif=disabled` drops the dependency and AVIF images simply fail to
  decode. This matters because libavif pulls in a complete AV1 decoder
  (dav1d or libaom) for a format that is rare on the web, which is a
  large thing to require of anyone building from source.

* `var()` resolves inside SVG presentation attributes. A custom property
  set by a stylesheet rule now reaches `r="var(--radii)"` or
  `fill="var(--tint)"`, so a class can retheme an inline icon's colour
  and geometry the way it does for ordinary CSS properties.

* `marker-start`, `marker-mid` and `marker-end` draw their `<marker>` on
  path, line, polyline and polygon vertices. Vertices and their tangents
  come from the built Cairo path, so arcs and curves orient the same way
  straight segments do, and a mid vertex uses the bisector of its two
  tangents. `markerUnits="strokeWidth"` scales the marker with the
  stroke, `orient="auto"` and `auto-start-reverse` rotate it, and the
  marker viewport clips unless `overflow` says otherwise. `refX`/`refY`
  are mapped through the marker's own `viewBox` before positioning, so
  the reference point lands on the vertex.

* `mask` is honoured on SVG elements. The referenced `<mask>` renders to
  an offscreen surface whose sRGB luminance becomes the alpha the element
  is composited through, so a white mask shows the element, black hides
  it, and a gradient fades it. Group opacity and masking combine.

* A square border is painted inside its border box rather than centred
  on the edge. Each side was stroked along the border-box boundary with
  the line width set to the border width, and Cairo centres a stroke on
  its path, so every bordered element rendered half a border wider than
  it laid out on each side -- a 4px border occupied 6..9 and 60..63
  where the box model puts it at 8..11 and 58..61. Layout was always
  right; only the paint was wrong, so borders overlapped whatever sat
  next to them. Rounded borders already inset correctly and are
  unchanged.

* SVG is rendered by the engine instead of librsvg. `librsvg` is gone
  from the dependency list; `src/svg.c` walks the SVG DOM and paints it
  through the same Cairo surface, cascade and font stack that HTML uses.
  Inline `<svg>` was previously re-serialised to XML and handed to
  librsvg as an opaque raster, so the document's own stylesheet could
  never reach inside it: `fill: currentColor`, `svg .icon { fill: … }`
  and any script-driven change to SVG geometry were invisible. SVG
  elements now take part in the normal cascade, so `fill`, `stroke`,
  `stroke-width`, `stroke-dasharray`, `fill-rule`, `stop-color`,
  `text-anchor`, `paint-order` and the SVG geometry properties `x`, `y`,
  `cx`, `cy`, `r`, `rx`, `ry` are real CSS properties that inherit and
  animate like the rest. Covered: paths (including elliptical arcs and
  smooth curve continuation), rect/circle/ellipse/line/polyline/polygon,
  `viewBox` and `preserveAspectRatio`, nested `<svg>`, `<g>`, `<use>`,
  `<symbol>`, `<switch>`, `<defs>`, linear and radial gradients with
  `href` inheritance, `spreadMethod`, `gradientUnits` and
  `gradientTransform`, `clipPath`, group opacity, dashing, and `<text>`
  shaped through Pango. A standalone `.svg` document now sizes to the
  viewport rather than to a 300x150 default. `<img src="…svg">` and
  CSS `url(…svg)` go through the same renderer.

* Media queries inside a frame evaluate against the frame's own size,
  not a 300x150 guess. The viewport pushed while collecting a frame's
  stylesheets came from the frame's inline `style` attribute or its
  `width`/`height` content attributes, so a frame sized by a stylesheet
  rule -- `iframe { width: 100% }`, the common responsive-embed pattern --
  was measured as 300x150 and its `@media (min-width: ...)` blocks
  resolved against a size the frame never had. Layout now records each
  frame's content box, collection prefers it over the default, and when
  the recorded size disagrees with the one a viewport-dependent frame
  sheet was collected under, style and layout run once more so the frame
  settles on its real size. Frames whose CSS carries no width, height,
  aspect-ratio or orientation query never trigger the extra pass. Acid3
  test 46 passes as a result; Acid3 now scores 99/100, up from 98/100.
* A frame document's own stylesheet can style its root element. Sheets
  inside an iframe are rewritten to be scoped to the frame's root, and
  every selector whose subject was not literally `html` or `:root` got a
  descendant combinator — so `* { … }` or `.cls { … }` in a framed
  document matched everything inside the frame except the frame's own
  `<html>`, and `getComputedStyle` on that element reported no value for
  any property. The scope marker now also attaches directly to the
  subject compound, and lands before a pseudo-element rather than after
  it. Shadow scopes are unchanged: a shadow host is still not styled by
  its own shadow tree. Acid3 test 41 passes as a result.
* `:empty` is re-evaluated when a text node gains or loses content.
  Writing to `.data`/`.nodeValue`, or calling `appendData`,
  `insertData`, `deleteData` or `replaceData`, changes whether the node
  counts towards its parent's emptiness, but only child-list mutations
  marked the parent for restyle, so an element that became non-empty by
  having text written into an existing empty child kept its stale
  `:empty` match. Acid3 test 38 passes as a result; Acid3 now scores
  98/100, up from 96/100.
* Removed the IE-only `attachEvent` and `detachEvent`. They were exposed
  on Element, Document and Window as no-op stubs that returned true and
  registered nothing. Libraries still feature-detect them to select a
  legacy path: RequireJS, finding a native-looking `attachEvent`, bound
  its script-load callback to `onreadystatechange` instead of
  `addEventListener`, the stub swallowed it, and every module load ended
  in "Load timeout for modules". jQuery's test suite could not get past
  its RequireJS bootstrap before this.
* `DOMParser` reports the line and column of an XML parse error. The
  synthesized `parsererror` document carried the bare text "XML parsing
  error"; it now names the position the parser stopped at.
* `Array.prototype.sort` calls the comparator for identical elements.
  quickjs-ng skips the call when two slots hold the same JSValue and
  assumes the comparator would have returned 0. That is permitted by
  ECMAScript, which does not prescribe which comparisons a sort makes,
  but jQuery's `uniqueSort` learns that a collection holds duplicates
  precisely by being invoked with `a === b`, so duplicate nodes survived
  `.siblings()`, `.parents()`, `.nextAll()`, `.prevAll()`, `.closest()`
  and `.addBack()`. Carried as a wrap patch alongside the existing one,
  so the engine is still consumed unforked. jQuery's traversing module
  goes from 12 failing tests to 2.
* `getComputedStyle(el).someUnknownName` is `undefined` rather than the
  empty string. The proxy in front of a computed declaration answered
  every string key through `getPropertyValue`; its `has` trap already
  distinguished supported properties from unknown ones, and `get` now
  draws the same line.
* Fixed a double free of the response `Vary` header on the cancelled-fetch
  path in `net.c`. Cancelling a load — navigating away, pressing stop —
  freed `header_ctx.vary` twice, corrupting the heap whenever the
  response carried a `Vary`, which on the real web means most of the
  time.
* `Vary: Origin` no longer defeats the HTTP cache. `Origin` is now one of
  the headers the cache can resolve at lookup time: `net.c` computes the
  value it will send once and uses that same string both as the request
  header and as the cache selector, so the two can never disagree, and
  the absence of an `Origin` selects distinctly from any present one.
  Google serves its stylesheets `public, immutable, max-age=31536000`
  with `Vary: Origin`; those were being refetched on every load and are
  now cached.
* ES module fetches join the same request identity as every other
  subresource. The module loader passed no top-level URL, so a module was
  partitioned in the HTTP cache under its own site rather than the
  document's — two unrelated sites importing the same module shared one
  cache entry — and it neither coalesced with nor consumed the preload
  issued for the same `<script type=module src>`, since that preload
  carries the JavaScript `Accept` and the module fetch did not. It now
  passes the document URL and the script `Accept`.
* Shutting down no longer hangs a caller waiting on a coalesced fetch.
  `ns_net_drain` discarded queued fetch tasks without telling the
  coalescer, so a task that led a group left the group behind: blocking
  joiners waited on a condition nobody would signal again, and
  asynchronous joiners never had their callback run. A blocking joiner
  now also gives up when the network layer starts aborting, and in any
  case five seconds past the longest transfer timeout a leader can have,
  instead of waiting without a bound. These waits happen on worker
  threads that teardown joins, so one that never returned took the
  joining thread down with it.
* Dedicated and service workers expose the standard `Headers` interface.
  Worker-created and fetched `Request` and `Response` objects now retain
  case-insensitive header lookup and the other `Headers` methods instead of
  carrying a plain object. This lets Workbox inspect `Vary` while populating
  its caches rather than rejecting the cache operation.
* An `<iframe>` becomes visible as soon as its document loads, on a
  quiet page as well as a busy one. The UA sheet hides frames until the
  engine stamps `data-nd-frame-loaded` on them, but that stamp is
  written by the loader rather than through the scripted attribute
  path, so it never invalidated style. The frame kept the cached
  `display: none` and produced no box at all — its document parsed and
  its scripts ran, entirely unpainted — until some unrelated mutation
  happened to force a restyle. Pages with continuous script activity
  masked it; a page whose only content was a frame never showed it. The
  three places that add or remove the attribute now mark it dirty.
* Inline atomic boxes contribute their full height to the individual
  wrapped line that contains them. Multi-line form controls and table
  cells now reserve the correct vertical space instead of allowing later
  lines to overlap following content, fixing the Google footer position.
* The HTTP cache selects the right variant of a negotiated response.
  `cache.c` keyed entries on URL and partition and stored nothing about
  `Vary`, so a resource served `Vary: Accept` and referenced both as a
  stylesheet and as a script was fetched once and that single variant
  handed to both — a `<script>` element could receive CSS. Entries now
  carry the response's `Vary` and are keyed on a selector built from the
  request headers it names, with an indexed base key so a lookup can walk
  the variants stored for a URL and match the right one. `Accept`,
  `Accept-Language` and `User-Agent` are resolved; `Accept-Encoding` is
  ignored because bodies are stored decoded, which keeps the web's most
  common `Vary` from fragmenting the cache; anything else, including
  `Vary: *`, is not stored rather than stored wrongly. The preload scan
  deduplicates candidates on (URL, destination) instead of URL alone, so
  both variants are preloaded. The cache schema is versioned through
  `PRAGMA user_version` and an upgrade discards the old cache.
* The speculative preloader hands its bytes to the loader that needs
  them through a single deduplication point keyed on the request's
  identity. Preload responses used to be parked in a private store
  keyed on the bare URL and consulted ahead of the HTTP cache. That
  store ignored the cache partition, so within its 20-second window one
  site could be served bytes another site had fetched with that site's
  cookies; it ignored `no-store`; it recorded a placeholder for every
  fetch it started but only removed entries when a loader consumed one,
  so failed preloads and preloaded images — which nothing consumed —
  permanently occupied its 32 slots until the preloader silently
  stopped preloading anything. Deduplication now happens in one place.
  The in-flight coalescer keys on method, URL, cache partition and
  request headers rather than URL plus referrer, and every entry point
  joins it — `ns_net_request_async` and the blocking fetchers as well
  as `ns_net_fetch_async` — so a loader that arrives while a preload is
  still in flight waits for it instead of issuing a second request. A
  preload that finishes first is held in a preload map under that same
  key, handed over by the fetch layer itself so there is no window in
  which a resource is in neither place, and dropped when the next
  navigation begins. The preloader now sends the `Accept` header its
  consumer will send, so content-negotiated resources match. The
  separate external-script prefetcher, a third path over the same URLs,
  is gone. A page with six scripts and five stylesheets issues exactly
  one request per resource.
* Concurrent fetches of the same subresource share one network
  request. Three separate paths ask for a page's scripts and
  stylesheets — the speculative preloader, the external-script
  prefetcher, and the loader that actually consumes the bytes — and
  each issued its own request. Because they overlap, none of them
  could ever hit the HTTP cache, so an ordinary page fetched every
  script three times and every stylesheet twice, as confirmed at the
  origin. `ns_net_fetch_async` now keys uncancellable GETs on URL plus
  top-level URL, lets the first caller do the transfer, and hands each
  later caller its own copy of the response.
* `Worker.postMessage()` can transfer a `MessagePort` that also appears
  inside the message payload. The structured clone now substitutes the
  transferred endpoint during serialization, restores it in the receiving
  worker, and exposes the same port object through both `event.data` and
  `event.ports`; duplicate ports in a transfer list raise `DataCloneError`.
  This allows worker-backed consent and advertising libraries used by large
  news sites to initialize instead of failing before their message channel is
  connected.
* The root element's font reaches the rest of the page. The UA
  stylesheet declared `font-family: serif` on `html, body` together,
  and a UA declaration on `body` outranks inheritance from `html` — so
  a page styling only `html` (`html{font-family:"Helvetica Neue",
  "Segoe UI",Arial,sans-serif}` on lite.duckduckgo.com) had its font
  dropped at `body` and rendered in the UA serif default. The
  declaration now sits on `html` alone and `body` inherits it.
* A concrete font family is used when the system actually has it.
  `Arial`, `Helvetica`, `Segoe UI`, `Roboto` and the SF Pro names were
  rewritten to generic `sans-serif` unconditionally, which resolved
  through fontconfig to whatever the default sans happened to be —
  Noto Sans rather than the requested Segoe UI or Arial. Each name is
  now resolved against the installed families first, and substituted
  by `sans-serif` only when it is missing.
* Container queries no longer defeat incremental restyle. ns_css_compute
  runs twice per relayout when a page has containers, and the second
  pass — the one with the container map set — failed the incr_want test
  and then took the branch that frees the previous pass's computed
  styles. The next relayout therefore always started with an empty
  cache, so any page using `@container` re-cascaded every element from
  scratch, forever. The cache is now only discarded when incremental
  restyle is genuinely unusable, not merely because this is the
  container pass. Rules carrying a container condition also no longer
  contribute conservative invalidation keys: container_cond_matches()
  returns false whenever there is no container map, so those rules
  cannot affect the first pass, which is the only one incremental
  restyle runs in. On a 1610-element container-query page over 13
  relayouts, cascade time drops from 39ms to 8ms and style reuse goes
  from 0 to 1609 of 1610 elements per pass.
* The intrinsic width of a replaced box prefers its specified `width`
  and its decoded intrinsic size over the 200x150 placeholder an
  `<img>` without `width`/`height` attributes is given while it loads.
  Shrink-to-fit contexts — a floated `<a>` around a thumbnail above all
  — measured the placeholder, so the float reserved 200px while the
  image painted at its real width and the text ran underneath it.
* A flex item in a column container is clamped by its own `max-height`
  and floored by its own `min-height`. Only an explicit `min-height` was
  consulted, and only to keep a shrinking item from collapsing, so
  `height: calc(100% - 560px); max-height: 290px` kept the full
  calculated height and pushed everything below it down the page.
* A block that does not establish a block formatting context no longer
  grows to enclose the floats inside it. The float is registered with
  the enclosing formatting context instead, so it keeps shortening line
  boxes in the blocks that follow — a floated thumbnail followed by
  sibling `<div>`s now has the post text beside it rather than beneath
  a container stretched to the float's height.
* `window.scrollX`, `window.scrollY`, `pageXOffset` and `pageYOffset`
  are read-only accessors over the real viewport offset instead of
  writable data properties. Page script that assigned to one of them
  poisoned every reader, including `getBoundingClientRect`, which then
  reported every element at the top of the viewport.
* A flex item's content-based base size no longer has the item's own
  padding and border subtracted from it. `measure_natural_width` already
  returns a content-box size, so a padded item — a `<button>` above all —
  was assigned a base size that was short by exactly its horizontal
  padding, and its label was clipped.
* Flex items honour the automatic minimum size of CSS Flexbox §4.5: an
  item with `min-width: auto` and visible overflow never shrinks below
  its min-content width, so the last item in an over-constrained row
  keeps its label instead of being squeezed to nothing.
* `margin-left: auto` and `margin-right: auto` centre — or, on their
  own, right-align — a block-level replaced element, not just a table.
  A centred `<img>` whose used width came from `max-width` stayed
  against the left edge of its containing block.
* about:start sizes the search row to the splash image, so the two share
  the same left and right edges.
* Flex items are sized by the flex algorithm rather than by their own
  `width`. `layout_block` read `width` back out of the style and ignored
  the main size the container had assigned, so nothing ever shrank —
  `flex-shrink: 1` is the initial value, so every over-constrained flex
  row overflowed instead of fitting.
* The flex main axis is reversed when exactly one of
  `flex-direction: row-reverse` and `direction: rtl` applies, and items
  are then packed from the opposite edge, on wrapping and non-wrapping
  rows alike. `row-reverse` used to reverse the item order but still pack
  against the left edge, and `rtl` was ignored for the main axis.
* Grid row placement added the item's top margin on top of the
  margin-box origin that `ns_box.y` already means, so a negative margin
  moved the item the wrong way by twice the amount and cut the
  container's scrollable overflow to match.
* Text now wraps around floats the way CSS 2.1 §9.5 describes. Floats
  intrude into the line boxes of nested blocks in the same block
  formatting context, and an inline run that crosses the bottom of a
  float is split so the lines below it reclaim the full width — a long
  paragraph next to a short floated image no longer stays in a narrow
  column all the way down.
* Only tables, block-level replaced elements and boxes that establish a
  new block formatting context are moved aside by a float. Every other
  in-flow block keeps its containing block's width and overlaps the
  float, so backgrounds, borders and percentage widths next to a float
  resolve against the right width.
* CSS `display` is now a structured computed value (outer type, inner
  type, list-item flag, layout-internal kind) instead of a keyword string.
  Two-value syntax such as `display: flow-root list-item` reaches layout
  correctly; previously such elements lost their boxes. `-webkit-box` and
  `-webkit-inline-box` map to flex and inline-flex.
* Anonymous table boxes are generated around any run of table-internal
  siblings, so `display: table-row` and `display: table-row-group` outside
  a table lay out as tables instead of collapsing into surrounding text.
* Blockification follows the spec: the root element blockifies
  (`display: contents` on `<html>` computes to `block`) and flex and grid
  items report their blockified `display` to script, including items
  nested inside a `display: contents` wrapper.
* Cascade layers are ordered as a tree rather than by first-declaration
  order across the whole document: sublayers sort inside their parent, a
  layer's own declarations act as its implicit final sublayer, and nested
  anonymous layers stay nested instead of escaping to the top level.
* The incremental restyle pass identifies stylesheets by a parse-time
  serial instead of by address. A reparsed `<style>` reusing the freed
  block of the sheet it replaced could look unchanged and leave stale
  styles behind.
* `@scope` preludes are parsed against the grammar and invalid ones drop
  the rule; the prelude is serialized canonically.
* `StyleSheet.media` is a live `MediaList` that writes back to the owner
  node's `media` attribute.
* A unitless `0` on a length property computes to `0px`. Elements with a
  box hid this because their box edges are read back from layout; on an
  element with no box every such property reported `0`, and the inset
  resolver rejected `left: 0`, so the opposite inset of an out-of-flow
  box came back as `auto` instead of its used value.
* `getComputedStyle` returns an empty declaration list for an element
  that is not rendered — not connected, or outside the flat tree — as
  CSSOM requires, instead of a full style.
