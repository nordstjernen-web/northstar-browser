Changelog:
=========
Significant changes in each release:

1.0.5:
======
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
