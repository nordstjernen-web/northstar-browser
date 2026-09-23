Changelog:
=========
Significant changes in each release:

1.0.10:
=======
* An idle page no longer costs a frame every 16 ms. The window used to
  ask the engine for a frame at 60 Hz whenever the page had any timer,
  fetch or socket outstanding, and most of those frames came back
  unchanged; a page with a 50 ms `setInterval` that never touches the
  DOM made 443 frame requests in eight seconds and now makes one. The
  engine now wakes the window when there is something to show -- a DOM
  change, a `requestAnimationFrame`, an image or canvas update, a scroll,
  a navigation or an audio command -- and the 60 Hz loop runs only while
  something animates. `requestAnimationFrame` timestamps and CSS
  animations follow the display's frame clock (callbacks are 16.7 ms
  apart instead of wherever the engine thread happened to run), canvas
  drawing repaints without relaying out the page, and wheel scrolls and
  window resizes that arrive while the engine is busy are merged instead
  of queued one by one. Element `scrollTop` and paint-only `<img>` changes
  made from a timer now repaint at once; they were drawn only when
  something else happened to repaint.
* Forms submitted from pages in a legacy encoding send their fields in
  that encoding (or the first valid `accept-charset` label) as the
  Encoding Standard's encoders produce them. A single character the
  encoding could not represent -- a snowman on a Shift_JIS page, `Ā` on
  a windows-1252 page -- made the whole field fall back to UTF-8; it is
  now sent as `%26%23NNNN%3B` and the rest of the field keeps its
  encoding. Link queries use the same encoders, which fixes the
  Shift_JIS NEC/IBM duplicates (U+2170 is `FA40`, not `EEEF`), the
  GB18030-2022 mappings in gb18030 and GBK, and the ISO-2022-JP escape
  that must close a JIS X 0208 run before an unencodable character.
* `--trace=FILE` writes a Chrome trace-event file of where the engine's
  time goes -- each frame's tick, paint and copy, every cascade and
  layout, script evaluations, fetches, image decodes and the GTK thread's
  present -- for Perfetto or `chrome://tracing`, in the GUI as well as
  headless. `scripts/sample-profile.sh` now samples the engine thread
  instead of the GTK main thread, which in the GUI only ever waits.
* The Windows process mitigations are the ones intended. The policies
  were passed as bare numbers, and two were wrong: the call meant for
  ASLR set DEP (always on for 64-bit) and the one meant for the
  dynamic-code ban set Control Flow Guard (which cannot be enabled after
  start), so neither took effect. The policies are now named, forced
  image relocation applies everywhere, and headless and tooling runs --
  which load no GPU driver or shell extension -- also refuse to create
  executable memory.
* Images and MPEG-1 video that a page fetches are decoded on a worker
  thread. They were decoded on the engine thread as each download
  finished, so a video clip froze the page's scripts, timers and
  rendering for as long as it took to decode every frame -- over half a
  second for three seconds of 720p.
* The audio worker checks `file:` URLs itself. It opened any `file://`
  path it was handed, relying on the script bindings alone to refuse
  local files to `http(s)` pages; the page view now tells it whether the
  page is a `file:` document, and the worker refuses local paths
  otherwise. It also decodes the URL properly, so a local file whose name
  holds a space or other escaped character plays instead of failing to
  open.
* `OfflineAudioContext` and `createBuffer` refuse a channel count outside
  1-32, a sample rate outside 3000-768000 Hz, or more than 64 Mi samples
  in all, with the `NotSupportedError` the Web Audio specification names.
  A page could ask for `new OfflineAudioContext(1, 2e9, 44100)`, or raise
  `length` before `startRendering()`, and the renderer's 8 GB allocation
  aborted the whole browser.
* `canPlayType`, `navigator.mediaCapabilities` and
  `MediaSource.isTypeSupported` answer from one table of what the build
  can decode (`src/media_types.c`). `decodingInfo` claimed WebM, VP8, VP9,
  WAV and Opus played and MPEG and MP3 did not -- the reverse of the
  truth -- and `encodingInfo` claimed the same though nothing encodes.
  `canPlayType` now answers for the element it is called on (a `<video>`
  does not play an MP3), says "probably" to `mp2v` no longer (pl_mpeg is
  MPEG-1 only), and a build without the SDL2 mixer no longer advertises
  any audio type.
* A build configured with `-Daudio=disabled` links again. The audio stub
  that replaces the SDL2 mixer lacked `ns_audio_context_dispatch_blob`,
  which the page view calls for `blob:` media, so the final link failed.
* Links in pages that use a legacy encoding put non-ASCII query text
  into the URL in that encoding, as HTML's URL parsing requires: on a
  windows-1252 page `<a href="?q=é">` now reads back and navigates as
  `?q=%E9` rather than `?q=%C3%A9`, Shift_JIS, EUC-KR or Big5 pages
  send their own multi-byte sequences, and a character the encoding
  cannot represent becomes `%26%23NNNN%3B`. The fragment and every
  other part of the URL stay UTF-8.
* `crypto.subtle` follows the Web Cryptography API's algorithm
  normalization and error rules. Algorithm dictionaries are read the
  WebIDL way (a missing member or an out-of-range length is a
  TypeError, an unknown algorithm or hash a NotSupportedError), usages
  that do not fit the key are a SyntaxError, bad key material a
  DataError and a key used for the wrong algorithm, usage or key type
  an InvalidAccessError, where many of these used to come back as
  OperationError or as a plain Error. Keys are real `CryptoKey` objects
  whose `algorithm` carries `length`, `hash`, `modulusLength`,
  `publicExponent` or `namedCurve` as the key type requires; JWK import
  checks `kty`, `use`, `key_ops`, `ext`, `alg` and `crv` and rejects
  mismatched EC and Ed25519 key pairs; JWK export sets `alg`;
  `deriveBits` honours an absent, zero or non-byte length; Ed25519
  verification refuses small-order keys and signatures; compressed EC
  points import and export uncompressed; `wrapKey`/`unwrapKey` with JWK
  no longer fail to parse the unwrapped text; and `structuredClone()`
  copies a `CryptoKey`.
* `crypto.getRandomValues()` throws the errors the Web Cryptography API
  specifies: a `TypeMismatchError` DOMException for a `DataView` (it
  was a plain TypeError) and a `QuotaExceededError` for more than 65536
  bytes (it was a RangeError).
* `XMLHttpRequest.responseText` decodes the body in the charset named by
  `overrideMimeType()` or the response's `Content-Type`, and a byte-order
  mark overrides both, as the XHR standard specifies. The body was
  always read as UTF-8, so Shift_JIS or windows-1252 text came back as
  mojibake; `x-user-defined` now maps bytes 0x80-0xFF to U+F780-U+F7FF
  as the Encoding Standard defines.
* `TextDecoder` accepts every label of the Encoding Standard, not just
  UTF-8, UTF-16 and windows-1252, so `new TextDecoder("shift_jis")` or
  `"gbk"` no longer throws a RangeError, and it decodes with the same
  decoders as documents: partial sequences of any encoding carry over
  between `{stream: true}` calls and invalid input is replaced the way
  the standard specifies. `TextDecoder` and `TextEncoder` are real
  interfaces with `encoding`, `fatal`, `ignoreBOM` and the methods on
  their prototypes, `TextEncoder.encodeInto()` is native and counts
  what it read in UTF-16 units, `TextDecoderStream` reports the
  decoder's canonical encoding name and its `fatal`/`ignoreBOM`, and
  `TextEncoderStream` joins a surrogate pair split across two chunks.
* Pages in the legacy East Asian encodings -- Big5, EUC-JP, Shift_JIS,
  ISO-2022-JP, EUC-KR, GBK and gb18030 -- decode as the Encoding
  Standard specifies. The whole document went through iconv in one
  call, so a single sequence iconv's table lacked (Big5 has hundreds of
  them, and Shift_JIS pages use NEC and IBM rows) failed the conversion
  and the page was read as UTF-8 or windows-1252 instead. Each encoding
  now has its own decoder that follows the standard's state machine,
  looks characters up in the standard's index tables (those lexbor
  already ships, so decoding no longer depends on the platform's iconv)
  and puts U+FFFD where a sequence is invalid. Single-byte encodings
  decode the same way, so a windows-1252 page with a byte like 0x81 no
  longer falls back to another encoding, and the labels that name the
  replacement encoding give a single U+FFFD rather than decoded text.
* A frame sandboxed without `allow-same-origin` can no longer reach the
  page that embeds it. A `srcdoc` or same-origin frame sandboxed that way
  got the embedding page's real window as `parent` and `top`, so its
  script could read and change the parent's DOM, `document.cookie` and
  `localStorage`; it now gets the same restricted window proxy as a
  cross-origin frame, where anything but `postMessage`, the `location`
  setter and a few navigation properties throws `SecurityError`.
* `--dump=print:FILE` works under the Linux sandbox. The output directory
  of a print dump was not made writable the way a `png:` or `pdf:` dump's
  is, so the pagination check `docs/building.md` describes failed with
  "failed to create PDF surface" and wrote nothing.
* The documentation matches the source again. `docs/architecture.md`
  describes the threads, the two headless paths, printing and
  diagnostics as they are, and its diagram is regenerated from
  `scripts/gen-architecture.py` instead of the old picture of a render
  protocol that no longer exists. `SECURITY.md` lists the Windows
  mitigations that are actually applied, the real Landlock paths, which
  CSP directives, mixed-content rules and SRI checks are enforced, how
  the address bar shows internationalised hosts, and the known gaps.
  `docs/compliance.md` leads with the latest reading per area and drops
  gaps that have closed, `docs/building.md` documents every command-line
  option and environment variable, and the manual page does the same.
* `reportError(value)` reports the value the way an uncaught exception
  is reported: it fires a cancelable `error` event at the window with
  the caller's file, line and column and `event.error` set to the value,
  and logs it only if no listener cancels the event. It used to print
  the value to the console and nothing else, so error trackers never
  saw it. The `error` events for uncaught exceptions are now
  `ErrorEvent` instances.
* `Object.prototype.toString` names the natively implemented interfaces:
  a `MessagePort`, `XMLHttpRequest`, `MessageChannel`, `DOMParser` and
  the like read `[object MessagePort]` and so on instead of
  `[object Object]`, which scripts use to tell platform objects apart.
* URL setters behave as the URL Standard describes where the parser
  library does not: `url.host = "example.com:"` or `"example.com:abc"`
  changes the host and keeps the port, an out-of-range port still sets
  the host, clearing the host of a non-special URL with credentials or a
  port is refused, and a URL with an empty host cannot gain a username,
  password or port. `new URL("??a=b").searchParams` keeps the second
  `?`. Links (`<a>`, `<area>`) whose `href` does not parse now report
  `":"` as the protocol and ignore setters instead of working on the raw
  string, and an `href` containing a NUL character is no longer cut
  short there.
* An iframe whose `load` handler navigates it again (for example to
  `about:blank`) no longer freezes the page. Each reload ran inside the
  same loop that processed the previous one, so timers and rendering
  never got a turn; the next load now waits for the following frame.
* `structuredClone()` and `postMessage()` follow the HTML serialization
  rules more closely. Transferring an `ArrayBuffer` detaches it (a
  detached one, a duplicate, or an object that cannot be transferred
  throws `DataCloneError`), resizable buffers keep their
  `maxByteLength`, views of one buffer still share a buffer in the
  copy, sparse arrays keep their length, `BigInt` wrappers survive, and
  errors keep only an own `message` and a standard name. A page that
  replaces `window.structuredClone` no longer changes what
  `postMessage()` sends.
* `Blob` and `File` follow the File API. The constructors accept any
  iterable of parts and reject strings, numbers and plain objects,
  honour `endings: "native"`, read their options in the specified
  order, drop a `type` with non-printable characters and copy
  `ArrayBuffer` parts (a detached one is empty); `size`, `type`, `name`
  and `lastModified` are prototype getters, `slice()` clamps like other
  browsers and validates its content type, `blob.bytes()` exists, and
  `String(blob)` is `[object Blob]`. Calling `Blob()` without `new`
  throws.
* Setting `meta.content`, `textarea.rows` or `frameset.rows` from script
  changes the attribute; the assignments were silently ignored.
  `role`, `ariaLabel`, `ariaBusy` and the other ARIA properties read
  `null` when the attribute is absent (as in other browsers) instead of
  an empty string or a default such as `"false"`, setting them to
  `null` removes the attribute, and the numeric and text ARIA
  properties such as `ariaValueNow` and `ariaLevel` exist at all.
  `font.size` is a string, `textarea.cols = 0` falls back to the
  default, `progress.max` ignores non-positive values and parses its
  attribute with the HTML number rules (`"5%"` is 5), assigning `null`
  to the legacy colour and margin attributes clears them, `object.data`
  resolves an empty value against the base URL, and setting `label` or
  `defaultValue` is seen by mutation observers.
* `innerText` and `textContent` of a shadow host no longer include the
  shadow tree's text, text inside an inline `<svg>`'s `<text>` elements
  is part of `innerText`, a `visibility: hidden` paragraph or `<br>`
  adds no line breaks of its own, and an `<optgroup>` outside a
  `<select>` keeps its text. Setting `innerText` or `outerText` to a
  string with a NUL character keeps the text after it instead of
  cutting it off there.
* A `fit-content(<length>)` grid track sizes to its content up to the
  limit. It behaved like `minmax(auto, <length>)` and always grew to
  the limit, so `fit-content(70px)` holding 30px of text was 70px wide
  and an empty one kept its full size instead of collapsing.
* Rows repeated by `grid-template-rows: repeat(auto-fit, ...)` that no
  item occupies collapse to zero height, as auto-fit columns already
  did, instead of keeping their size like `auto-fill`.
* Line names inside `repeat(auto-fill, ...)` and `repeat(auto-fit,
  ...)` are repeated with the tracks. They were kept only once and the
  names after the repetition kept their unrepeated line numbers, so
  `grid-column: b 3` and names following the `repeat()` placed items on
  the wrong lines, and the resolved `grid-template-columns` of such a
  grid dropped every line name.
* `getComputedStyle()` reports `grid-template-columns` and
  `grid-template-rows` of an element that is not a grid, and
  `grid-auto-columns` and `grid-auto-rows` of any element, as the
  computed track list: `repeat()`, `minmax()`, `fit-content()` and line
  names stay as written and only lengths become pixels, so `[a] 1em
  repeat(2, 2em [b] 3em)` reads back as `[a] 16px repeat(2, 32px [b]
  48px)` instead of an expanded list with the functions dropped.
* Flexible grid tracks honour their fixed minimums. `minmax(40px, 1fr)
  minmax(0, 1fr)` in a 60px grid gave the second column 30px and
  overflowed; a track whose share falls below its minimum now keeps the
  minimum and the others split what is left (40px and 20px). Space that
  `fr` factors summing below 1 leave over stretches `auto` tracks, and
  in a grid with a fixed height a `minmax(0, 1fr)` row no longer grows
  to fit its content, nor does an item spanning several rows including a
  flexible one stretch them.
