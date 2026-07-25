---
name: build-northstar
description: Configure, compile, and smoke-test the Northstar browser on Linux, macOS, or Windows, and diagnose platform-specific Meson, compiler, linker, pkg-config, or dependency failures. Use after source changes, for clean builds, or when reproducing CI build problems on a supported desktop platform.
---

# Build Northstar

Read `AGENTS.md`, `docs/building.md`, and the workflow for the current platform under `.github/workflows/`. Preserve unrelated working-tree changes and reuse a compatible `builddir`.

## Choose the platform environment

- Linux: use the native shell with GCC by default. Install the packages listed in `docs/building.md`; `libseccomp` is required.
- macOS: use the native shell with Homebrew Clang. Add Homebrew curl and OpenSSL pkg-config directories exactly as shown in `docs/building.md`.
- Windows: run inside MSYS2 MINGW64, not ordinary PowerShell or the MSYS shell. Use the packages and Clang/LLD configuration in `.github/workflows/windows.yml`.

Do not disable a required dependency or hardening option merely to make configuration pass. Optional Enchant and Ogg decoders may remain absent.

## Configure and compile

If `builddir/build.ninja` exists, run:

```sh
meson compile -C builddir
```

Otherwise run:

```sh
meson setup builddir
meson compile -C builddir
```

Use `./scripts/dev.sh build` as the equivalent convenience command in a POSIX shell. For a CI reproduction, copy the compiler, LTO, `--werror`, and feature flags from that platform's workflow instead of inventing new flags. Never delete an existing build directory without first proving it is incompatible and preserving any useful logs.

## Verify

Run the platform binary against the built-in page:

```sh
./builddir/src/gtk/northstar --headless --dump=text about:start
```

On Windows use `./builddir/src/gtk/northstar.exe`. Confirm the output contains `Northstar`, then run `./scripts/dev.sh smoke` where a POSIX shell is available.

For material changes, launch the GUI binary in the background, allow startup to complete, and terminate only that process. Treat new compiler warnings, a failed headless render, baseline drift, or a failed launch as a build failure and diagnose it before committing.
