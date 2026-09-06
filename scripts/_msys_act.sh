#!/usr/bin/env bash
# _msys_act.sh: run a page headless with an --act script and print the act-eval and click lines.
export MSYSTEM=MINGW64
export PATH="/mingw64/bin:$PATH"
repo="$(cd "$(dirname "$0")/.." && pwd)"
bin="${NS_BIN:-$repo/builddir/src/gtk/northstar.exe}"
page="$1"
acts="$2"
shift 2
cd "$repo" && "$bin" --headless --dump=none --viewport=800x600 --act="$acts" "$@" "$page" 2>&1 \
    | grep -E 'act-eval|^eval:|headless\] (click|hit)'