* `calc()`, `min()`, `max()` and `clamp()` values set from script read
  back simplified the way CSS Values 4 serializes them: terms of the
  same unit are combined, absolute units become `px`, and a sum lists
  its number, then its percentage, then its dimensions sorted by unit,
  so `calc(1vh + 2px + 3%)` reads back as `calc(3% + 2px + 1vh)` and
  `min(1px + 1%)` as `calc(1% + 1px)`. Only a value whose rounded
  six-digit form is exact is rewritten, so `calc(100% / 3)` keeps its
  full precision for layout.
* `grid-template-rows: repeat(auto-fill, ...)` repeats its rows. Only
  columns expanded an automatic repetition; rows kept a single copy of
  the pattern. Rows now repeat as many times as fit the grid's height,
  or its `max-height` or `min-height` when the height is not fixed.
* `repeat(auto-fill, ...)` and `repeat(auto-fit, ...)` count their
  repetitions from the space the other tracks and gaps leave, and size a
  `minmax()` track by its fixed maximum; `10px 20% repeat(auto-fill,
  35px)` in a 200px grid made five repetitions that overflowed instead
  of four. Columns an item adds past the explicit grid take their size
  from `grid-auto-columns` instead of always being `auto`.
* Math functions keep the sign of zero. `calc(-0)` was rewritten to
  `calc(0)` before it was evaluated, so `1 / sign(calc(-0))` came out
  as `infinity` instead of `-infinity`; `min()`, `max()` and `clamp()`
  now order `-0` below `0`, `round()` with an infinite step, `mod()`
  and `rem()` return the signed zero CSS Values 4 specifies, and `-0`
  still reads back as `0` through `element.style`.
* A `calc()` that evaluates to NaN or an infinity no longer produces
  NaN geometry. `width: calc(NaN * 1px)` laid out and read back as
  `nanpx` and `calc(infinity * 1px)` as an unusable infinite length;
  computed values now clamp NaN to 0 and infinities to the largest
  (or most negative) representable length, as CSS Values 4 specifies,
  for lengths, percentages, numbers and the `scale` and `translate`
  properties.
* Grid items that span several `auto`, `min-content` or `max-content`
  columns size those columns. Only single-column items were measured,
  so a heading spanning two content-sized columns could overflow them;
  a spanning item's minimum and maximum content widths are now spread
  over the columns it crosses (narrower spans first, respecting each
  column's maximum), as the grid sizing algorithm specifies.
* `grid-template-columns/-rows: subgrid` checks its line-name list:
  only `[names]` groups and `repeat(N | auto-fill, [names]...)` may
  follow `subgrid` (so `subgrid 1px` or `subgrid repeat(2, 1px)` is
  dropped), the names -- including empty `[]` groups -- are kept for
  `getComputedStyle()`, and `subgrid` on an element whose parent is not
  a grid reports `none`, as it is laid out as an ordinary grid.
* A grid with only a `min-height` grows its `fr` rows to fill it. The
  common page skeleton `min-height: 100vh; grid-template-rows: auto 1fr
  auto` sized the middle row to its content and left the footer halfway
  up the screen; the flexible rows now share the space the minimum
  height leaves, as they already did for a fixed `height`.
* Grids that use `grid-template-areas` are laid out by the full grid
  algorithm. They went through a separate, reduced code path that gave
  `fr` rows no share of a fixed container height (a header / `1fr` /
  footer page left the footer under the header instead of at the
  bottom), sized every column the areas added as `1fr` instead of by
  `grid-auto-columns`, and ignored `align-content`, `align-items` and
  `grid-auto-rows`. Area names also resolve as line names, so
  `grid-row: main` and `grid-column: main-start / main-end` place items
  on a named area.
* `getComputedStyle()` reads the `grid-area`, `grid-row` and
  `grid-column` shorthands (they read empty), and `grid-template`
  reports a declared track list as declared, falling back to the
  laid-out tracks only when no template is set.
* `align-self` and `justify-self` values `self-start` and `self-end` on
  a grid item use the item's own writing mode. A `vertical-rl` item
  aligned with `justify-self: self-start` sits at the right edge of its
  area, and a vertical item with `direction: rtl` aligns to the bottom;
  only the item's `direction` was considered before, as if every item
  were horizontal.
* The `grid` and `grid-template` shorthands follow their grammar and
  set every longhand they cover. Rows written in the template form
  without a size (`"a a" "b b" 1fr`) are `auto`, where the first size
  given was applied to the first row; line names between rows merge
  (`"a" [x] [y] "b"` names one line `x y`); omitted longhands are reset,
  so `grid-template: auto / 1fr 1fr` clears an earlier
  `grid-template-areas` and `grid: auto-flow / ...` resets
  `grid-template-rows`; and invalid values (`grid-template: 10px`,
  `"a" 10px 10px`, `none / "a"`) are dropped instead of partially
  applied. `grid-auto-flow` rejects `auto` and repeated keywords.
  `element.style` and `getComputedStyle()` read both shorthands back,
  composed from their longhands, and a track list keeps its
  `repeat()` and `fit-content()` when read back from a stylesheet or a
  style attribute.
* `grid-row`, `grid-column`, `grid-area` and their `-start`/`-end`
  longhands follow the `<grid-line>` grammar. Values such as `0`,
  `span`, `span -2`, `1 2`, `auto 1` or a fifth `grid-area` part were
  accepted and could override a valid earlier declaration; they are now
  dropped. Omitted parts are filled in as the spec says (a named line
  repeats, anything else becomes `auto`), so `grid-row: 2` also resets
  an earlier `grid-row-end`, and `element.style` reads the values back
  in their shortest canonical form (`2 i span` reads `span 2 i`, `1 /
  auto` reads `1`). Line and area names keep their case, as custom
  identifiers are case-sensitive.
* Viewport units inside an iframe measure the iframe. `vw`, `vh`,
  `vmin` and the `sv*`/`lv*`/`dv*` variants in a frame's document
  resolved against the top-level window unless the frame's size came
  from its `style` attribute; the frame's own computed width and height
  now define its viewport. A `calc()`, `min()`, `max()` or `clamp()`
  mixing viewport units with other units also recomputes them for the
  current viewport, where it used to keep the size the window had when
  the stylesheet was first parsed -- so `calc(100vh - 60px)` follows a
  window resize and resolves per frame.
* `sibling-index()` and `sibling-count()` in a container size query
  resolve against the container element, instead of always counting 1.
* Flexbox follows `writing-mode`: in a vertical container `row` runs
  along the vertical inline axis and `column` along the horizontal block
  axis, and vertical items take a central baseline for baseline
  alignment, so vertical-rl/vertical-lr flex layouts no longer lay out
  as if they were horizontal.
* In a column flexbox an image, video or SVG stretches across the
  container like any other item (keeping its aspect ratio, capped by
  `max-width`), and a stretched item's percentage `max-width` resolves
  against the container instead of being applied a second time to the
  item's own width.
* Scripts that wait for web fonts measure the page in those fonts.
  `document.fonts.ready` could resolve while an `@font-face` font was
  still being fetched -- or after it had arrived but before the page was
  laid out again -- so `offsetWidth` and friends in its callback still
  reported the fallback font's metrics. A font that finishes loading now
  makes the next layout query lay the page out again, the fonts the
  page's stylesheets reference are requested before `ready` is decided,
  `document.fonts.status` reads `loading` meanwhile, and
  `document.fonts.load()` and `FontFace.load()` wait the same way.
* Headless `--wpt`, `--inspect` and image dumps load the document's
  images before its scripts run and fire their `load` events, so tests
  and scripts that measure images at `load` see their real sizes.
* `element.style.color` and the other colour properties read a colour
  back in the form CSS Color 4 and 5 give it. `lab(20 0 10/50%)` came
  back exactly as typed and `color(srgb 10% 10% 10%)` kept its
  percentages; they now read `lab(20 0 10 / 0.5)` and
  `color(srgb 0.1 0.1 0.1)`. A `color-mix()` lists its colours and
  percentages in normalised form, a relative colour keeps its `from`
  form around a normalised origin, and a `calc()` inside a colour reads
  back as its simplified value.
* `color-mix()` and relative colors that use `currentcolor` paint in
  the element's own text colour. The colour parser knew nothing of
  `currentcolor` inside a function, so a Tailwind-style
  `background-color: color-mix(in oklab, currentColor 10%, transparent)`
  was thrown away and the box stayed transparent. Such a value now keeps
  its `currentcolor` and is worked out for each element, so a child that
  inherits it under a different `color` gets its own shade, and
  `color: currentcolor` takes the parent's colour instead of the default.
  Colours are also kept at full precision in the space they are written
  in: a `none` component is filled from the other colour of a mix
  instead of reading as zero, `color-mix()` takes a percentage before
  the colour, any number of colours and every hue interpolation method,
  and `getComputedStyle` reports `lab()`, `oklch()`,
  `color(display-p3 ...)` and mixed colours in their own notation rather
  than rounding them to `rgb()`.
* An `<iframe>` whose source is an image shows it in an image document,
  as a top-level navigation does, instead of parsing the image bytes as
  HTML text.
* Images in the page start downloading as soon as the document is laid
  out, not at the first paint; each `<img>` fires its `load` or `error`
  event when its image arrives, `complete` and `naturalWidth` report
  it, and the window `load` event waits for the page's (non-lazy)
  images, as HTML says, instead of firing before any of them loaded.
* Restyling after a script change no longer gives an element the
  styles of a stranger's child. Elements whose parents have the same
  computed style share one computed style, and parents were told apart
  by a number that restarted with every style pass, so after a class,
  id or attribute change a freshly styled parent could get the same
  number as an untouched one elsewhere, and their children swapped
  inherited values -- custom properties, colours, fonts -- until the
  next full restyle. Parents that were never numbered, such as those
  using `attr()`, all counted as the same parent even on a first load.
  Every computed style now gets its own number that is never reused.
* Elements whose interface is plain `HTMLElement` (`article`,
  `section`, `b`, `abbr`, `nav`, `summary`, ...) and valid custom element
  names are no longer `HTMLUnknownElement` instances; truly unknown tags
  report `[object HTMLUnknownElement]`, and `listing` / `xmp` are
  `HTMLPreElement`s.
* `relList.supports()` answers per element: `<link>` reports the link
  types it acts on (stylesheet, icon, preload, modulepreload, ...),
  while `<a>`, `<area>` and `<form>` report only `noopener`,
  `noreferrer` and `opener`; `<form>` now has a `relList`.
* Absolutely positioned children of flex containers land where they
  should. One with `margin: auto` and no `left`/`right` sat 40px in from
  its static position instead of at it (auto margins only centre a box
  between two insets), `align-self: baseline` and `last baseline` put it
  at the wrong edge of a `wrap-reverse` line, and in a wrapping container
  whose lines are stretched by `align-content`, `flex-end` and `center`
  items stayed at the top of the taller line.
* A float in a column too narrow for it is as wide as its longest word
  or widest unbreakable child, like in other browsers, instead of being
  squeezed below it so that its content spills out of its border. A
  child with `width: 0` also counts as zero wide when its parent shrinks
  to fit, rather than as wide as its content.
* `min-content`, `max-content`, `fit-content` and `stretch` work in
  `min-width`/`max-width` and `min-height`/`max-height` as well as in
  `width`/`height`, on inline-blocks, floats, flex items and absolutely
  positioned boxes. The width keywords measured a box that had a pixel
  `width` of its own as that width -- so `width: 0; min-width:
  min-content` stayed 0 and `width: 500px; max-width: max-content`
  stayed 500px -- the height keywords were ignored, and a flex item's
  `width: min-content` or `max-width: fit-content` was sized from its
  max-content width. A flex item with a `min-content` or `max-content`
  height is no longer stretched to the line.
* A `<fieldset>`'s `<legend>` sits in the frame's top border, the way
  every browser draws it, instead of being a full-width line of text
  inside the frame. The legend is shrink-wrapped to its text whatever
  its `display`, its border box is centred on the top border, the
  border is left out behind it, and the fieldset's content starts below
  whichever of the two reaches further down. `align="center"` /
  `"right"` (mapped to `justify-self`), `justify-self` and auto margins
  place it along the border, and a right-to-left fieldset starts it on
  the right. Content written before the legend in the source now joins
  the text after it, and a fieldset, like its rendered legend, contains
  its floats.
* A type selector that follows another simple selector in a compound
  (`[foo]i`, `.a*`) is a parse error instead of silently matching, and
  `selectorText` / `cssText` drop comments and write an attribute
  selector's case flag as ` i]` / ` s]`.
* A one-line text input (`text`, `search`, `tel`, `url`, `email`,
  `password`) never uses a line height smaller than `normal`, as HTML
  requires, so `input { line-height: 1px }` no longer squashes the field
  and clips its text; `getComputedStyle` reports the used value.
* The user-agent sheet hides `area` and `base`, leaves `source` and
  `track` at `display: inline`, keeps `input type=hidden` and a `form`
  the parser left inside a table part hidden even against author
  `!important`, and lays out `embed[hidden]` as a 0x0 inline box, as
  HTML's rendering section lists.
* Setting `document.title` on a page without a `<title>` creates one
  that later reads (and `getElementsByTagName`) can find, reports the
  change to mutation observers, and does nothing when there is no
  `<head>`, as HTML says.
* WebCrypto AES-CTR honours the `length` parameter: a length outside
  1..128 is an `OperationError`, the counter wraps within its low
  `length` bits instead of carrying into the nonce, and a message long
  enough to reuse a counter block is refused.
* A comment or a `display: none` element (a `<script>`, a hidden
  `<span>`) between pieces of inline content no longer ends the line:
  `foo<!-- -->bar` and `a<script></script>b` lay out as one line of text
  instead of two.
* `display: contents` on an element that cannot be unboxed (`img`,
  `input`, `iframe`, `video`, `br`, an outermost `svg` and the other
  replaced elements and form controls CSS Display lists) computes to
  `display: none`.
* Documents in quirks mode get the extra user-agent rules HTML lists
  for them: tables reset font, line-height, white-space and text-align
  instead of inheriting them, forms keep a 1em bottom margin, stray list
  items put their marker inside, text inputs and textareas size with
  `border-box`, and left- or right-aligned images get a 3px gap.
* In quirks mode a `<td nowrap>` or `<th nowrap>` that also has a
  fixed pixel `width` wraps normally, as HTML says legacy pages expect.
* `getComputedStyle().transform` resolves only the `transform`
  property; the individual `translate`, `rotate` and `scale` properties
  stay separate as CSS Transforms 2 says, instead of being folded into
  the reported matrix.
* In a `<picture>`, the `width` and `height` of the `<source>` that is
  selected size the `<img>`, as HTML's dimension attribute source rule
  says, so art-directed images with different proportions per
  breakpoint reserve the right box.
* An `<img>`, `<video>` or `<input type=image>` with `width` and
  `height` attributes computes `aspect-ratio: auto W / H`, as the HTML
  rendering rules map them, so a responsive image styled `width: 100%;
  height: auto` reserves its height before it loads; once it has loaded
  its own ratio takes over, as the `auto` says.
* An `<iframe>` has the 2px inset border the rendering section gives it,
  which `frameborder="0"` (or any value that is not a non-zero integer)
  removes, and `<video>` computes `object-fit: contain`.
* `<fieldset>` has the rendering section's default style -- a 2px
  groove border, 2px inline margins, `0.35em 0.625em` block and `0.75em`
  inline padding and a `min-content` minimum inline size -- instead of a
  1px solid border and 8px margins, and the deprecated `ThreeDFace`
  system colour resolves to `ButtonBorder` as CSS Color 4 maps it.
* Form controls compute `appearance: auto` as the HTML rendering rules
  give them (`none` for hidden, file and image inputs), and
  `getComputedStyle` answers for prefixed aliases such as
  `-webkit-appearance` and `-webkit-border-radius` -- they were parsed as
  aliases but read back as empty strings, which feature-detection code
  takes to mean the property is unsupported.
* Legacy table borders follow the HTML rendering rules. `<table
  border>` draws an outset border of that width in the text colour (1px
  when the value does not parse) and inset 1px cell borders, instead of a
  fixed `#888` solid line; `frame` picks which sides are outset or
  hidden; `rules` collapses the borders, hides the table's own and draws
  the rules on cells, rows or groups as specified; `bordercolor` sets the
  border colour; `cellspacing` and `cellpadding` accept any non-negative
  integer; and `align=middle`/`absmiddle` centres a row or cell.
* `innerHTML`, `outerHTML`, `insertAdjacentHTML` and
  `Range.createContextualFragment` parse markup in the context
  element's namespace. Inside an `<svg>` every element came out as an
  HTML element with a lower-cased name -- `<linearGradient>` became an
  HTMLUnknownElement called `lineargradient` -- so gradients, filters and
  shapes that D3, icon libraries and chart code insert that way were
  never drawn.
* `:focus-visible` is its own pseudo-class instead of another name for
  `:focus`. Clicking a button or link no longer puts it in the
  `:focus-visible` state, so pages that draw focus rings only for
  keyboard users -- the usual `:focus:not(:focus-visible) { outline: 0 }`
  -- stop showing a ring after every click, while Tab, access keys, focus
  moved by script without a preceding click, and any text field still
  match, as the Selectors spec suggests.
* In nested CSS, declarations that follow a nested rule apply after it,
  as the CSS Nesting spec's nested-declarations rule requires. They were
  gathered into one rule at the top of the parent, so
  `.card { padding: 1px; @media (...) { padding: 20px } padding-top: 3px }`
  ended with a 20px top padding instead of 3px.
* Switching the desktop between light and dark, or turning animations
  off, now reaches `prefers-color-scheme` and `prefers-reduced-motion`
  rules in style sheets that were already loaded. Media queries are
  evaluated when a sheet is parsed, and the parsed-sheet caches were
  only cleared for print, so a page reloaded after a theme change kept
  the rules chosen for the old one.
* Invalid style rules are dropped the way CSS Syntax requires. After a
  stray `;` or `}` between rules the parser resumed at the next rule,
  and a selector list with an empty item, a trailing comma or trailing
  junk (`.a, {`, `.b) {`) was applied from whatever parsed; other
  browsers read the stray token as the start of the next rule's
  selector, which then fails, and drop the whole rule. Northstar now
  does the same, so a stylesheet that relies on those errors to hide a
  rule from browsers no longer shows it.
* A comment inside a media query no longer disables it. `@media
  (min-width: 100px) /* desktop */ { ... }` was never applied, and the
  same went for `<link media>`, `<style media>` and `matchMedia()`; the
  media query parser now treats a comment as whitespace, as `@supports`
  already did.
* `<meta charset="utf-16">` no longer turns a page into CJK mojibake. A
  document that declares UTF-16 in a meta tag is necessarily ASCII-
  compatible, and the HTML encoding sniffing rules read it as UTF-8, but
  the declaration was handed straight to the converter. Declared labels
  now go through the WHATWG encoding table for decoding too, so an
  unknown label is ignored instead of guessed at, `euc-kr` decodes as
  the Windows-949 superset the web means by it, and `iso-8859-8-i` is
  understood.
* The rest of the default stylesheet follows the HTML rendering rules
  instead of Northstar's own taste. Text is `CanvasText` (black, not
  `#1a1a1a`), links are `LinkText`/`VisitedText` (not a Wikipedia blue),
  `<code>`, `<kbd>` and `<samp>` no longer get a grey background, padding,
  a border or `pre-wrap` -- the padding also slipped a stray space into
  the text around every inline code span, and the background showed
  through dark `pre code` themes -- `<pre>` has 1em margins and a normal
  line height, `<small>`/`<big>` are `smaller`/`larger`, `<address>` is
  not grey, `<legend>` is not bold, a non-modal `<dialog open>` is
  positioned out of flow and centred rather than pushing the page down,
  and form controls no longer inherit `letter-spacing`, `line-height`,
  `text-transform`, `text-indent` or `text-shadow` from the text around
  them.
