Changelog:
=========
Significant changes in each release:

1.0.5:
======
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
