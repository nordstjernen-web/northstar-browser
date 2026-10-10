# Northstar — agent operating guide

The operating guide for this repository is **[CLAUDE.md](CLAUDE.md)**,
and it applies to every coding agent, not only to Claude Code. Read it
before changing anything: it carries the project scope (what this
minimalist GPL edition deliberately omits), the build and verification
workflow, the comments policy, and the definition of done. Apart from
the edition and licensing rules below, this file is only a pointer to
it, so the two cannot drift apart.

Task-specific workflows — building, diagnosing rendering regressions,
fixing web-platform compatibility, auditing security boundaries, and
porting changes between editions — live in `.agents/skills/`;
`.claude/skills/` holds stubs that point at the same files.

Harness note: `.claude/settings.json` sets `defaultMode:
bypassPermissions` plus a broad allow-list for the build, run, git and
inspect workflow. On a harness that does not read that file the
equivalent is full-access / never-ask; those routine commands must never
prompt.

## The editions

Northstar shares its hand-written engine with these browsers:

| Edition | Purpose | License |
|---|---|---|
| **Northstar** (this tree) | The purist GPL browser: free software under copyleft, every line of it distributable under the GPL | GPL-3.0-or-later |
| **Nordstjernen** (`nordstjernen-web/nordstjernen-browser`) | The flagship: every feature and platform, developed with external contributors | NSL-1.0 OR GPL-3.0-or-later |
| **Southstar** (`nordstjernen-web/southstar-browser`) | Nordstjernen's code base, being ported to Rust | NSL-1.0 OR GPL-3.0-or-later |

## Licensing and code provenance

Everything in this tree must be distributable under GPL-3.0-or-later:
the copyright holder's own work, contributions made to Northstar under
the GPL, or third-party code under a GPL-compatible license listed in
`THIRD-PARTY-LICENSES.md`. Purity is the point of this edition, so check
where code comes from before porting it:

- **From Nordstjernen and Southstar, only the copyright holder's own
  commits.** The flagship has offered the GPL alongside the Nordstjernen
  Source License since 1.0.28 (2026-10-04), but its external developers'
  contributions belong to them, and those made before then were
  contributed under NSL-1.0 alone. A Nordstjernen or Southstar commit
  may come across only when the copyright holder wrote it, or a Claude
  session wrote it on the copyright holder's behalf. Check the
  author of every source commit, including each commit inside a pull
  request (`git log --format='%an: %s'`); anything by another author
  stays out unless that author has licensed it under the GPL in writing.
  When such a fix is wanted, reproduce the bug here and write the fix
  from the bug and the specification, without working from the
  contributor's diff. The 1.0.12 standards-conformance backport broke
  this rule and was withdrawn in 1.0.13.
- **Name the source commit** in a `Ported-From:` trailer, as the
  `port-engine-changes` skill describes.
- **Mark security fixes.** Give every memory-safety or denial-of-service
  fix a `Security-Fix:` trailer with a stable slug, and keep the slug
  when the fix is ported, so the same bug has the same name in every
  edition:

  ```
  Security-Fix: css-grid-auto-repeat-overread
  Ported-From: nordstjernen-web/nordstjernen-browser@<full sha>
  Co-Authored-By: ...
  ```

  Both lines go in the final trailer block. Running
  `git log --format=%B | sed -n 's/^Security-Fix: //p' | sort -u` in two
  trees and comparing the lists shows which fixes have not crossed yet.
- **Outbound:** Nordstjernen may take only the copyright holder's own
  Northstar commits; GPL contributions from others stay GPL.
- **Dependencies** must be GPL-compatible. Nothing under NSL or a
  proprietary license is linked or vendored.
