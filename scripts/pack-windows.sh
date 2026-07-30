#!/usr/bin/env bash
# Build a redistributable Northstar Windows bundle: a root launcher plus the
# mingw64 DLLs and runtime data under app/ so it runs outside MSYS2.
#
# Builds (or reuses) a separate --buildtype=release tree in $BUILDDIR so the
# shipped binary has NDEBUG defined — third-party assertions in vendored deps
# like quickjs-ng are compiled out, and the optimiser runs at -O3.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILDDIR=${BUILDDIR:-$ROOT/builddir-release}
OUT=${OUT:-$ROOT/dist/northstar-win64}
APP=$OUT/app
BIN_SRC=$BUILDDIR/src/win32/northstar.exe
LAUNCHER_SRC=$BUILDDIR/src/northstar-launcher.exe
BROWSER_EXE=northstar-ui.exe
EXTRA_MESON_SETUP_ARGS=()
if [ -n "${NS_MESON_SETUP_ARGS:-}" ]; then
    EXTRA_MESON_SETUP_ARGS=($NS_MESON_SETUP_ARGS)
fi

resolve_mingw_prefix() {
    for cand in "${MINGW_PREFIX:-}" /c/msys64/mingw64 "C:/msys64/mingw64" \
                /mingw64; do
        [ -n "$cand" ] || continue
        [ -d "$cand/bin" ] || continue
        [ -f "$cand/bin/libglib-2.0-0.dll" ] || continue
        echo "$cand"; return
    done
    return 1
}

MINGW_PREFIX=$(resolve_mingw_prefix) || {
    echo "pack-windows: could not find mingw64/bin; set MINGW_PREFIX." >&2
    exit 1
}

if [ ! -d "$BUILDDIR" ]; then
    meson setup "$BUILDDIR" --buildtype=release "${EXTRA_MESON_SETUP_ARGS[@]}"