* A page can hide its own iframes again, and `<object>` shows its
  fallback content. The default stylesheet hid every frame, object and
  embed with `!important` and forced a loaded iframe to `display: block
  !important` -- and a user-agent `!important` beats anything an author
  writes, so an OAuth or payment helper iframe styled `display: none`
  was laid out in the page once it loaded, and `<object data=x.swf><p>Get
  Flash</p></object>` showed nothing at all. Those rules are ordinary
  defaults now; an `<object>` that has not loaded a document renders the
  content inside it, as HTML specifies.
* A `<summary>` draws its disclosure triangle as a list marker, the way
  the HTML rendering rules style it, so `list-style: none`, `display:
  block` or `::marker { content: "" }` removes it as in other browsers;
  it was inserted into the text itself and could not be removed. The
  summary is no longer bold, and the default stylesheet no longer pushes
  every paragraph, list, table and heading inside a `<details>` 16px to
  the right -- once per nesting level. Any element with `display:
  list-item` now gets a marker, not just `<li>`.
* More of the legacy presentational attributes work the way the HTML
  rendering rules map them. `<font size>` accepts `+n` and `-n` relative
  to size 3 and maps to the absolute font-size keywords, so `size="+1"`
  is 18px rather than 10px, `size="-1"` is no longer ignored, and nested
  `<font>` elements no longer compound. `<body>`'s `marginheight`/
  `topmargin` and `marginwidth`/`leftmargin` set its margins, falling back
  to the containing iframe's `marginheight`/`marginwidth`; `background`
  sets the background image
  of a body, table, row or cell; `<br clear>` moves what follows below
  the floats; `<caption align>` places and aligns the caption; `<nobr>`
  does not wrap; and `<marquee>` is shown as a (still) inline-block
  instead of being hidden with its text.
* Italics, underlines, strike-throughs and superscripts come from CSS
  rather than from the tag name. The text collector slanted every `<i>`,
  `<em>`, `<cite>` and `<dfn>` whatever its `font-style` said -- so Font
  Awesome's `<i class="fa">` icons were drawn slanted -- and a
  `font-style: normal` span inside italic text stayed italic. `<sup>` and
  `<sub>` were shrunk twice (the stylesheet's size and then a fixed 0.75
  scale) and raised a fixed 4px, while `vertical-align: super` on any
  other element did nothing. Now `font-style` alone decides, in both
  directions; `vertical-align: super`, `sub` and lengths raise or lower
  text by the amounts other browsers use, relative to the parent's font
  size; a `position: relative` inline element's `top`/`bottom` moves its
  text; and the default stylesheet gives `<u>`, `<ins>`, `<s>`, `<del>`
  and `<strike>` their decorations, so `getComputedStyle` reports them.
  `<ins>` and `<del>` are no longer tinted green and red.
