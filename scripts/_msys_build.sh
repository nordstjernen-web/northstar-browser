#!/usr/bin/env bash
# _msys_build.sh: build the tree from an MSYS2 MINGW64 shell with the environment the toolchain needs.
set -euo pipefail
export MSYSTEM=MINGW64
export USERPROFILE="${USERPROFILE:-C:\\Users\\$USER}"
export PATH="/mingw64/bin:/c/Program Files/Git/cmd:$PATH"
export TMPDIR="${TMPDIR:-/tmp}"
export TMP="$TMPDIR"
export TEMP="$TMPDIR"
export CC="${CC:-clang}"
repo="$(cd "$(dirname "$0")/.." && pwd)"
builddir="${BUILDDIR:-$repo/builddir}"
if [ ! -f "$builddir/build.ninja" ]; then
    meson setup "$builddir" "$repo"
fi
meson compile -C "$builddir" "$@"