elif [ ${#EXTRA_MESON_SETUP_ARGS[@]} -gt 0 ]; then
    meson configure "$BUILDDIR" "${EXTRA_MESON_SETUP_ARGS[@]}"
fi
meson compile -C "$BUILDDIR"

if [ ! -x "$BIN_SRC" ]; then
    echo "pack-windows: build did not produce $BIN_SRC" >&2
    exit 1
fi
if [ ! -x "$LAUNCHER_SRC" ]; then
    echo "pack-windows: build did not produce $LAUNCHER_SRC" >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$APP"
cp "$LAUNCHER_SRC" "$OUT/northstar.exe"
cp "$BIN_SRC" "$APP/$BROWSER_EXE"
validate_launcher_imports() {
    local dep src alt
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        src=$MINGW_PREFIX/bin/$dep
        if [ ! -f "$src" ]; then
            alt=$(find "$MINGW_PREFIX/bin" -maxdepth 1 -iname "$dep" -print -quit 2>/dev/null || true)
            [ -n "$alt" ] && src=$alt
        fi
        if [ -f "$src" ]; then
            printf 'pack-windows: root launcher imports %s; keep it system-only before using the app/ layout\n' \
                "$dep" >&2
            exit 1
        fi
    done < <(objdump -p "$OUT/northstar.exe" 2>/dev/null | awk '/DLL Name:/ {print $3}')
}

validate_launcher_imports

if [ -d "$MINGW_PREFIX/etc/fonts" ]; then
    mkdir -p "$APP/etc"
    cp -r "$MINGW_PREFIX/etc/fonts" "$APP/etc/"
fi

# Transitively resolve DLL dependencies starting from the executable.
# objdump reports import names; we look them up in the
# mingw bin dir and skip anything that resolves to a Windows system DLL.
declare -A seen
queue=("$APP/$BROWSER_EXE")
while [ ${#queue[@]} -gt 0 ]; do
    cur=${queue[0]}
    queue=("${queue[@]:1}")
    deps=$(objdump -p "$cur" 2>/dev/null | awk '/DLL Name:/ {print $3}') || true
    for dep in $deps; do
        key=$(printf '%s' "$dep" | tr '[:upper:]' '[:lower:]')
        if [ -n "${seen[$key]:-}" ]; then continue; fi
        seen[$key]=1
        src=$MINGW_PREFIX/bin/$dep
        if [ ! -f "$src" ]; then
            # case-insensitive fallback for DLLs whose import name capitalisation
            # differs from the on-disk file name
            alt=$(find "$MINGW_PREFIX/bin" -maxdepth 1 -iname "$dep" -print -quit 2>/dev/null || true)
            [ -n "$alt" ] && src=$alt
        fi
        if [ -f "$src" ]; then
            cp "$src" "$APP/"
            queue+=("$APP/$(basename "$src")")
        fi
    done
done

dll_exports_symbol() {
    objdump -p "$1" 2>/dev/null | awk -v want="$2" '
        /^[[:space:]]*\[[[:space:]]*[0-9]+\][[:space:]]/ && $NF == want {
            found = 1
        }
        END { exit found ? 0 : 1 }
    '
}

validate_ngtcp2_ossl() {
    local ngtcp2 ssl_dep ssl
    ngtcp2=$(find "$APP" -maxdepth 1 -iname 'libngtcp2_crypto_ossl-0.dll' -print -quit 2>/dev/null || true)
    [ -n "$ngtcp2" ] || return 0
    ssl_dep=$(objdump -p "$ngtcp2" 2>/dev/null | awk '
        /DLL Name:/ { dep = $3; next }
        dep != "" && $NF == "SSL_set_quic_tls_cbs" { print dep; exit }
    ')
    [ -n "$ssl_dep" ] || return 0
    ssl=$(find "$APP" -maxdepth 1 -iname "$ssl_dep" -print -quit 2>/dev/null || true)
    if [ -z "$ssl" ]; then
        printf 'pack-windows: %s imports SSL_set_quic_tls_cbs from missing %s\n' \
            "$(basename "$ngtcp2")" "$ssl_dep" >&2
        exit 1
    fi
    if ! dll_exports_symbol "$ssl" SSL_set_quic_tls_cbs; then
        printf 'pack-windows: %s imports SSL_set_quic_tls_cbs, but bundled %s does not export it\n' \
            "$(basename "$ngtcp2")" "$(basename "$ssl")" >&2
        exit 1
    fi
}

validate_ngtcp2_ossl

# Per-application data: license text. The browser reads it relative to
# the exe at runtime (see src/net.c::about_read_first).
mkdir -p "$APP/share/northstar"
cp "$ROOT/LICENSE" "$APP/share/northstar/"
cp "$ROOT/THIRD-PARTY-LICENSES.md" "$APP/share/northstar/"
cp -r "$ROOT/data/i18n" "$APP/share/northstar/"

mkdir -p "$APP/share/icons/hicolor/scalable/apps"
cp "$ROOT/data/icons/hicolor/scalable/apps/northstar.gif" \
   "$APP/share/icons/hicolor/scalable/apps/"

# Northstar's own GPL text plus the third-party copyright + license notices
# required by the libraries we ship, both at the root of the bundle.
cp "$ROOT/LICENSE" "$OUT/LICENSE.txt"
cp "$ROOT/THIRD-PARTY-LICENSES.md" "$OUT/"

# CA certificate bundle for libcurl HTTPS verification.
mkdir -p "$APP/etc/ssl/certs"
for ca in \
    "$MINGW_PREFIX/etc/ssl/certs/ca-bundle.crt" \
    "$MINGW_PREFIX/etc/ssl/cert.pem" \
    "$MINGW_PREFIX/ssl/certs/ca-bundle.crt"; do
    if [ -f "$ca" ]; then
        cp "$ca" "$APP/etc/ssl/certs/ca-bundle.crt"
        break
    fi
done

bundled=$(find "$APP" -maxdepth 1 -name '*.dll' | wc -l)
size=$(du -sh "$OUT" | awk '{print $1}')
printf 'pack-windows: bundled %s DLLs, total size %s, output: %s\n' "$bundled" "$size" "$OUT"
