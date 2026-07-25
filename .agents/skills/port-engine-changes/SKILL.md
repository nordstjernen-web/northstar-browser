---
name: port-engine-changes
description: Port a browser-engine change between sibling editions or long-lived branches of the same codebase, in either direction. Use for backports, forward-ports, and cherry-picks where the two trees share most engine sources but differ in features, helpers, or vendored dependencies.
---

# Port Engine Changes Between Editions

Read the repository instructions of the destination tree first. Preserve unrelated changes. The source commit is evidence, not authority: it was verified against a different tree.

## Establish what is already there

1. Compare commit subjects only to shortlist candidates. The same fix often lands in both trees under different wording, so a subject that looks absent may already be present.
2. Confirm presence or absence by searching the destination for the code the change introduces — a new function, field, or predicate — not by trusting the log.
3. Discard anything the destination excludes by policy: features the scope forbids, dependencies it vendors differently, and modules it does not build.

## Check applicability before committing to it

Generate a patch from the source commit and test it against the destination:

```sh
git format-patch -1 <commit> --stdout -- src/ > /tmp/port.patch
git apply --check /tmp/port.patch
```

Prefer this to adding a remote for the other tree: it keeps the histories separate and makes divergence visible immediately. A patch that does not apply is a signal to port by hand, not to force it.

## Expect edition-specific breakage

A change that compiles in one tree can fail in the other because the destination lacks a helper the source takes for granted, declares it later in the file, or gives it a different signature. Compilers report this as an implicit declaration followed by a conflicting redeclaration, and the symbol then fails to link.

Before porting, resolve every function the change calls against the destination. When a helper is missing, either lift it out of the destination's own equivalent code or express the call through primitives the destination already has. Do not copy a helper that duplicates one the destination spells differently.

## Verify in the destination, not the source

Compiling the changed translation units alone is not sufficient: it misses missing declarations in other units, link errors, and header drift. Build the whole destination tree.

Work in a clean worktree checked out at the destination's published head so a shared or mid-edit working tree cannot contaminate the result:

```sh
git worktree add --detach <tmp> origin/main
```

Then exercise the behavior the change is supposed to alter, and confirm it differs from the unported build in the direction the source commit claims. A port that builds but changes nothing observable has not been verified.

## Record the origin

Name the source commit in the message and describe the behavior in the destination's own terms. Do not copy a message that refers to code, tests, or scores that do not exist in the destination.

## Northstar context

Northstar is the GPL edition of Nordstjernen; the engine sources are largely shared and changes flow both ways. Both trees are edited continuously, so re-read the destination head immediately before porting.

Differences that break ports in practice: Northstar consumes quickjs-ng as an upstream meson subproject and carries no in-tree engine fork, so changes that patch a vendored JavaScript engine do not apply; the features the repository instructions exclude from this edition are absent here; and helper availability in `src/js.c` differs between the trees even where the surrounding code matches.

Push only to the destination's own remote. Never push one edition's history to the other's.
