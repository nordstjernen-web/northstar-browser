# Northstar — agent operating guide

The operating guide for this repository is **[CLAUDE.md](CLAUDE.md)**,
and it applies to every coding agent, not only to Claude Code. Read it
before changing anything: it carries the project scope (what this
minimalist GPL edition deliberately omits), the build and verification
workflow, the comments policy, and the definition of done.

This file used to duplicate that guide for other harnesses. The copy
drifted — it still described a `<video>` element that laid out but never
decoded — so there is one authoritative document now, and this is a
pointer to it.

Harness note: `.claude/settings.json` sets `defaultMode:
bypassPermissions` plus a broad allow-list for the build, run, git and
inspect workflow. On a harness that does not read that file the
equivalent is full-access / never-ask; those routine commands must never
prompt.
