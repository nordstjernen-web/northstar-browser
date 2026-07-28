Changelog:
=========
Significant changes in each release:

1.0.5:
======
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
