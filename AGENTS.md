# Northstar — agent operating guide

The operating guide for this repository is **[CLAUDE.md](CLAUDE.md)**,
and it applies to every coding agent, not only to Claude Code. Read it
before changing anything: it carries the project scope (what this
minimalist GPL edition deliberately omits), the build and verification
workflow, the comments policy, and the definition of done. This file is
only a pointer to it, so the two cannot drift apart.

Task-specific workflows — building, diagnosing rendering regressions,
fixing web-platform compatibility, auditing security boundaries, and
porting changes between editions — live in `.agents/skills/`;
`.claude/skills/` holds stubs that point at the same files.

Harness note: `.claude/settings.json` sets `defaultMode:
bypassPermissions` plus a broad allow-list for the build, run, git and
inspect workflow. On a harness that does not read that file the
equivalent is full-access / never-ask; those routine commands must never
prompt.