* `<hr>` is drawn by its borders, as the HTML rendering rules style it:
  a 1px inset rule in grey with `0.5em auto` margins. It used to be a
  1px grey background with a line of its own painted 4px below the top
  whenever it had no border -- so Bootstrap's `border: 0; border-top:
  1px solid` rule showed a second, grey line under the first, and an
  `<hr>` restyled as a coloured bar had a stripe drawn across it. The
  `color`, `noshade` and `size` attributes follow the rendering rules
  too: `size` is the rule's full height, including its borders.
* Style matching skips rules whose ancestors cannot be there. While the
  style pass walks the document it keeps a small counting filter of the
  tag names, ids and classes of the current element's ancestors, and a
  rule such as `ul li.menu a[href]` is dropped at once for a link with
  no `li.menu` above it instead of walking the ancestor chain. The style
  pass on a page of 16,000 elements and 3,000 rules takes 195 ms
  instead of 2 seconds. Rules inside `@scope`, and matching relative to
  a scope, still take the full path.
* The render pipeline's zoom factor scales every element's font size
  exactly once. Elements that share one computed style value -- five
  identical list items, say -- had that value multiplied once per
  element, so at 150% the fifth sibling's text came out 7.6 times too
  big. The pipeline is only driven at 100% today, so no page rendered
  differently yet.
* Numbering a long ordered list is linear again. Each marker counted
  every `<li>` before it, reading their `value` attributes, so a list of
  4,000 references cost 8 million sibling steps per layout with inside
  markers and again on every paint; a layout or paint pass now numbers a
  list's items in one sweep the first time a marker asks. Laying out
  4,000 items with `list-style-position: inside` takes 157 ms instead of
  253 ms.
* An image with a CSS `filter` is filtered once, not on every paint.
  The filtered copy was rebuilt from the full-resolution pixels each
  time the image was drawn -- once per `<img>` when a page repeats an
  image -- and is now kept with the decoded image until the filter
  changes. A page showing one 800x800 image forty times in grayscale
  renders in 2.0 s instead of 4.0 s, as fast as without the filter.
* The CSS `font-family` list of a text run is turned into a font name
  once rather than three or four times per run on every layout and
  paint. The answer is remembered per family list until the system font
  set changes or a web font finishes loading; laying out a page of 3,000
  paragraphs with long font stacks is 12% faster.
* Relayouts no longer redo work on unchanged style sheets. Each one
  re-resolved every `url()` in every cached sheet, copied every linked
  sheet to scan it for viewport media queries, and built the lookup key
  for the page's inline styles by copying all of their text; on a page
  with 1.5 MB of CSS that was 100 ms per relayout and is now 9 ms. The
  inline-style cache now also includes the page's base URL, so a page
  whose `<style>` text matches one visited earlier no longer loads
  `url()` images relative to the earlier page.
* A page that declares `container-type` but has no `@container` rules
  and no container units no longer styles itself twice on every layout.
  The second, container-aware style pass ran whenever any element was a
  container, and threw its result away; on a 12,000-element page with
  3,000 rules that cost 700 ms per relayout. It now runs only when a
  sheet has `@container` rules or a container unit was resolved, and a
  `@container` condition is parsed once per rule instead of once per
  rule and element.
* Removing or inserting children no longer counts the node's position
  among its siblings when no `Range` exists. The bookkeeping that keeps
  live ranges pointing at the right offsets measured the index of every
  moved node by walking its previous siblings, so emptying a 20,000-item
  list from the end took 13 seconds and 5,000 insertions into the middle
  of a 10,000-child element took 4; they now take 29 ms and 41 ms. Pages
  with live ranges keep the exact same range updates.
* Changing an element's `class` or `id` to a name no style sheet
  mentions no longer restyles everything inside it. Toggling a theme
  class on `<body>` that no selector uses re-ran the whole cascade --
  900 ms on a page of 12,000 elements and 3,000 rules -- and now leaves
  the styles alone (6 ms). Names that appear anywhere in a selector,
  including inside `:is()`, `:not()`, `:has()`, `:nth-child(... of S)`
  and `@scope`, or any `[class]`/`[id]` attribute selector or
  `:target`, still restyle as before.
* `getElementById` answers repeat lookups of a duplicated id at once.
  When two elements shared an id every call walked the whole document to
  find the first one, so 2,000 lookups on a 60,000-node page took 4
  seconds; the answer is now remembered until an element with that id is
  added, removed or renamed, and the same loop takes 4 ms. The tag and
  class indexes behind `getElementsByTagName`, `getElementsByClassName`
  and simple `querySelectorAll` calls no longer search a whole list to
  drop or place one element: a large list keeps a set of its members and
  is put back in document order the next time it is read.
* A style sheet pulled in with `@import` is parsed once, like a
  `<link>` sheet, instead of again on every layout. Every relayout
  re-parsed each imported sheet from its bytes, so a page that imports
  a 2,000-rule sheet and changes its DOM twenty times spent 4.6 seconds
  in the parser; it now takes 0.14 seconds, the same as linking the
  sheet. The cached sheet is keyed by its address, the layer it is
  imported into and the viewport, and is re-parsed when the fetched
  bytes change.
* `:nth-child()`, `:nth-last-child()`, `:nth-of-type()` and
  `:nth-last-of-type()` no longer slow down with the square of the list
  length. Each test counted the element's siblings from scratch, so a
  zebra-striped list of 40,000 rows took 14 seconds to style; the style
  pass now numbers all the children of a parent in one sweep the first
  time one of them is asked about, and the same page loads in about a
  second. `:nth-child(... of S)` still counts the matching siblings each
  time.
* A long descendant selector no longer hangs the browser on a deep page.
  Matching `.nomatch div div div span` retried every ancestor at every
  step, so the work grew with the depth of the tree raised to the number
  of compounds: 3 seconds for that selector over a 120-deep tree, over a
  minute with one more `div`. A step that has already searched every
  ancestor up to the root now tells the steps before it to stop, as
  other engines' selector checkers do, and the same selectors match in
  under a millisecond.
* Matching a selector against an element no longer looks up the
  element's namespace unless the selector names one, and a type selector
  compares the tag name against a lowercase copy made when the sheet is
  parsed instead of case-folding both names on every test. The style
  pass on a page of 16,000 elements and 3,000 rules takes a quarter less
  time, and a long descendant selector over a deep tree a third.
* Each `<style>` element is its own style sheet again. Adjacent inline
  sheets were joined into one text before parsing, so a sheet that ended
  inside an unclosed block, string or comment swallowed every sheet after
  it; such a sheet is now parsed on its own, where the end of the sheet
  closes whatever it left open. `<style type="text/foo">` and a
  `<link rel=stylesheet>` whose `type` is not CSS no longer apply, a
  `<link>` with the `disabled` attribute is not loaded, and
  `styleEl.disabled = true` or `sheet.disabled = true` switches a sheet
  off -- the getter was a stub that always said `false`.
* The `dir` attribute sets the CSS `direction`, as the HTML rendering
  rules map it. Only the text shaper read the attribute, so on a
  right-to-left page the words ran right to left but a table still put
  its first column on the left and a flex row still started at the left
  edge. `dir=rtl`, `dir=ltr`, `dir=auto` and `<bdi>` now reach the
  cascade through `:dir()`, and list markers sit on the right of a
  right-to-left list item instead of being painted off its left edge.
* Lists follow the HTML rendering rules. A nested `<ul>` draws a
  circle and the next level a square instead of a disc at every depth; a
  list inside a list has no block margins of its own; `<dir>` and `<menu>`
  are block lists indented 40px like `<ul>`; and `ul`/`li` `type="none"`
  hides the marker. The marker is chosen from the computed
  `list-style-type` rather than from whether the parent is an `<ol>`, so
  `ol { list-style-type: disc }` draws bullets. The 2px margin the
  default stylesheet put around every `<li>`, and the bold and extra top
  margin on every `<dt>`, are gone -- no browser has them.
* `width`, `height`, `hspace` and `vspace` attributes are read with the
  HTML rules for parsing dimension values. They went through `strtod`,
  so `width="+200"` and `width=".5"` produced a width where no browser
  gives one, and `width="20.25e2"` came out 2025px wide instead of
  20.25px. A table's or a cell's `width="0"` is now ignored, as the
  "ignoring zero" rule requires; `hspace`/`vspace` accept a percentage
  and reach `<embed>`, `<object>`, `<marquee>` and `<input type=image>`,
  not only `<img>`; and an image's `border` attribute is no longer capped
  at 100px and applies to `<object>` and image buttons too.
* A `min()`, `max()` or `clamp()` inside `calc()` resolves its
  percentages against the box it sits in. The nested function was
  reduced to a number up front, with any percentage taken of the window
  width, so `width: calc(min(100%, 800px))` in a 500px column came out
  800px wide. Additions and multiplications around the function, as in
  `calc(100% - min(2rem, 5%))`, are now folded into it instead.
* A declaration whose `var()` cannot be substituted -- the variable is
  undefined and there is no fallback, or what it holds does not parse
  for that property -- leaves the property `unset`, as the spec's
  "invalid at computed-value time" rule says, instead of vanishing and
  letting an earlier declaration win. `color: red` followed by
  `color: var(--undefined)` now inherits the parent's colour, as in
  every other browser, rather than staying red.
* A declaration that uses `var()` keeps its place among the other
  declarations of its rule. Such declarations are set aside until the
  element's custom properties are known, and were then ranked after
  every plain declaration of the rule, so
  `padding: var(--gap); padding-top: 3px` ended up with the variable's
  padding on top instead of 3px.
* An element's own `style` attribute outranks cascade layers, for
  `!important` declarations as well as normal ones. Layers were
  compared before the inline flag, so
  `@layer base { .x { color: red !important } }` beat
  `style="color: green !important"`.
* A translation that mixes a percentage with a length, such as
  `translateX(calc(-50% + 10px))`, moves by both. The percentage was
  dropped whenever a length was present, which left centred pop-ups and
  tooltips off by half their width. `em` and `rem` in `translate()`, its
  siblings and the `translate` property are measured against the
  element's own font size and the root's, not a fixed 16px, and a
  transition between a percentage and a length translation moves
  through both instead of treating the percentage as pixels.
* A transition between `transform: none` and a transform plays, and the
  element keeps its transform when it ends. Building the identity
  transform to animate from wrote zeros into the target value itself --
  the one the element's computed style holds -- so a hover that slid or
  scaled something from `none` left it where it was, for good.
* `text-shadow` is inherited, as CSS Text Decoration specifies, so a
  shadow set on a container reaches the text of the paragraphs, list
  items and inline-blocks inside it rather than only the container's
  own loose text. `orphans`, `widows` and `dominant-baseline` are
  inherited too, as their specs say.
* Every layer of a multi-image `background` in an external stylesheet
  is fetched relative to that stylesheet. Only the first `url()` was
  resolved against the sheet's address and the rest against the page,
  so the second image of `url(img/a.png), url(img/b.png)` in
  `/css/site.css` was requested from `/img/b.png`.
* `font-weight: bolder` and `lighter` are worked out from the parent's
  weight, using the table in CSS Fonts 4, when the style is computed.
  They were kept as keywords and measured later against a fixed 400
  with the old thresholds, so `bolder` inside bold text stayed at 700
  instead of 900, `lighter` inside bold fell to 100 instead of 400, and
  children inherited the keyword rather than the weight.
* `color: currentColor` takes the parent's colour, which is what the
  keyword means on the `color` property itself. It was left unresolved,
  so it was handed down as a word, getComputedStyle reported
  "currentcolor", and a border that takes its colour from the text was
  drawn black.
* `initial` on an inherited property means that property's initial
  value -- black text, a 16px font, normal weight and style,
  `line-height: normal` and so on -- instead of acting like `inherit`.
  The cascade stored nothing for the keyword, and an inherited property
  with nothing stored takes its parent's value, so `color: initial`
  inside red text stayed red and `all: initial` barely reset anything a
  reader could see.
* `rem` in the root element's own `font-size` is measured against the
  initial 16px, as the spec requires, instead of against the size it is
  in the middle of computing. `html { font-size: 1.25rem }` came out at
  25px rather than 20px, and a fluid
  `clamp(1rem, 0.9rem + 0.5vw, 1.25rem)` settled on the wrong size --
  which then scaled every other `rem` on the page with it.
* A percentage `line-height` is worked out once, on the element that
  sets it, and descendants inherit the resulting length. It was handed
  down as a percentage and each child resolved it again against its own
  font size, so a heading inside `font-size: 14px; line-height: 150%`
  got a 48px line instead of 21px -- the opposite of what the spec (and
  every browser) does, and the reason a percentage is not the same as a
  plain number there.
* A grid row with a fixed size keeps it. Rows declared as `20px` (or a
  resolvable percentage, or `minmax()` of two fixed sizes) grew to fit
  taller content like `auto` rows do, pushing every later row down;
  now the content overflows the row, and an item spanning a fixed row
  and an `auto` one grows only the `auto` row.
* Grid items given both a row and a column claim their cell before the
  automatically placed items flow in, as the grid placement algorithm
  orders it. An item pinned to row 1, column 1 that came later in the
  source used to land on top of whichever auto-placed item had already
  taken that cell.
* A `position: fixed` box inside a transformed element belongs to that
  element: it is placed against it and scrolls with it, as CSS
  Transforms says, instead of being pinned to the window. Slide-in menus
  and modals built inside a `transform`ed wrapper now open where the
  page puts them.
* `<center>` and `align="center"` (or `"right"`) line up the blocks
  inside them, not just images and tables: `<center><div
  style="width: 200px">` is centred, as is a table nested in a
  `<td align="center">`. A block with a margin of its own or an `auto`
  margin keeps the position those give it, and plain `text-align:
  center` still moves only inline content.
* A table's `width` includes its border and padding, as HTML's default
  stylesheet makes tables `box-sizing: border-box` and table layout now
  honours box-sizing. `<table style="width: 100%; border: 1px solid">`
  no longer sticks out of the page by its border, and a `width="600"`
  table is 600px wide overall.
* A percentage height inside a `box-sizing: border-box` parent is a
  share of that parent's content box. It was taken from the border-box
  height instead, so `height: 50%` inside a 100px-tall parent with 10px
  of padding came out 50px rather than 40px.
* An absolutely positioned box without `top` sits where it would have
  flowed, not at the bottom of its parent. Finding that static position
  only settled when the walk reached the next element after the box, by
  which point the whole parent had been counted, so a positioned first
  child of a 30px block landed 30px too low; and boxes inside earlier
  positioned boxes were counted as if they took up space in the flow.
* A child's bottom margin stays inside a parent it must not escape. It
  collapsed through any parent without bottom padding or border, so an
  `overflow: hidden` box, a float or an inline-block lost its last
  child's bottom margin from its own height, and a parent with a fixed
  `height` pushed the next block down by that margin as if it were its
  own. Margins now only collapse through a parent whose height is
  `auto` and whose `min-height` is zero, and never through one that
  starts a new formatting context or the root element -- so the
  document is as tall as the body's margins say, as in other browsers.
* An absolutely positioned box sized by its content is as wide as that
  content again: its own padding and border were being taken out of the
  measured width, so a box with `padding: 10px; border: 5px` around a
  100px child came out 70px wide inside. Its height now also honours
  `min-height` and `max-height` -- `top: 0; bottom: 0; max-height: 50px`
  was as tall as the containing block, and `height: 10px; min-height:
  60px` stayed 10px.
* Deeply nested flex rows lay out in a blink instead of seconds. Every
  row laid each item out once to measure it and again in place, and each
  of those layouts did the same for the row inside, so the work doubled
  with every level: 22 nested `display: flex` boxes took three and a
  half seconds. An item whose size did not change between the two passes
  is now moved into place rather than laid out again, and a row with a
  definite height hands its stretched items that height on the first
  pass, so the same page takes 0.2s. Moving a grid container now also
  moves its track positions, which absolutely positioned grid children
  are placed against.
* Shrink-to-fit boxes -- inline-blocks, floats, flex items sized by
  their content -- no longer come out wider than what they hold. A
  child with a pixel `width` and `box-sizing: border-box` was measured
  at its border-box width and then had its padding and border added a
  second time, and every float inside was counted as at least 60px
  wide, so an inline-block around a 16px floated icon was 60px wide.
* Floated columns with `box-sizing: border-box` sit side by side again.
  Deciding where a float fits counted its padding and border twice for
  a border-box width, so the Bootstrap 3 grid -- `*{box-sizing:
  border-box}` and two `float: left; width: 50%; padding: 0 15px`
  columns -- dropped its second column below the first.
* A flex item with a height of its own keeps it in a row. Stretching
  ignored whether the item's height was `auto`, so two 20px-tall items
  in a 100px-tall row both came out 100px tall -- and the second layout
  that stretch triggered sized them from their `width` again instead of
  the flexed width, so two `width: 200px` items squeezed into 300px
  overlapped. Only an item with an automatic height stretches now, the
  stretched height respects its `min-height` and `max-height`, and the
  flexed width survives the relayout.
* A multi-column block splits a list, not just a run of siblings. The
  column code distributed a container's own children and gave up when
  there were fewer than two, so `column-width` on a wrapper holding a
  single `<ol>` -- which is exactly how a Wikipedia reference list is
  built -- laid the whole list out in one column. A lone in-flow block
  child is now looked through and its children are distributed instead,
  and the balance point is measured from that child's content rather
  than the wrapper's outer height.
* A multi-column block establishes a block formatting context, as the
  spec says it does, so it sits beside a float instead of running
  underneath it.
* A block that establishes a formatting context is placed clear of every
  float it spans, not just the ones beside its top edge. It was narrowed
  against the float band at its first line and kept that width all the
  way down, so a taller float lower on the page -- a second stacked
  thumbnail, say -- ended up overlapping it. Where the finished box turns
  out to reach such a float it is laid out once more against the widest
  intrusion over its own height.
* `content: '[' / ''` renders just the bracket. The alternative text a
  `content` value may carry after a slash, for a screen reader to read in
  place of the glyphs, was being drawn as part of the text, so
  MediaWiki's section-edit links came out as `[/ edit ]/`.
* A list item styled `display: inline-block` or `display: block` no
  longer draws a bullet. Only a `list-item` display generates a marker;
  the painter went by the tag name, so an `<li>` that a page had made
  into something else -- a thumbnail in a MediaWiki gallery, say --
  carried a bullet no browser would show.
* A `<th>` no longer paints a grey background of its own, and a
  `<caption>` is no longer bold with padding under it. No browser's
  default stylesheet has either, so a table that sets its own colours --
  a Wikipedia infobox, for one -- showed its header cells in a shade the
  page never asked for.
* An inline-block whose width is a percentage no longer drags the
  intrinsic width of whatever contains it up to the width of the page. A
  percentage is indefinite while intrinsic sizes are being measured, but
  the atomic was laid out against the containing block anyway, so a table
  cell holding one reported a minimum as wide as the container and the
  table grew to match. It is measured against its own content instead.
* `align` and `valign` on a `<tr>`, `<thead>`, `<tbody>`, `<tfoot>`,
  `<col>` or `<colgroup>` now reach the cells, and a cell's
  `vertical-align` is inherited rather than pinned to `middle` by the
  default stylesheet, so a row can set the alignment for its cells the
  way the HTML rendering rules say it can. Cells still centre by default,
  because the row groups carry that default and the cells inherit it.
* `<figcaption>` is no longer italic and smaller than its figure, and a
  `<figure>`'s and a `<dl>`'s default margins match the HTML rendering
  rules. The italics in particular showed on every Wikipedia thumbnail
  caption.
* A style rule that names a pseudo-element is no longer thrown away
  because of what follows the pseudo-element's name. The selector parser
  treated any character after `::before` or `::after` as a syntax error,
  and the whole rule went with it -- so `li::after { ... }` survived only
  when the brace sat tight against the selector, and a selector list like
  `dd::after, li::after { ... }` never survived at all. That is how
  Wikipedia's horizontal lists are punctuated, so an infobox's platform
  list or a navbox's links ran together as one unbreakable word, which in
  turn forced the column measures wrong. Only something that really
  continues the compound selector is an error now; whitespace, a comma, a
  combinator and the block's brace merely end it.
* A table no longer squeezes its columns below the width their contents
  need. When a table asked for a width smaller than the sum of its
  columns' minimums -- an infobox at `width: 22em` whose labels do not
  fit, say -- every column was scaled down proportionally and the text
  ran out over the cell beside it. The table now grows to that sum, as
  CSS 2.1 requires.
* `min-width` and `max-width` on a table cell now take part in the
  column measures. The auto layout read only the cell's `width`, so a
  cell asking for `width: 50px; min-width: 150px` stayed at 50px and a
  `max-width` never clamped anything. A cell's contribution is now
  clamped the way every other box's is -- `max-width` first, then
  `min-width` -- in the min-content floor, the max-content measure and
  the specified width alike.
* A table cell inherits `text-align` from the table or the row again.
  The default stylesheet pinned `td, th` to `text-align: left`, which no
  browser's does, so `text-align: center` set on a `<table>` or a `<tr>`
  reached the caption and nothing else: a Wikipedia navbox title, a
  sidebar heading and every `align="center"`-era table layout came out
  left-aligned. `th` still centres on its own account.
* A table column with a specified width is no longer narrower than its
  cells need. The auto table layout took a cell's own `width` as that
  cell's minimum, so the width won outright: `width: 1%` on a heading
  cell -- the idiom Wikipedia's navboxes, and countless other tables,
  use to shrink a column to its label -- left the column one per cent of
  the table wide and its text ran across the cell beside it. A cell's
  floor is now its min-content width, measured without its own `width`,
  and a percentage `width` no longer counts as a definite minimum
  anywhere intrinsic sizes are measured, since it resolves against a
  basis that is not yet known.

1.0.9:
======
* Placeholder text in an `<input>` or `<textarea>` is no longer
  spell-checked. The layout already withheld spell ranges from a
  placeholder, but the painter's fallback underlined every run inside an
  editable host, so an empty search box showed a red squiggle under its
  own hint; the fallback now covers only `contenteditable` hosts, whose
  runs carry no explicit ranges. The README describes the engine thread
  in place of the removed renderer processes and request protocol, the
  typed `calc()` math, the Cache API and the site-partitioned cookie
  store, and its screenshot is recaptured from the 1.0.9 window.
* Text layout is ns-pango `3c6adba`, which merges upstream Pango 1.58.2.
  Of upstream's changes, four reach code the fork carries: an overline or
  strikethrough now spans the wider of a run's ink and logical extents,
  as the underline already did, so a decorated run whose glyphs are
  narrower than their advance no longer shows a shorter line above or
  through it than under it; a run with no font has its offsets zeroed
  rather than left uninitialised; the variant-to-feature mapping moves
  into shared helpers; and two zero-length memcpy and qsort calls that
  UBSan flags are guarded. The fork keeps its Ubuntu 24.04 dependency
  floors rather than upstream's new HarfBuzz 11 and fontconfig 2.17
  requirements, since nothing in the merged code needs them.
* Text layout is ns-pango `5a49882`, and a paragraph now comes out the
  same wherever it sits in its text. The fork's itemisation cache keyed a
  paragraph on its bytes and the layout's attribute list but not on the
  paragraph's offset, and attributes are ranges over the whole text: in a
  textarea, a `<pre>` or a pre-line paragraph whose second line repeated
  the first, a bold or a font on the first line was served to the second.
  Its shaping cache copied a font-feature range as an absolute offset into
  the paragraph, so a word shaped once under `font-feature-settings` on
  the word before it kept that shaping when it turned up elsewhere -- a
  kerning pair lost, or a ligature kept, on a word the feature never
  covered. Both keys carry what they lacked, and the fork's harness gains
  a `position` mode that fails on the old caches. The same pin fixes a
  heap overflow in the attribute-list deserialiser on a lone quote, two
  latent out-of-bounds reads inherited from upstream Pango, the fontmap
  serial not moving when a font file is added, and compares the item
  cache's attribute lists in linear rather than quadratic time. The fork
  now asks for stack protectors, stack-clash and control-flow protection
  and `_FORTIFY_SOURCE=3` by name, so a clang build carries them too, and
  runs its harness under AddressSanitizer and UBSan in its CI.
* Entering full screen now shows a notice. When the window goes full
  screen -- through the View menu, the shortcut or any other path -- a
  dark banner at the top of the page reads "<host> is now full screen.
  Press Esc to exit." for five seconds, and disappears again the moment
  the window leaves full screen. Without the banner a page could pass off
  its own drawing of an address bar and a sign-in form as the browser's
  chrome once the real toolbar was hidden (reported by Muhammad Wishal as
  fullscreen address-bar spoofing). `Element.requestFullscreen()` also
  now rejects with a `TypeError`, as the Fullscreen API specifies when
  `document.fullscreenEnabled` is false, instead of resolving as if the
  page had been granted full screen; page scripts cannot put the window
  into full screen in this edition and are no longer told otherwise.
* The address bar suggests as you type. A list drops down under the
  field with matching bookmarks, history entries ranked by visit count
  and recency, and a "Search for …" row that comes first when the text
  looks like a query and last when it looks like an address. Up and Down
  walk the list and fill the field with the highlighted address, Enter
  or a click opens it, and Escape or leaving the field closes the list.
  The history store gains a substring search over URLs and titles for
  this.
* The navigation toolbar is laid out like Mozilla 1.0 and Netscape
  Communicator. It sits on a bright silver-blue face with a white top
  highlight and a dark bottom groove and shows Back, Forward, Reload,
  Stop, Home, Print, Downloads and Bookmarks as icon-over-label buttons;
  they are flat until hovered, raise on hover and sink when pressed, and
  Stop stays in place greyed out while nothing is loading. The sunken
  location field with its page proxy icon takes the full width between
  the groove separators; the "Location:" label, the "Go" button and the
  toolbar grippy are gone, since Enter already goes and nothing was
  dragged. A Downloads button opens the downloads window from the
  toolbar. A new faceted printer icon and a downloads-tray icon join the
  icon set. The hamburger menu button carries a "Menu" label under its
  icon and keeps every window action with its shortcut, including Quit.
* The `about:start` splash is redrawn as a flat, sunny xkcd-style comic:
  black ink on white paper with only a few flat colours -- a yellow sun
  with wobbling rays, blue wave lines, a tan-planked ark and a red-banded
  lighthouse. The rain, lightning, umbrella, night sky and perspective
  road are gone; the animal pairs walk a plain ground line toward Noah in
  side view, and at the head of the queue two little browser windows on
  stick legs wait their turn beneath the speech bubble "Two of each. Yes,
  even browsers." A star pennant flutters on the ark's roof, Noah's wife
  waves from the deck, gulls circle the lighthouse, the keeper waves from
  the gallery and the dove carries its olive sprig. The 32 frames at 80 ms
  animate the sun's rays, drifting clouds, waves, the rocking ark, walking
  legs, hopping rabbits and kangaroos, the flapping dove and gulls and a
  whale surfacing to spout, on one 256-colour palette rendered at three
  times supersampling. `scripts/gen-splash.py` prefers its explicit font
  paths over `fc-match`, so the lettering stays comic on Windows. The
  README screenshot is updated with a full browser window capture,
  showing the navigation toolbar above the sunny about:start comic.
* The navigation toolbar is shorter. The icon-over-label buttons drop
  from 44 to 34 pixels with 20-pixel icons and a smaller label, the
  location field, Go button and throbber shrink to match, and the
  toolbar's own padding and margins tighten, so the whole bar is about a
  quarter less tall. The toolbar icons are redrawn in a style between the
  earlier flat gradient glyphs and the faceted 3D set: each keeps a
  single smooth gradient and a thin dark outline, and gains a soft drop
  shadow and a glossy top highlight, without the split facets and bevel
  edges. Stop is a red button with a white cross again, Reload a plain
  circular arrow, Home a blue-roofed house with door and windows, Print a
  printer with paper in and out, and Bookmarks a blue ribbon whose gold
  star fills in when the page is saved.
* `<textarea rows>` and `cols` are parsed as bounded non-negative
  integers (1 to 1000, defaulting to 2 and 20), so an attribute like
  `rows="2000000000"` no longer makes layout build billions of
  placeholder lines and exhaust memory.
* SVG rendering bounds the element tree it walks to 256 levels in every
  recursive pass -- rendering, `id` indexing for `url(#...)` references and
  locating the root of an SVG image -- so a document nested tens of
  thousands of `<g>` or `<div>` elements deep can no longer overflow the
  stack. Masks are budgeted at 256 MB of live surfaces across nesting.
  The SVG property resolver keeps its scratch buffers and the computed-style
  table in the per-render context instead of process-wide statics, so
  SVG images decoding on worker threads no longer race the main thread's
  inline SVG paint.
* XML internal entities expand to at most 1 MB per document; a nested
  entity chain (the "billion laughs" pattern) now makes the document
  not well-formed instead of exhausting memory.
* `<audio>` and `<video>` elements on `http(s)` documents can no longer
  name a `file:` URL, matching the rule the stream path already
  applied; a `file:` document may still play local media.
* Animation timelines: `currentTime` seeks on audio and animated images
  clamp non-finite and out-of-range values before converting to frame
  indices, per-frame delays are capped at ten minutes and total durations
  accumulate in 64 bits, so a crafted APNG or a huge `currentTime` cannot
  overflow the integer arithmetic.
* The JavaScript engine is quickjs-ng v0.17.0 (from v0.16.2), which adds
  `Iterator.zip`, `Array.fromAsync` and the upstream fixes of that
  release. The compatibility shims and the Windows link patch apply
  unchanged.
* The address bar shows the URL as serialised instead of percent-decoding
  it, and drops any `user:password@` part of the authority, so
  `https://bank.example%2Flogin@evil.example/` can no longer read as a
  page on the first host.
* Downloads triggered by `<a download>` require a recent user gesture, so
  a page cannot drop files into the download directory from a timer or
  from `click()`; names beginning with a dot or containing a path
  separator fall back to `download`.
* `navigator.clipboard.writeText()` and `execCommand("copy")` require a
  recent user gesture and reject with `NotAllowedError` otherwise, so a
  page cannot replace the clipboard contents while the user is elsewhere.
* The camera permission bar names the requesting origin, or "this page"
  when there is none, and never renders a raw `data:` or `blob:` URL in
  the prompt.
* After a crash or hang the supervisor restores the previous session once;
  if the restored session fails again before it has run for five minutes
  the next start is a clean one, so a page that wedges the browser cannot
  keep it in a restart loop.
* The in-process audio mixer accepts only `http(s):`, `data:` and `file:`
  URLs from the engine's side channel; anything else is refused instead
  of being treated as a local path. Side-channel headers (`X-Nav`,
  `X-Download`, `X-Audio`) that exceed their length limit are dropped
  whole rather than truncated to a different URL or command, and a queued
  audio command is only appended when it fits.
* Subresource redirects keep their initiator. A redirected image,
  stylesheet, script or fetch no longer loses its top-level document
  after the first hop; later hops keep the initiator's cookie and cache
  partition, the same-site cookie rule and the real `Sec-Fetch-Site`,
  `Sec-Fetch-Mode` and `Sec-Fetch-Dest` values instead of being treated as
  a fresh navigation, and the WebExtension request-block check runs on
  every hop.
* The `about:settings-save` and `about:settings-clear` endpoints require
  POST and read only the request body, a request without a top URL is
  trusted only when it is a navigation, and web pages can no longer
  navigate to `about:` pages other than `about:blank` and the start page.
  Home page and search engine URLs saved from `about:settings` must be
  http(s) (the home page may also be an `about:` page), free of control
  characters and at most 2048 bytes, and the cookie policy must be a known
  value.
* `document.cookie` refuses a name that collides with an `HttpOnly`
  cookie for the host in the site's network jar, so a server session
  cookie is never replaced by or sent alongside a script-set value.
* A `file:` page can still embed local images, scripts, stylesheets and
  frames, but `fetch()` and `XMLHttpRequest` receive only an opaque
  response for `file:` URLs, and directory listings are synthesized only
  for navigations.
* The Linux sandbox probes the Landlock ABI at startup, never grants
  symlink creation, grants truncate (ABI 3+) only where writes are
  allowed, and requests the link/rename right only on ABI 2+ so the
  ruleset also builds on older kernels. CSP scheme-only, `*` and host
  sources compare the parsed scheme, host and port of the resource URL
  and fail closed on a malformed URL; numeric render-IPC headers reject
  negative values.
* Option selectedness no longer rewrites the `selected` attribute.
  `option.selected`, `select.value`, `select.selectedIndex` and the
  dropdown and listbox picking paths change an internal selectedness flag
  (like checkbox checkedness) instead of the content attribute, so
  `defaultSelected` keeps reflecting the markup, `form.reset()` restores
  the default option, and `new Option(text, value, defaultSelected,
  selected)` maps its arguments correctly.
* Real clicks on checkboxes and radios follow the legacy-pre-activation
  rules: a mouse click toggles the control before the `click` event,
  reverts when the event is cancelled, and fires `input` and `change`
  afterwards, matching `element.click()`; clicking a `<label>` dispatches
  a synthetic `click` on its labeled control instead of toggling it
  silently, and a label whose `for` names a missing element no longer
  falls back to a descendant control.
* `<base href>` applies to images loaded through `img.src`, `new Image()`
  and srcset rescans and to `img.currentSrc`; `base.href` itself resolves
  against the document's fallback base URL.
* The `formdata` event fires when `new FormData(form)` is constructed and
  on every real form submission, so listeners can append fields, and the
  browser serializes that entry list. Real submissions validate like
  `checkValidity()`: `minlength` and `maxlength` only block after a user
  edit and `pattern` uses the JavaScript regex matcher.
* `range.value`, `stepUp()` and `stepDown()` produce the shortest
  round-trip decimal (`1234567`, `0.3`) instead of `1.23457e+06` or
  `0.10000000000000001`, and number and range sanitization enforce the
  HTML valid floating-point number grammar.
* `focus()` ignores elements that are not focusable areas; `select.add(el,
  index)` indexes across optgroups and inserts beside the reference;
  `img.complete` stays false while a `srcset`-only image is loading;
  `textarea.rows` reflects as a positive number with a fallback of 2; and
  the `form` attribute only selects a form owner while the control is
  connected.
* Canvases are origin-tainted. Drawing a cross-origin `<img>` (including
  one that redirected to another origin), a pattern made from one, an
  ImageBitmap created from one or an already tainted canvas clears the
  canvas's origin-clean flag, and `getImageData`, `toDataURL` and
  `toBlob` then throw a `SecurityError` DOMException instead of leaking
  the pixels; resizing the canvas restores the flag, and `data:`, `blob:`
  and same-origin images stay clean. `getImageData` checks its
  pixel-buffer size in 64-bit arithmetic before allocating.
* Cross-origin frames no longer expose their DOM. `iframe.contentDocument`
  and `getSVGDocument()` return `null` and `contentWindow` returns a
  restricted window proxy (`postMessage`, the `location` setter,
  `closed`, `length`, `window`/`self`/`frames`/`parent`/`top`/`opener`,
  `close`/`focus`/`blur`; everything else throws `SecurityError`) when the
  frame's origin differs from the embedding document or the frame is
  sandboxed without `allow-same-origin`. Same-origin, `about:blank` and
  `srcdoc` frames are unaffected.
* `postMessage` matches `targetOrigin` exactly: the value is parsed to
  scheme, host and port (a full URL collapses to its origin, the scheme
  is case-folded, default ports drop) and compared for equality with the
  target window's origin; `/` means the caller's own origin and `*`
  still matches everything.
* The CSS component-value parser bounds nesting at 512 levels and the
  `@container` condition parser at 32, so a style sheet or a
  `style.setProperty()` value made of hundreds of thousands of `(` no
  longer overflows the stack.
* An unterminated CSS string ends at the next newline as a bad-string
  token, as css-syntax-3 requires, so `content:"x;` only invalidates its
  own declaration instead of swallowing the rest of the block. `:not()`
  and `:has()` with an empty argument are selector parse errors and drop
  their rule; `p:not(){}` no longer matches every `p`. A pseudo-element
  ends its compound selector: `div::before span`, `::before.x` and
  `::before#id` are parse errors, while the user-action pseudo-classes
  (`::before:hover`, `::after:focus-visible`) still follow it.
* Custom properties are substituted at declaration time: with
  `.a{--x:var(--y);--y:red} .b{--y:blue}` a `.b` inside `.a` inherits the
  red `--x` computed on `.a`, and `getPropertyValue('--x')` returns `red`
  rather than `var(--y)`. Registered properties are type-checked on the
  substituted value. A `var()` cycle such as `--a:var(--b);--b:var(--a)`
  makes every member guaranteed-invalid so fallbacks apply instead of
  leaking the literal `var()` text once the depth cap was hit, and a
  declaration whose substitution leaves a top-level `!important` is
  dropped rather than kept with the keyword stripped.
* `color-mix()` scales the result's alpha by the percentage sum when
  both percentages are given and add up to less than 100%, and `in
  oklch` interpolates in polar form with the hue along the shorter arc.
* Shorthands reset the longhands they omit: `text-decoration` emits
  line, style and colour (so `text-decoration:underline` after
  `underline dotted red` renders solid in the current colour), `outline`
  and `column-rule` reset to medium/none/currentcolor, `flex-flow` to
  row/nowrap, `columns` to auto/auto and `border-block`/`border-inline`
  reset width, style and colour on both sides. A shorthand containing a
  token that does not parse is dropped whole -- `padding:10px auto` no
  longer sets the top and bottom. `outline-style: auto` is accepted.
* `-webkit-device-pixel-ratio`, `-webkit-min-device-pixel-ratio` and
  `-webkit-max-device-pixel-ratio` are aliases of `resolution` in dppx,
  and `@import` honours a `supports(...)` condition after its `layer()`
  part.
* `min()`, `max()` and `clamp()` keep `em`, `rem` and `lh` terms per
  argument and resolve them against the element at computed-value time
  instead of baking 16px and 19.2px at parse time, so
  `font-size:clamp(1.6rem,4vw,2.4rem)` under `html{font-size:62.5%}`
  clamps to 16-24px and `padding:max(1em,8px)` at 20px gives 20px.
* Grid containers no longer cap auto-placement at 24 rows: the occupancy
  map and row-size arrays grow with the content (up to 4096 implicit
  rows), so long grid lists lay out one item per row instead of piling
  everything past row 24 onto the last row.
* Floats are placed at their margin-box top; a float with `margin-top`
  previously landed a full margin lower than its siblings. Floats paint
  above the backgrounds of in-flow blocks that follow them, `z-index`
  applies to flex and grid items even when they are not positioned, and
  positioned boxes with a negative `z-index` paint immediately after the
  background of the stacking context that owns them.
* A block whose first in-flow child has a top margin grows its own top
  margin instead of keeping the collapsed margin inside its content box
  (CSS 2.2 section 8.3.1), so a background on the parent no longer shows a
  band above the first child and the collapse propagates through nested
  ancestors. The root element and flow-root, flex, grid, float and
  positioned boxes still keep their children's margins inside.
* Table column counting and width measurement account for cells occupied
  by a rowspan from an earlier row, a cell spanning several columns only
  widens them when they add up to less than its own width (distributing
  the excess proportionally), and `<col span width>` gives its width to
  each covered column.
* `text-align` no longer centres or right-aligns block-level tables,
  images, video or SVG; auto margins, the table `align` attribute and the
  legacy `<center>` / `align=` ancestry keep working. Wrapping flex
  containers honour `margin-left:auto` / `margin-right:auto` on their
  items, replaced elements respect `box-sizing:border-box`, and
  `position:relative` offsets accept `calc()`, math functions and every
  length unit.
* The `:checked` selector on options and the listbox highlight read the
  same selectedness the DOM uses, so an option chosen by script in a
  `<select multiple>` matches `:checked` and paints selected.
* Continuous integration builds with the optional Ogg Opus/Vorbis and
  Enchant libraries on every platform, so the in-process Ogg decode and
  spell-checking paths compile on Linux, Alpine, macOS and Windows; the
  Linux job also builds on Ubuntu 26.04, the Windows job uploads its
  binary, and Dependabot keeps the GitHub Actions versions current.
* The shell calls the page engine directly. The internal HTTP/JSON
  request protocol that carried every page load, event and frame over a
  socketpair between two halves of the same process is gone, together
  with the fork-and-exec spawners for a renderer executable that was
  never built and the Task Manager that listed this one process. The
  engine now runs on a dedicated thread with its own GLib main context,
  so page timers, fetch completions and settle loops no longer share the
  GTK main loop and the window stays responsive while a page loads;
  audio, navigation, camera and download signals come back as plain
  fields of a rendered frame instead of length-capped headers.
* The `about:start` splash animation and the logo are read from
  `share/northstar/splash.gif` and the installed `northstar.gif` instead
  of being compiled in as base64 arrays, which takes 660 KB out of the
  binary and a second copy off the heap on every start page; the
  unreferenced `data/splash.png` is gone and `scripts/gen-splash.py`
  writes `data/splash.gif` directly.
* Dead JavaScript bindings are gone: the C `Element.animate()` that
  resolved every animation as finished before the polyfill replaced it,
  the fake `getSelection()` and `Range` stubs whose twenty methods did
  nothing, and the polyfill copies of `URLSearchParams`,
  `AbortController`, `AbortSignal`, `XMLSerializer` and
  `structuredClone` that never ran because the native binding wins; the
  polyfill verifier no longer checks for those natively provided
  constructors. Three unreferenced engine functions are removed with
  them.
* `document.cookie` and network requests share one cookie store per
  site partition, owned by libcurl. Each partition has a cookie-sharing
  libcurl share and a holder handle that loads and writes the Netscape
  jar file; requests attach to that share instead of loading and
  rewriting the file per handle, script writes go in through
  `CURLOPT_COOKIELIST` and reads come out of `CURLINFO_COOKIELIST`. The
  separate `.js.txt` sidecar jar, the hand-written file parser and
  rewriter and the clobbering race between concurrent handles that
  motivated the sidecar are gone; an existing sidecar is folded into the
  jar on first use. Scripts still cannot read or overwrite `HttpOnly`
  cookies.
* One `calc()` evaluator. The CSS engine's math parser now carries a
  value kind (number, length, angle, time, resolution, flex) through
  every operation and function, so the five private evaluators that
  colour channels, `image-set()` resolutions, time properties, media
  queries and `sin()`/`cos()`/`tan()` arguments each kept are deleted,
  along with the text rewriters that turned angle and time units into
  bare numbers. `atan()`, `asin()`, `acos()` and `atan2()` now yield
  angles, so `rotate(atan(1))` rotates by 45deg instead of being
  dropped, `min()`, `max()` and `clamp()` compare angles, times and
  resolutions, and a length property no longer accepts `calc(10deg)`
  as zero.
* The Cache API is real. `caches.open()`, `has()`, `delete()`, `keys()`
  and `match()` and the `Cache` methods `match()`, `matchAll()`,
  `add()`, `addAll()`, `put()`, `delete()` and `keys()` store request
  and response pairs per site partition in IndexedDB and follow the
  Service Workers specification: URL fragments are ignored,
  `ignoreSearch`, `ignoreMethod`, `ignoreVary` and `cacheName` are
  honoured, `Vary` headers are matched against the stored request,
  a non-GET request, a non-http(s) URL, a 206 response, a `Vary: *`
  response or a used body rejects with a `TypeError`, and `addAll()`
  stores nothing when any fetch fails. The same storage is visible from
  a page and its service worker, so a worker that fills a cache at
  install time serves it offline. Until now every method resolved with
  nothing.
* Style recalculation reads each element's selector keys once. To find
  the rules that might match an element, the cascade looks it up in
  every stylesheet's index by id, by each of its classes, by tag name
  and by each attribute name -- and it re-derived those keys for every
  sheet it consulted, so an element was scanned for its `id` and `class`
  attributes, had its class list re-split into tokens with a copy per
  token, and had every attribute name measured and lower-cased once per
  stylesheet rather than once. The keys are now built a single time per
  element and handed to each sheet, the class tokens come from the
  parsed class set the element already caches for `ns_node_has_class`,
  and the tag and attribute names are folded to lower case once instead
  of at every lookup. Nothing about which rules match changed; the same
  page simply reaches the same answer with a fraction of the string
  work, which is most of what style recalculation was doing on a large
  document.
* Looking an element up by id no longer walks the document when there is
  no such element. The document keeps an id index, but a lookup that
  missed fell through to a full depth-first walk, because nothing
  recorded whether the index could be trusted to be complete. Every read
  of a global that a page has not defined goes through this path -- the
  window object consults its named properties whenever ordinary property
  lookup fails -- so a single `typeof someGlobal` cost a walk of every
  element in the document, and feature detection in a loop cost one per
  test. Writing an `id` attribute now updates the index from the one
  place every id write passes through, rather than from the scripting
  layer alone, so the index holds every id in the document and a miss
  can answer immediately -- the same trust the document's tag index
  already enjoys. A lookup scoped to a subtree, such as one inside a
  shadow root whose ids the document index deliberately does not hold,
  and a duplicated id both still take the walk. Three thousand reads of
  an undefined global on a document of eight thousand elements fall from
  420 ms to 0.5 ms.
* Declaring a transition no longer costs a style recalculation every
  frame. The animation layer keeps a record for each element that names
  a `transition` or an `animation`, and creating that record barred the
  element from reusing its computed style -- necessarily, because while
  an animation runs it writes interpolated values straight into that
  style, and reusing it next frame would hand the cascade a value it
  never computed. But the bar was raised as soon as the record existed,
  whether or not anything was animating, and a barred element drags its
  whole subtree with it. On a page whose stylesheet puts a `transition`
  on most of its elements -- which is to say a modern page -- that
  turned an incremental restyle back into a full one: on the Speedometer
  3.1 TodoMVC pages roughly two thirds of the document was recomputed on
  every frame with nothing on the page having changed. The bar is now
  raised and lowered by the code that actually writes the values, so it
  covers exactly the elements that are mid-animation, and an element is
  recomputed once more as its animation ends so it never keeps an
  interpolated value. A declared transition that is not running now
  costs nothing.

1.0.8:
======
* When several options of a single-choice `<select>` carry the
  `selected` attribute, the last one wins, as the HTML selectedness
  setting algorithm requires; the first used to win, so `<option
  selected>` appended after another selected option (by the parser,
  `appendChild` or `innerHTML`) did not become the value.
* `HTMLOptionsCollection` exposes `selectedIndex`, and every event the
  engine dispatches carries a `composed` flag (true for the UI event
  types, false otherwise) instead of leaving the property undefined on a
  `change` event.
* `transform` is validated function by function against css-transforms:
  each function checks its argument count and types, so `translate(1px,
  2px, 3px)`, `scale(6, 7, 8)`, `skewX(0, 0)` and `translateX(3%) none`
  are rejected, and the specified value serialises canonically --
  percentages in `scale()` become numbers, `rotate(0)` reads
  `rotate(0deg)`, `0` lengths read `0px`, and function names other than
  the `translate` family are lowercased. The `scale`, `rotate` and
  `translate` properties get the same treatment: `scale: 100% 100%` reads
  `1`, `translate: 100px 0px` reads `100px`, `rotate: 400grad x` and
  `rotate: 0.5 0 0 400grad` are accepted and read `x 400grad`, and a
  negative axis vector folds its sign into the angle. `transform-origin`
  and `perspective-origin` follow the position grammar (`top center`
  reads `center top`, `1px left` and `top bottom` are rejected,
  `perspective-origin` takes the edge-offset form `right 20px bottom
  30px`), `perspective: 1000` without a unit is rejected, and
  `transform-box` is a property. WPT css/css-transforms/parsing: 190 ->
  318 of 336.
* `border-image` is implemented. The five longhands (`border-image-source`,
  `-slice`, `-width`, `-outset`, `-repeat`) parse and validate against the
  css-backgrounds grammar, the shorthand splits `source || slice [ / width
  | / width? / outset ] || repeat` and resets what it leaves out, and
  `border` resets all five as the specification requires. Paint draws the
  nine-slice border from a `url()` image, which the loader now fetches like
  a background image, or from a gradient rendered to the border area, with
  `stretch`, `repeat`, `round` and `space` edges, `fill` for the middle and
  `outset` enlarging the area; `border-image: linear-gradient(...) 1`, the
  common gradient-border idiom, now shows a gradient frame.
* `-webkit-border-radius` and the `-webkit-border-*-radius` corners are
  aliases of the unprefixed properties, `border-radius` rejects a fifth
  value, a negative radius or a second slash, a corner rejects a third
  value, and the shorthand's specified value collapses each half as a
  quad (`1px 1px 1px 2% / 1px 2% 1px 2%` reads `1px 1px 1px 2% / 1px 2%`).
  `em` and `rem` corner radii written as `h / v` pairs resolve against the
  font size in the computed style.
* The CSSOM rebuilds `style.border`, `style.borderTop` and the other
  sides, `style.borderRadius` and `style.borderImage` from their
  longhands, so `border: 1px solid #fff` reads back
  `1px solid rgb(255, 255, 255)` and `border-top: 2px` after `border:
  1px` reads `2px 1px 1px` through `border-width`. `cssText` prefers the
  `border` shorthand when its seventeen longhands agree, then the
  `border-width`/`-style`/`-color` quads, then a side, then
  `border-image` and `border-radius`, following the CSSOM
  serialization order; `style.length` counts every longhand a `border`
  shorthand sets. The `border` shorthand rejects `auto`, a second width
  or a negative length, and a shorthand that carries `var()` is kept
  whole rather than expanded into guessed longhands. `getComputedStyle`
  resolves `border` and the side shorthands from their computed
  longhands instead of echoing the specified text.
* `background-position-x`/`-y` accept `x-start`, `x-end`, `y-start`,
  `y-end` and an edge with an offset (`right 10px`, `top -20%`).
* A colour in the specified style serialises the way CSS Color 4
  requires: a keyword keeps its spelling in lowercase (`ActiveText` reads
  back `activetext`), and a legacy `#hex`, `rgb()`, `hsl()` or `hwb()`
  value reads back as `rgb()`/`rgba()`. The deprecated CSS2 system
  colours (`Menu`, `ButtonShadow`, `ThreeDFace`, `WindowFrame` and the
  rest) map to their CSS Color 4 replacements instead of being invalid.
  A comma-separated layer list with one invalid layer is now rejected as a
  whole, as the grammar requires, rather than keeping the valid layers.
* The `background` shorthand is parsed layer by layer against the
  css-backgrounds grammar. Every comma-separated layer sets all eight
  longhands -- image, position, size after the slash, repeat, attachment,
  origin and clip, with the colour on the final layer -- and a longhand the
  layer leaves out resets to its initial value, so `background: red` no
  longer keeps an earlier `background-image`. The old parser read one
  layer, dropped the origin, clip and attachment keywords and did not know
  `background-attachment` at all; that property now exists, and
  `background-clip`, `background-origin` and `background-attachment` take
  a comma-separated list like the other layered longhands. Paint resolves
  origin and clip per layer, clips the colour by the last layer's clip,
  and positions a `background-attachment: fixed` layer against the
  viewport. `background-position` keeps the keywords it was written with
  (`left top`, `center center`) in the specified style, `background-repeat`
  rejects a third keyword, and `background-clip` accepts `text`,
  `border-area` and `border-area text`. `style.background` and
  `cssText` rebuild the shorthand from the longhands in canonical order,
  omitting initial values, and `style.length` counts the nine longhands
  a shorthand sets. WPT css/css-backgrounds parsing/background-*: 168 ->
  330 of 350 subtests.
* The CSSOM keeps the keyword a shorthand was written with: after
  `border-color: red yellow` or `border: thin dotted blue`,
  `style.borderTopColor` reads `red` and `style.borderTopWidth` reads
  `thin` rather than `rgb(255, 0, 0)` and `1px`, and a multi-layer value
  such as `background-image: url(a), url(b)` serialises every layer
  instead of the first. Setting or removing a property now follows the
  CSSOM algorithm for overlapping declarations: a shorthand replaces the
  longhands and shorthands it covers, and clearing one longhand of a
  stored shorthand expands only that shorthand, so `style.length` goes
  back to zero once every longhand of `border-color` is removed and
  `style.border` no longer leaves an earlier `border-width` behind. WPT
  css/css-backgrounds parsing/border-*-shorthand: 27 -> 89 of 96.
* `box-shadow` and `text-shadow` serialise their specified value in
  canonical order (colour, offsets, blur, spread, `inset`) with `0`
  written as `0px`, and reject the invalid forms the grammar excludes:
  a lone length, a fifth length, two colours, `inset` twice, a negative
  blur, a percentage, or a colour splitting the lengths. The parser now
  keeps a `calc()` or `em` length until the computed style resolves it
  against the element's font size, and a shadow without a colour takes
  `currentcolor` from the computed `color` instead of a fixed
  half-opaque black. `rgb(0, 255, 0)` and other colours with spaces
  inside the parentheses were split into separate tokens and lost. WPT
  css/css-backgrounds parsing/box-shadow-*: 25 -> 82 of 82.
* lexbor is v3.0.1, up from v3.0.0. The release fixes the URL parser's
  buffer growth: a component longer than its stack buffer was copied into
  fresh heap storage without the bytes already written, and the list of
  search parameters kept a stale tail pointer, so a long path, query or
  host could lose its prefix on the way through lxb_url_parse. IDNA
  ASCII conversion had the same missing copy, an unfinished :contains()
  selector leaked and could be freed twice, and the multi-byte decoders
  returned a short-buffer status rather than writing past the end of a
  full output buffer. quickjs-ng stays at v0.16.2; the documentation now
  says so.
* A table's max-content and min-content widths are measured column by
  column, as css-tables-3 §4.4 requires: each column takes the widest
  cell it holds (or the cell's specified width, floored at its
  min-content), the columns are summed once with the border spacing, and
  captions widen the result. They used to be the sum of every row, so a
  table nested in a cell, a floated infobox with multi-paragraph cells,
  and a table inside a flex or grid item reported several times their
  real width; cells and captions now measure like blocks (widest child)
  instead of summing their children. WPT css/css-tables: 330 -> 334 of
  787 on a 2026-09 checkout.
* position: fixed elements stay anchored to the viewport while the page
  scrolls. Layout placed them against the initial containing block and
  nothing translated them by the scroll offset, so a fixed header, cookie
  bar or modal overlay scrolled away with the document. The paint walk
  now offsets a fixed box by the viewport origin (and culls its subtree
  against the offset bounds), hit-testing applies the same offset so
  clicks land on the fixed element, and mouse events carry
  viewport-relative clientX/clientY with document coordinates in
  pageX/pageY.
* position: sticky boxes are hit-tested where they are painted: clicking
  a stuck header or navigation bar used to fall through to whatever
  lay beneath it. One shared ns_box_sticky_offset serves paint, hit
  testing and getBoundingClientRect; it resolves percentage and calc()
  insets against the scrollport, and measures a sticky box inside an
  overflow container against that container's padding box rather than
  the cairo clip, so a partly scrolled-out scroller no longer drags its
  sticky children to the viewport edge.
* Fixed a use-after-free in the MutationObserver delivery loop: the job
  queued raw observer pointers, so a callback that disconnected and
  dropped a later observer left the loop reading and calling through
  freed memory. The queue now holds a reference on each observer's
  wrapper for the duration of the drain, and slots waiting for a
  slotchange event are scrubbed when their node is destroyed by an
  earlier listener rather than dispatched to a dangling node.
* OfflineAudioContext.startRendering() bounds the graph walk to 4096
  node renders; a page that wired a node into its own inputs several
  times over could make the recursion exponential and hang the browser.
* docs/cve-2026-85046.md records why the actively exploited V8 JIT type
  confusion CVE-2026-85046 does not apply to Northstar: the engine is the
  quickjs-ng interpreter with no optimizing compiler, no per-array
  elements kind, no write barriers, and a generic Array.prototype.sort
  that stores back through the checked property path.
* Raw pointers into page-owned ArrayBuffers are no longer held across a
  call back into JavaScript. putImageData, AnalyserNode's byte getters and
  the AudioBufferSourceNode renderer read the page's properties first and
  fetch the backing bytes last, so a getter that transfers or resizes the
  buffer meanwhile cannot leave the engine writing through a freed
  pointer (the type-confusion class behind CVE-2026-85046). WebAssembly
  externref boxes release their JavaScript value through the runtime
  rather than the context they were created in.
* Flex layout resolves flexible lengths the way css-flexbox-1 §9.7
  describes: one implementation shared by row, wrapping-row and column
  containers distributes free space with the item freezing loop, so flex
  factors below one scale the free space, flex-shrink is weighted by the
  flex base size, and min/max violations are frozen and re-distributed.
  The automatic minimum size of a flex item is min(content size,
  specified size) rather than the specified size, so width: 200px in a
  100px container shrinks as browsers do; min-content and max-content
  minimums are honoured, and a percentage size on a replaced element or
  text control counts as zero for the specified size suggestion.
* Column flex containers wrap: flex-wrap: wrap and wrap-reverse break
  items into lines against the definite main size, align-content places
  the lines (start/end resolve in the inline axis; space-around and
  space-evenly fall back to start when the lines overflow), a wrapping
  container with a single line is still multi-line, rtl mirrors the
  cross axis, column-reverse packs from the main end, auto margins in
  the main axis absorb free space, and an indefinite-height column sizes
  itself from its items' content contributions so flex: 1 items no
  longer collapse.
* Negative free space overflows in the right direction: space-between,
  space-around and space-evenly fall back to start, flex-end and center
  overflow the start edge, and a scroll container packs overflowing
  content toward its start so it stays reachable; row wrap-reverse
  mirrors lines against the container's definite height.
* scrollWidth and scrollHeight include the scroll container's end
  padding and, in rtl, overflow to the left. offsetTop and offsetLeft
  round negative values to nearest and flush pending layout before
  locating the offset parent.
* The static position of an absolutely positioned flex child honours
  start, end, left and right on justify-content and align-self as
  writing-mode-relative keywords, self-start/self-end use the child's
  own direction, and last baseline aligns to the cross end.
* align-items, align-self, align-content, justify-content, justify-items
  and justify-self parse first baseline, last baseline and the safe and
  unsafe prefixes.
* WPT css/css-flexbox: 1465 -> 1997 of 3670 subtests on a 2026-09
  checkout of the horizontal-writing-mode suites.
* An absolutely positioned box whose containing block is a grid
  container takes that block from its grid-column and grid-row lines,
  as css-grid-1 §9 requires: a line inside the explicit grid resolves to
  the edge of the adjacent track, a line outside it, an unknown name or
  a span resolves to the padding edge after the start/end swap, and
  offsets, percentages and shrink-to-fit sizes resolve against that
  area. A shrink-to-fit abspos box no longer squeezes below its
  min-content width when the area is narrower than its content.
* align-content: stretch on a grid container distributes free block
  space only to rows whose max track size is auto; fixed-length rows
  kept their length.
* An absolutely positioned element with an inline-level display that
  follows inline content takes its static position from the line it
  would have occupied, after the preceding text, instead of the top of
  the block.
* document.fonts.ready waits for the web fonts the page needs: it
  flushes style so pending @font-face loads are requested, resolves
  once the loader is idle and marks the document for relayout;
  fonts.status reports loading meanwhile.
* Alignment properties keep their full specified keyword: safe and
  unsafe prefixes, legacy left/center/right, first baseline (computed
  as baseline) and last baseline parse and serialize, and the
  place-self, place-items and place-content shorthands split two-word
  values and serialize a repeated value once.
* offsetTop/offsetLeft flush pending layout before locating the offset
  parent, so a first read during parsing no longer returns viewport
  coordinates.
* Grid containers with direction: rtl lay their columns out from the
  right, and grid-placed absolutely positioned boxes mirror with them.
* repeat(auto-fit, ...) collapses the repeated tracks that no in-flow
  item occupies, so a card grid with fewer cards than columns stretches
  the remaining fr tracks as browsers do.
* A grid item's percentage height resolves against its grid area when
  the rows it spans have definite track sizes, and an absolutely
  positioned child of a grid container with auto offsets takes its
  static position from its grid area, aligned by justify-self and
  align-self.
* getComputedStyle on a grid container returns the used track sizes for
  grid-template-columns and grid-template-rows, with explicit line names
  in place, as CSSOM requires.
* Track lists resolve em against the element's own font size and calc()
  percentages against the track axis; an auto track grows to its
  max-content contribution before fr tracks share the remainder, fr rows
  fill a definite container height, over-constrained minmax() rows
  shrink toward their minimum, percentage rows in an indefinite-height
  grid re-resolve against the final height, and an auto column measures
  its content with real text metrics.
* The grid track-list parser rejects negative sizes, stray commas,
  consecutive or trailing-only line-name lists, reserved words as line
  names, a second auto-repeat and non-fixed tracks beside one, and
  supports line names inside repeat().
* Fixed a hang in the CSS parser on an at-rule inside a declaration
  list (div { @foo {} color: green }), in stylesheets and inline styles
  alike; the at-rule is skipped as CSS Syntax requires.
* font-family values are canonicalized: quoted strings and ident
  sequences are told apart, ident-like strings are unquoted, generic
  families are lowercased and reserved single idents are rejected. The
  font shorthand is validated against its full grammar before any
  longhand is written, resets the longhands it does not mention, accepts
  math functions for the size and line-height, expands the system font
  keywords, and rolls back entirely on an invalid family;
  getComputedStyle().font is composed from the longhands.
* "prop in element.style" is true only for supported properties, and a
  longhand written after a later shorthand that covers it lands after
  the shorthand so it wins.
* Grid items honour justify-self and align-self self-start, self-end,
  start, end, left and right with the grid's and the item's direction,
  minmax(auto, X) tracks take their intrinsic minimum, min-width and
  max-width keywords (min-content, max-content, fit-content) resolve on
  grid items, an explicit-width item under justify-self normal is
  start-aligned in its area (right edge in rtl) and "safe" alignment
  falls back to start when the item overflows, and aspect-ratio sizes
  replaced elements.
* Gradients are parsed against the CSS Images 4 grammar: to-side and
  corner directions (the corner angle follows the box), radial shapes,
  size keywords and explicit radii, conic from-angles, positions in all
  forms, colour interpolation methods ("in oklch longer hue"), two-
  position colour stops and interpolation hints. Invalid preludes,
  empty arguments and stray hints are rejected. Specified and computed
  values serialize canonically, ellipse gradients paint as ellipses and
  closest/farthest side and corner sizes are honoured.
* background-position and object-position validate the position
  grammar (3-value syntax only for background-position) and serialize
  horizontal-first with a single keyword expanded to its pair.
* scrollWidth and scrollHeight extend the in-flow content's margin
  boxes by the container's end padding instead of every descendant's
  border box, so negative margins and margins that collapse through
  the container report no phantom overflow; in rtl and in the flipped
  cross axis of wrap-reverse the region is measured from the far edge.
  Block children in rtl containers that over-constrain the line keep
  their right margin and overflow to the left.
* getComputedStyle exposes -webkit- prefixed aliases through their
  camelCase names (webkitAppearance).
* The content property is validated against its grammar (strings,
  quotes, images, attr(), counter()/counters() with symbols(), an
  alt-text list after a slash) and serializes canonically: double-quoted
  strings, decimal counter styles omitted, the symbolic system dropped.
* Container queries evaluate the full condition grammar: not/and/or
  with nesting, size features in plain, boolean and range form
  (double-sided ranges and math functions included), aspect-ratio and
  orientation, unknown features that make the enclosing condition
  false, comma-separated condition lists, name-only rules and vertical
  writing-mode containers. Invalid preludes are dropped from the engine
  and the CSSOM, conditionText serializes canonically, container-name
  and the container shorthand validate their values, and
  CSSContainerRule exposes containerName, containerQuery and
  conditions. In headless mode a style flush after a mutation performs
  the container-aware relayout so getComputedStyle sees query results.
* image-set() and -webkit-image-set() are validated option by option
  (images, x/dppx/dpi/dpcm resolutions or math functions resolving to a
  resolution, type()) and serialize canonically in specified and
  computed style; unicode-range descriptors are validated per CSS
  Syntax 3 and serialize as uppercase U+XXXX or U+XXXX-YYYY.
* JavaScript can be turned off: an "Enable JavaScript" toggle in
  Settings parses pages with scripting disabled (so noscript content
  renders) and skips script execution entirely — the user decides what
  runs, in the old Mozilla tradition.
* about:mozilla shows the maroon page every browser of this lineage
  owes its readers, and about:config opens the settings page.
* The status bar reads "Done" for a moment when a page finishes
  loading, as it always did.
* Fixed a use-after-free of the session URL: timer, event-dispatch and
  requestAnimationFrame callbacks saved the current URL pointer and restored
  it unconditionally after the callback, so a handler that navigated (a
  fragment click, location.hash =, history.pushState) left the engine
  reading and double-freeing a freed URL. The URL is now restored only when
  it was actually swapped for an iframe realm.
* Added view-source: — Ctrl+U / "Page Source" in the menu shows the current
  page's HTML with classic syntax highlighting. Only chrome-initiated
  navigations can use the scheme; web content is refused.
* input.showPicker() and select.showPicker() are implemented per spec:
  InvalidStateError on disabled or readonly controls, NotAllowedError
  without a user gesture, and a successful call consumes the activation.
  WPT show-picker suites pass 129/129.
* stepUp()/stepDown() follow the spec: they throw InvalidStateError on
  non-numeric input types and step="any", honor the per-type default
  step and scale, round to the step grid, clamp to min/max, and
  serialize date, month, week, time and datetime-local values back to
  their canonical strings. WPT input-stepup 53/53 and time 32/32.
* The color input sanitizes through the CSS color parser: keywords,
  rgb() and #rgb shorthand normalize to lowercase six-digit hex, and
  surrounding whitespace is stripped.
* Label association follows the spec: label.control resolves the for
  attribute against the label's own tree to the first element with that
  id (null when it is not labelable or the attribute is empty),
  label.form returns the associated control's form owner, and .labels
  is a live NodeList.
* The WPT harness's testdriver bridge grants real user activation for
  simulated gestures, and the __nsWpt* hooks are inert outside the
  harness.
* Form constraint validation follows the HTML spec much more closely:
  ValidityState flags are computed for disabled and readonly controls
  (bars from validation affect willValidate, not the flags), valueMissing
  is suppressed on disabled/readonly controls, the pattern attribute
  compiles as a JavaScript regular expression with the v flag (invalid
  patterns are ignored) and applies to each address of a multiple email
  input, tooLong/tooShort fire only after a user edit as the spec's dirty
  value flag requires, and willValidate is false inside a datalist.
  WPT form-validation: patternMismatch, tooLong, tooShort, typeMismatch
  and badInput suites now fully pass.
* Live HTMLCollection/NodeList property semantics follow WebIDL: silent
  sloppy-mode failures and strict TypeErrors for read-only indexed and
  named properties, spec-compliant descriptors, expando support, and
  Object.keys listing only indices; moveBefore() is ParentNode-only;
  replaceChildren() queues a single mutation record; Node.isConnected is
  true for any node whose root is a document.
* document.getElementById respects shadow boundaries and duplicate-id
  document order after moveBefore/append reorderings.
* Popup blocking, in the Firefox 1 tradition: window.open only navigates
  when called within five seconds of a real user gesture (click, key
  press, touch), consumes that activation, and logs blocked attempts to
  the console. navigator.userActivation now reports the live activation
  state instead of constants.
* Ctrl+Enter in the address bar completes a bare name to www.name.com,
  Shift+Enter to .net and Ctrl+Shift+Enter to .org, as in classic
  Firefox.
* Ctrl+Shift+R and Ctrl+F5 reload the page bypassing the HTTP cache.
* Ctrl+D bookmarks the current page, with a matching "Bookmark This Page"
  menu entry, in the classic browser tradition.
* about:book — every browser of the lineage carries its Book.
* Updated the ns-pango subproject pin to the latest upstream commit.
* Optimized event dispatch throughput by lazily evaluating composedPath()
  arrays on demand rather than eagerly creating them on every event dispatch.
* Added node-level listener filtering with NS_NODE_HAS_LISTENERS flag, skipping
  listener array iterations on intermediate DOM tree nodes without listeners.
* Bound standard Event prototype methods (preventDefault, stopPropagation,
  stopImmediatePropagation, cancelBubble, composedPath) directly on Event.prototype.
* Added instant early-return checks to MutationObserver record dispatch when no
  observers are active, bypassing unnecessary node invalidations.
* Accelerated document.createElement with lowercase ASCII fast paths and
  ASCII-first element name validation.
* Optimized inline style property conversions (camel_to_kebab) and empty initial
  inline style updates avoiding intermediate GString allocations.
* Optimized dataset property lookups by matching target attribute names directly
  without repeated per-attribute string allocations.
* Bypassed redundant storage allocations and event dispatches when setting
  identical localStorage/sessionStorage values.
* Added HTMLDialogElement.showModal() standards compliance validating open and
  connected document state.
* Aligned structuredClone with specification requirements (argument validation
  and transfer options support).
* Optimized CSS.escape for simple identifiers and bounded selector and form
  ancestor traversals.
* Optimized DOM textContent and innerText get/set paths with zero-allocation
  string returns for empty and single text child nodes.
* Optimized DOMTokenList (classList) add, remove, and toggle with single-token
  fast paths avoiding token array and parser allocations.
* Accelerated DOM hierarchy validation in pre-insert checks and ancestor-or-self
  queries with O(1) leaf child checks and bounded traversals.
* Optimized DOM attribute operations (getAttribute, setAttribute, hasAttribute,
  toggleAttribute, removeAttribute) with zero-allocation lowercase ASCII fast paths
  and single-pass string conversion.
* Fast-path selector matching in Element.matches() and Element.closest() for simple
  class (.cls), tag (tag), and id (#id) selectors, avoiding full CSS selector AST
  allocations on delegated event lookups.
* Subtree getElementById queries leverage document root ID indexes and bloom filters
  prior to falling back to full recursive DOM tree traversal.
* CSS line-height unit calculations now cover all root font relative units
  (rem, rlh, rex, rch, rcap, ric), element font relative units (lh, ex, ch,
  cap, ic), viewport percentage units (vh, vw, vmin, vmax, vi, vb, svh, svw,
  lvh, lvw, dvh, dvw), and container query units (cqw, cqh, cqi, cqb, cqmin,
  cqmax) across text layout and painting.
* DOMTokenList (classList) optimizes add and remove operations to skip
  redundant attribute re-serializations and DOM mutation dispatches when the
  underlying token set is unchanged.
* AbortSignal spec compliance: AbortSignal.abort() and AbortSignal.timeout()
  generate standard DOMException instances (AbortError, TimeoutError),
  AbortSignal.any() accepts any iterable signal collection, and prototype
  chains correctly inherit from EventTarget.
* Norwegian regional Accept-Language configuration adds complete fallbacks
  across Bokmål (nb), Nynorsk (nn), and generic Norwegian (no) locales.
* Parser and layout loop safety improvements: bounded counter formatting
  buffers, guaranteed forward pointer progress in pseudo content resolution,
  unified depth-bounded ns_node_root tree traversals, and cycle-protected
  document order comparisons.
* Selection highlights the text it is actually on. The highlight was
  painted as one flat pass over the finished page, in document
  coordinates, after everything else had been drawn — so inside a
  scrolling box it landed wherever the text would have been unscrolled,
  spilled past the box it belonged to, and ignored the transforms and
  clips the glyphs themselves were drawn under. It is now drawn where the
  text is drawn, from a range table computed once per frame and looked up
  per box, so it inherits that box's scroll offset, transform and clip by
  construction. It also goes down before the glyphs rather than over them,
  which is what lets `::selection` carry an opaque background without
  washing the letters out. Selection hit-testing gained the two things the
  click path already had: it adds a scrolling ancestor's scroll offset as
  it descends, so a click inside a scrolled `<div>` selects the line under
  the pointer instead of the line that would be there at scroll zero, and
  it stops at a box that clips its children, so a point below an
  `overflow: hidden` container no longer reaches the content clipped out
  of it.
* `user-select` and `::selection` are read from where the style is. Both
  were looked up on the inline box's own `style`, which inline boxes do
  not carry — the style lives on an ancestor — so the pointer was always
  null and both properties were silently ignored: `user-select: none` text
  copied anyway, and a page's `::selection` colours never appeared. Both
  now walk to the nearest ancestor that has a style, the same way the text
  painter finds its font.
* Copied text reads like the page. Every inline box ended with a newline,
  so a paragraph broken into runs by a `<b>` or an inline-block came out
  one word per line; a `<br>` — which layout carries as U+2028 — came out
  as nothing at all. A line break is now emitted where a block boundary
  is, U+2028 and U+2029 become newlines, and the zero-width characters
  `<wbr>` leaves behind are dropped. Text under `user-select: none` is no
  longer collected at the ends of a range, only in the middle of one.
* Double-click selects a word, triple-click selects the block. Both
  gestures previously did what a single click did. Word edges come from
  the shaper's own break attributes rather than an ASCII rule, so they
  hold for scripts that do not put spaces between words. Shift-click
  extends the existing selection from its anchor instead of dropping it,
  and a drag that selected text no longer activates the link it ended on —
  releasing the mouse after selecting a sentence containing a link used to
  navigate away.
* A page can see and set the selection it is showing. `getSelection()`
  reported an empty, collapsed selection no matter what was selected on
  screen, because nothing ever told the JavaScript engine what the
  selection was; `document.execCommand` answered false to everything and
  `navigator.clipboard.write` was absent. The page selection now flows
  into the engine on every change, so `toString()`, `type`,
  `isCollapsed`, `rangeCount` and the range's `getBoundingClientRect()`
  describe the real one; `execCommand` performs `copy`, `cut`,
  `selectAll` and `unselect`, with `queryCommandSupported` and
  `queryCommandEnabled` agreeing about them; and `clipboard.writeText`
  and `clipboard.write` reach the system clipboard through a side channel
  on the render response, the same route downloads and audio commands
  already take. Reading the clipboard stays refused — there is no
  permission prompt behind which to put it.
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
* Viewport units inside a frame measure that frame. `vw`, `vh`, `vmin`,
  `vmax` and their small/large/dynamic spellings resolved against the
  top-level window wherever they appeared, so a 200x100 iframe laid its
  `100vw` box out at the width of the whole browser. The cascade now
  swaps in the frame's own viewport while it walks a nested document,
  the way the media-query evaluation already did, and only when the
  frame's size is actually known — from its `width`/`height` attributes,
  an inline size, or the last layout — so a frame whose size has not been
  measured yet keeps the behaviour it had rather than guessing at
  300x150. `data/fixtures` aside, a 200x100 `<iframe>` whose content asks
  for `100vw` by `50vh` now lays that box out at 200x50.
* A registered custom property computes its value instead of carrying the
  text it was written with. `<length>` arrives in pixels whatever unit it
  was authored in and whatever the element's font size is, a
  `<length-percentage>` that mixes the two serializes as the `calc()` the
  CSSOM specifies, `<angle>` lands in degrees, `<time>` in seconds,
  `<resolution>` in `dppx`, `<integer>` rounded, `<color>` as `rgb()` with
  `currentcolor` resolved against the element's own colour, `<string>`
  requoted, and a `<transform-function>` with its arguments computed the
  same way. The computation runs once the element's font metrics are
  known, so `--x: 14em` on a ten-pixel element is `140px` and an inherited
  value keeps the number its parent computed.
* A property registered through `CSS.registerProperty` now restyles the
  page. Incremental restyle skips a pass when no stylesheet has changed,
  and a registration changes no stylesheet, so a property registered from
  script had no effect until something else happened to dirty the tree.
  The registration is part of the signature that decides whether the pass
  can be skipped.
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

* Transitions and animations run on every property and show in style.
  The animation engine keeps one run per animation-name entry, so an
  element plays several @keyframes animations at once; iteration counts
  are fractional; a missing or empty @keyframes rule behaves as the spec
  says and a rule added after load is picked up on the next cascade; a
  @keyframes rule that omits its 0% or 100% frame interpolates from the
  element's underlying value, with transform: none as the identity; and
  the timing function applies per keyframe interval. Transitions start
  from a property that had no cascaded value (its initial value) and
  from the previous computed style when the transition and the change
  land in the same flush, interpolate keyword-stored numbers and lengths
  such as font-weight and vertical-align, clip rectangles and
  space-separated pairs such as two-value border radii, are cancelled
  when the property leaves transition-property or the element (or an
  ancestor) becomes display: none, shorten by the eased progress when
  reversed, and a child whose value is inherited from a transitioning
  ancestor follows the ancestor's animated value instead of starting its
  own transition.
* The animation and transition longhands are real properties:
  animation-name, animation-timing-function, animation-iteration-count,
  animation-direction, animation-fill-mode, animation-play-state,
  animation-composition, transition-property, transition-timing-function
  and transition-behavior (allow-discrete lets any property transition
  discretely), plus animation-timeline, animation-range,
  animation-range-start and animation-range-end as parse-only
  scroll-driven-animation properties. The shorthands expand into the
  longhands in the cascade and in the inline style object, so
  e.style.animation sets and clears the longhands and reads back the
  shortest serialisation; the shorthand grammar is strict (a bare number
  is an iteration count, a time never lacks its unit, a negative
  duration, a second timing function or a second name rejects the
  declaration, a transition without a property means all);
  animation-duration accepts auto; keyframe names given as strings are
  unescaped and serialised as identifiers; and the CSSOM rejects
  @keyframes rules named none, default, a CSS-wide keyword or a number.
  getComputedStyle serialises every longhand and both shorthands from the
  effective lists.
* A Web Animations surface backs the engine: Element.animate() drives the
  same keyframe runs from script (array and property-indexed keyframes,
  offsets, camelCase names, duration/delay/iterations/direction/fill/
  easing options) and its Animation reaches finish and cancel through
  the promises and events; element.getAnimations() and
  document.getAnimations() return CSSTransition, CSSAnimation and
  script Animation objects in spec order with currentTime, startTime,
  playState, play, pause, finish, cancel and reverse-safe seeking;
  AnimationEffect.getTiming and getComputedTiming follow the phase rules
  for fill, direction, iterations and zero durations;
  KeyframeEffect.getKeyframes returns the computed keyframes of a CSS
  animation with per-keyframe easing and composite; play() and pause()
  take precedence over an unchanged animation-play-state; and
  AnimationEvent and TransitionEvent have real constructors. A run
  starts on the first animation frame and the timeline is frozen
  between frames, so what a script sets is what it reads back, and the
  requestAnimationFrame timer no longer fires alongside the host frame
  loop.
* Animation and transition events follow the Web Animations phase model:
  animationstart, animationend, animationiteration, transitionstart and
  transitionend are emitted when the effect crosses between the before,
  active and after phases in either direction, including after finish()
  and seeks, with the elapsed times the spec assigns, and cancel events
  carry the active time.
* attr() substitutes at cascade time on every property, per css-values-5:
  attr(name) and attr(name string) quote the attribute, raw-string keeps
  it as is, a unit name (px, %, em, deg, s ...) appends that unit to a
  number, type(*) takes the attribute as a token stream and
  type(<syntax>) checks it against a registered-property syntax; a missing
  attribute or a value that fails the type falls back to the second
  argument or makes the declaration invalid at computed-value time; and
  attr() with a bad grammar is rejected at parse time. Elements that use
  attr() are excluded from style sharing and from the incremental restyle
  cache, so an attribute change re-substitutes.
* stretch, -webkit-fill-available and -moz-available are real sizing
  keywords: width fills the containing block's content box minus margins
  for block boxes, inline-blocks, floats, replaced elements, flex items
  (as the flex base size) and grid items, and absolutely positioned boxes
  fill from their static position or their insets to the padding edge;
  height, min-height and max-height stretch against a definite containing
  block and stay indefinite against an auto-height parent (also in quirks
  mode), and box-sizing does not change the stretched border box.
* Grid items with horizontal margins were laid out against the grid area
  minus their margins and then had the margins subtracted again, so every
  margined grid item was too narrow by its margin sum.
* A canvas element's width and height attributes map to aspect-ratio, as
  the HTML rendering section says, instead of to the width and height
  properties, so a stylesheet can size a canvas in one axis and get the
  other from its bitmap ratio. aspect-ratio keeps its numerator and
  denominator (auto 16 / 9 parses, serialises and computes as written,
  16 9 and 16px / 9px are rejected); a plain ratio overrides a replaced
  element's natural ratio while auto <ratio> only fills in for one.
  Replaced elements resolve min-width and max-width: fit-content,
  min-content and max-content from their ratio, and clamping one axis no
  longer rescales an axis the author specified.
* An absolutely positioned box with both insets set honours justify-self
  and align-self: start, end and center size it to its content and place
  it inside the inset-reduced containing block, normal stretches a
  non-replaced box and fits a replaced one, and stretch fills the area.
* attr() substitutions that land inside url(), src(), image() or
  image-set(), or that carry a url() of their own, make a non-custom
  declaration invalid, as css-values-5 requires for attribute-derived
  URLs.
* Fixed a read past the end of a linked stylesheet's bytes while
  scanning it for viewport media queries.
* Serialisation fixes for the specified style: counter-reset,
  counter-increment and counter-set escape their counter names and keep
  calc() integers as authored; list-style keeps the position when the
  type is a counter style named inside or outside, and its longhands read
  from a list-style shorthand serialise the image the way the
  list-style-image property does; gradient colour stops serialise hex and
  legacy rgb()/hsl() colours as rgb() and keep named colours and modern
  colour functions as written; overflow expands in the inline style, a
  visible/scroll pair computes to auto, and overflow-clip-margin,
  counter-set and list-style are reflected by getComputedStyle.
* Transitions and animations: a transition created in a later frame gets
  a later start time and reads as pending until its first frame; steps()
  rounds its floor at the interval boundary; one animationiteration event
  fires per seek across several iterations; animation, transition,
  container, direction and unicode-bidi properties are never animated.
* Nested style rules are exposed through the CSSOM: CSSStyleRule inherits
  from CSSGroupingRule with cssRules, insertRule and deleteRule,
  selectorText on a nested rule is relative to its parent (a selector
  without & or one starting with a combinator serialises with a leading
  "& ") and is settable, cssText prints the nested rules, and "&" followed
  directly by a type selector is rejected as the spec requires.
* A declaration whose value is a single {}-block (color: {var(--x)}) is
  a declaration, not a nested rule, in the CSSOM parser, and a custom
  property keeps a {}-block anywhere in its value.
* WPT, third pass of 1.0.8 (same 37 css/ areas, 6 s timeout): 42482 ->
  46653 of 68725 subtests. css/css-transitions 149 -> 2282 of 2504,
  css/css-animations 191 -> 803 of 976, css/css-sizing 745 -> 1320 of
  2444, css/css-align 3025 -> 3247, css/css-values 3599 -> 3739,
  css/css-lists 136 -> 256 of 274, css/css-overflow 300 -> 365,
  css/css-nesting 20 -> 84 of 117, css/css-easing 32 -> 84 of 156,
  css/css-position 267 -> 297; the full table is in docs/compliance.md.

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
