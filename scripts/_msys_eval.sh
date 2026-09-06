#!/usr/bin/env bash
# _msys_eval.sh: run a page headless and print the value of a JavaScript expression.
export MSYSTEM=MINGW64
export PATH="/mingw64/bin:$PATH"
repo="$(cd "$(dirname "$0")/.." && pwd)"
page="$1"
shift
expr="${1:-window.__result}"
shift 2>/dev/null
cd "$repo" && ./builddir/src/gtk/northstar.exe --headless --dump=none \
    --viewport=800x600 --eval="$expr" "$@" "$page" 2>/dev/null | grep '^eval:'
