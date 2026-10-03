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

## The three editions

Northstar is one of three browsers built on the same hand-written engine:

| Edition | Purpose | License |
|---|---|---|
| **Northstar** (this tree) | The purist GPL browser: free software under copyleft, every line of it distributable under the GPL | GPL-3.0-or-later |
| **open-internet-navigator** (`nordstjernen-web/open-internet-navigator`) | The minimalist project | `Apache-2.0 OR MIT` |
| **Nordstjernen** (`nordstjernen-web/nordstjernen-browser`) | The flagship: every feature and platform, developed with external contributors | NSL-1.0 |

## Licensing and code provenance

Everything in this tree must be distributable under GPL-3.0-or-later:
the copyright holder's own work, contributions made to Northstar under
the GPL, or third-party code under a GPL-compatible license listed in
`THIRD-PARTY-LICENSES.md`. Purity is the point of this edition, so check
where code comes from before porting it:

- **From Nordstjernen, only the copyright holder's own commits.** The
  flagship is under the Nordstjernen Source License (NSL-1.0) and its
  external developers' contributions belong to them. A Nordstjernen
  commit may come across only when the copyright holder wrote it, or a
  Claude session wrote it on the copyright holder's behalf. Check the
  author of every source commit, including each commit inside a pull
  request (`git log --format='%an: %s'`); anything by another author
  stays out unless that author has licensed it under the GPL in writing.
  When such a fix is wanted, reproduce the bug here and write the fix
  from the bug and the specification, without working from the
  contributor's diff. The 1.0.12 standards-conformance backport broke
  this rule and was withdrawn in 1.0.13.
- **From open-internet-navigator, anything.** It is the copyright
  holder's own work under `Apache-2.0 OR MIT`, which the GPL can carry.
- **Name the source commit** in the message, as the
  `port-engine-changes` skill describes.
- **Outbound:** Nordstjernen may take only the copyright holder's own
  Northstar commits; GPL contributions from others stay GPL.
- **Dependencies** must be GPL-compatible. Nothing under NSL or a
  proprietary license is linked or vendored.
