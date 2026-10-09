#!/usr/bin/env bash
# check-subprojects.sh: fail when a subprojects/ checkout is not at the revision its .wrap pins.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
stale=0

wrap_value() {
    sed -n "s/^$2[[:space:]]*=[[:space:]]*//p" "$1" | head -1 | tr -d '\r'
}

for wrap in "$ROOT"/subprojects/*.wrap; do
    grep -q '^\[wrap-git\]' "$wrap" || continue
    name=$(basename "$wrap" .wrap)
    dir=$(wrap_value "$wrap" directory)
    dir="$ROOT/subprojects/${dir:-$name}"
    [ -d "$dir" ] || continue
    rev=$(wrap_value "$wrap" revision)
    url=$(wrap_value "$wrap" url)
    if [ ! -e "$dir/.git" ]; then
        echo "check-subprojects: $name: not a git checkout, cannot verify $rev" >&2
        continue
    fi
    have=$(git -C "$dir" rev-parse HEAD)
    if [[ "$rev" =~ ^[0-9a-f]{7,40}$ ]]; then
        want=$rev
    else
        want=$(git -C "$dir" rev-parse -q --verify "refs/tags/$rev^{commit}" 2>/dev/null ||
               git ls-remote "$url" "refs/tags/$rev^{}" "refs/tags/$rev" "refs/heads/$rev" 2>/dev/null |
                   awk 'NR==1 || /\^\{\}$/ {sha=$1} END {print sha}')
        if [ -z "$want" ]; then
            echo "check-subprojects: $name: cannot resolve revision '$rev' (offline?)" >&2
            stale=1
            continue
        fi
    fi
    case "$have" in
        "$want"*) ;;
        *)
            echo "check-subprojects: $name is at ${have:0:12} but $(basename "$wrap") pins $rev" >&2
            stale=1
            ;;
    esac
done

if [ "$stale" -ne 0 ]; then
    echo "check-subprojects: delete the stale subprojects/<name> and run 'meson subprojects download <name>'." >&2
    exit 1
fi
