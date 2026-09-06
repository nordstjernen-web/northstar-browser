#!/usr/bin/env bash
# _msys_wpt.sh: run a WPT directory in parallel through the headless browser from an MSYS2 shell and print pass counts.
# Usage: _msys_wpt.sh [--bin=EXE] [--jobs=N] [--timeout-ms=N] [--out=FILE] WPT-RELATIVE-PATH...
export MSYSTEM=MINGW64
export PATH="/mingw64/bin:$PATH"
export NS_ALLOW_ROOT=1
repo="$(cd "$(dirname "$0")/.." && pwd)"
wpt="${NS_WPT_ROOT:-/c/dev/wpt}"
bin="$repo/builddir/src/gtk/northstar.exe"
jobs=8
timeout_ms=6000
out=""
port=8123
paths=()
for arg in "$@"; do
    case "$arg" in
        --bin=*) bin="${arg#*=}" ;;
        --jobs=*) jobs="${arg#*=}" ;;
        --timeout-ms=*) timeout_ms="${arg#*=}" ;;
        --out=*) out="${arg#*=}" ;;
        --port=*) port="${arg#*=}" ;;
        *) paths+=("$arg") ;;
    esac
done
[ -n "$out" ] || out="$(mktemp /tmp/wpt-XXXXXX.txt)"
if ! curl -sf --max-time 2 -o /dev/null "http://127.0.0.1:$port/"; then
    (cd "$wpt" && python -m http.server "$port" --bind 127.0.0.1 >/dev/null 2>&1) &
    for _ in $(seq 1 30); do
        curl -sf --max-time 2 -o /dev/null "http://127.0.0.1:$port/" && break
        sleep 1
    done
fi
list="$(mktemp /tmp/wptlist-XXXXXX.txt)"
for p in "${paths[@]}"; do
    (cd "$wpt" && find "$p" -type f \( -name '*.html' -o -name '*.htm' \) \
        ! -path '*/resources/*' ! -path '*/support/*' ! -path '*/tools/*' \
        ! -path '*/crashtests/*' ! -name '*-ref.html' ! -name '*-manual.html' \
        ! -name '*-notref.html' \
        | while IFS= read -r f; do
            grep -lq 'testharness\.js' "$f" 2>/dev/null && printf '%s\n' "$f"
          done)
done | sort > "$list"
runone="$(mktemp /tmp/wptone-XXXXXX.sh)"
cat > "$runone" <<EOF
#!/usr/bin/env bash
rel="\$1"
res="\$(timeout -k 5 $(( timeout_ms / 1000 + 8 )) "$bin" --wpt --wpt-timeout-ms=$timeout_ms "http://127.0.0.1:$port/\$rel" 2>/dev/null | sed -n 's/^WPT SUMMARY //p' | head -1)"
printf '%s %s\n' "\$rel" "\${res:-BROKEN}"
EOF
chmod +x "$runone"
: > "$out"
xargs -P "$jobs" -n 1 "$runone" < "$list" >> "$out"
rm -f "$runone" "$list"
awk '{ for (i = 2; i <= NF; i++) { split($i, kv, "="); if (kv[1] == "total") t += kv[2]; if (kv[1] == "pass") p += kv[2]; } if ($2 == "BROKEN") b++; n++ }
     END { printf "files=%d broken=%d subtests=%d pass=%d\n", n, b, t, p }' "$out"
echo "results: $out"
