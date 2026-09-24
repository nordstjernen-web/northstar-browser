# Contributing to Northstar

Northstar is a small web browser written from scratch in C on GTK 4 and
libcurl. This file is the short version of how to work on it; the full
operating guide, including the project's scope, is
[CLAUDE.md](CLAUDE.md), and it applies to people as much as to coding
agents.

## Build and run

Install the dependencies listed in [docs/building.md](docs/building.md)
for your system, then:

```sh
meson setup builddir
meson compile -C builddir
./builddir/src/gtk/northstar
```

`./scripts/dev.sh build` does the same setup-if-needed and compile in
one step. `ccache` makes rebuilds much faster and is picked up
automatically. CI builds with `--werror`, so check that your change adds
no warnings.

## Check your change

There is no unit-test suite. A change is checked by running the browser:

- `./scripts/dev.sh smoke` renders the fixtures in `data/fixtures/`
  headless and diffs each against its baseline in `data/baseline/`. Run
  it before every commit. Refresh a baseline with
  `./scripts/dev.sh baseline <target>` only when the change in output is
  the one you intended.
- `./scripts/render-tests.sh` renders the pages in `data/render-tests/`
  to PNGs for you to look at.
- `./scripts/wpt-fast.sh` runs Web Platform Tests headless and prints
  per-standard scores. It needs a checkout of
  [wpt-fast](https://github.com/nordstjernen-web/wpt-fast) (by default in
  `~/wpt-fast`), and you can pass paths to run only the part you are
  working on.
- The headless driver reads every stage of the pipeline:
  `--headless --dump=text|dom|layout|png:FILE`, with `--eval=` and
  `--act=` to run script and input first.
- Launch the GUI and use the part you changed.

Say in the commit message what you measured or checked.

## House rules

- **No test suites.** Don't add unit, integration or fuzz tests, a
  `tests/` directory or `meson test` targets.
- **No code comments.** Each source file has one short header comment
  naming it; nothing else. Rename or extract instead of explaining.
  Vendored code under `subprojects/` and `src/wamr/` is exempt.
- **No site-specific hacks.** Fix the engine capability a page exercises,
  never the host.
- **Mind the scope.** This minimalist GPL edition is single-process and
  deliberately leaves out tabs, WebGL, WebGPU, AI web APIs and more;
  CLAUDE.md lists what not to reintroduce.
- **Update [Changelog.md](Changelog.md)** for anything a user would
  notice.

Security issues go to the address in [SECURITY.md](SECURITY.md), not to
the public tracker.

## Agent skills

Workflows for building, diagnosing rendering regressions, fixing
web-platform compatibility, auditing security boundaries and porting
changes between editions are in [.agents/skills/](.agents/skills/). They
are written for coding agents but read fine as checklists.

## Licence

Northstar is licensed under the GNU General Public License, version 3
or (at your option) any later version (GPL-3.0-or-later; see
[LICENSE](LICENSE)).
