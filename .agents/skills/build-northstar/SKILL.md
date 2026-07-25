---
name: build-northstar
description: Configure, compile, and smoke-test a Meson-based C or C++ desktop browser on Linux, macOS, or Windows, and diagnose dependency, compiler, linker, runtime-library, build-directory, or concurrent-build failures. Use after source changes, for clean builds, or when reproducing platform CI problems.
---

# Build a Cross-Platform Browser

Read the repository instructions, build guide, and current platform's CI workflow. Preserve unrelated changes.

## Inspect before building

1. Identify the operating system, architecture, shell, compiler, linker, dependency source, and intended feature set.
2. Inspect the working tree and existing build directory. Reuse it only when its platform, compiler, and important options are compatible.
3. Check for an active Meson, Ninja, or compiler process using the same build directory. Wait for its owner; do not delete locks or kill an unrelated build.
4. Treat CI configuration as the reproducible platform reference. Do not invent flags when reproducing CI.

## Select the environment

- Linux: use the native shell and the project's primary GCC or Clang configuration. Verify required pkg-config dependencies, including platform sandbox libraries.
- macOS: use Apple Clang and the documented package manager. Export required pkg-config search paths before configuring.
- Windows: use the supported MSYS2 MinGW environment, not an ordinary PowerShell or MSYS toolchain. Keep compiler, archiver, linker, and pkg-config packages from the same environment.

Do not disable required dependencies, hardening, or supported features merely to make configuration pass. Preserve optional-feature auto-detection unless the task requests a specific matrix entry.

## Configure and compile

Use the repository wrapper when it exists. Otherwise configure only when needed and compile through Meson:

```sh
meson setup builddir
meson compile -C builddir
```

If configuration is current, run only `meson compile -C builddir`. Use a separate clearly named build directory for a genuinely incompatible toolchain; never delete an existing directory before preserving useful logs.

## Diagnose failures

- Configure failure: inspect the first missing dependency, wrong pkg-config path, unsupported option, or stale cross file.
- Compile failure: fix the first project-source diagnostic before cascaded errors; distinguish project warnings from vendored-code warnings.
- Link failure: check compiler/archiver compatibility, LTO plugins, library order, subsystem flags, and runtime search paths.
- Runtime failure: check shared-library discovery, resources, sandbox policy, environment, and platform GUI requirements.
- Lock failure: identify the owning process and wait or use a different compatible build directory.

## Match the CI warning level

A local build usually runs at the project's default warning level while CI adds `-Werror`, so a warning that is invisible locally fails the pipeline. Before pushing a change, rebuild once at the CI warning level and restore the option afterwards:

```sh
meson configure builddir -Dwerror=true
meson compile -C builddir
meson configure builddir -Dwerror=false
```

Judge only diagnostics from the project's own sources. Vendored subprojects raise platform-specific warnings that the pipeline's platform does not, so a vendored `-Werror` failure on one host says nothing about CI on another.

## Separate your failure from someone else's

A shared checkout may be edited by another session while you build. When the build breaks in files your change does not touch, inspect the working tree before assuming your change is at fault, and reproduce in a clean worktree at the published head:

```sh
git worktree add --detach <tmp> origin/main
```

An empty or truncated headless run usually means a stale or half-written binary, not a rendering bug. Rebuild before diagnosing it.

## Verify in layers

Confirm the expected binary exists, run a built-in headless page, run deterministic smoke fixtures, and then launch the GUI for material changes. Terminate only the process started for the smoke check. Treat new warnings, empty headless output, baseline drift, or an early GUI exit as failures.

## Northstar commands

Use `scripts/dev.sh build`, then run the platform binary with `--headless --dump=text about:start` and run `scripts/dev.sh smoke`, which renders the `data/fixtures/` set and diffs each against its baseline in `data/baseline/`. It is the fastest honest regression gate in the repository; run it before every commit. Refresh baselines with `scripts/dev.sh baseline <target>` only after an intended change.

The binary is `builddir/src/gtk/northstar` on Linux/macOS and `builddir/src/gtk/northstar.exe` on Windows. Follow `docs/building.md` and `.github/workflows/{linux,musl,macos,windows}.yml` for dependencies and exact CI variants.

On Windows, build and run inside MSYS2 MinGW64. A binary launched without that environment's `bin` directory on `PATH` aborts at startup while loading a runtime library rather than reporting a browser error; export the MinGW64 path in any shell that launches the binary, including background and GUI launches. `ccache` is picked up automatically and takes a warm rebuild down to seconds.
