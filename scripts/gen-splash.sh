#!/usr/bin/env bash
# gen-splash.sh — regenerate src/about_splash_gif.h, the about:start splash animation.
set -euo pipefail

cd "$(dirname "$0")/.."

ver=$(sed -n "s/^[[:space:]]*version:[[:space:]]*'\([^']*\)'.*/\1/p" meson.build | head -n1)
[ -n "$ver" ] || { echo "could not read version from meson.build" >&2; exit 1; }
ver=${ver%%-*}
codename='A fine open source web browser.'

FRAMES=${NS_SPLASH_FRAMES:-36}
DELAY=${NS_SPLASH_DELAY:-11}
LEVELS=${NS_SPLASH_LEVELS:-14}
LOSSY=${NS_SPLASH_LOSSY:-60}

find_font() {
    local q=$1; shift
    if command -v fc-match >/dev/null 2>&1; then
        local f; f=$(fc-match -f '%{file}' "$q" 2>/dev/null || true)
        [ -n "$f" ] && [ -f "$f" ] && { echo "$f"; return 0; }
    fi
    local p
    for p in "$@"; do [ -f "$p" ] && { echo "$p"; return 0; }; done
    echo "missing font: $q" >&2; return 1
}
python3 -c 'import numpy, PIL' 2>/dev/null || {
    echo "splash rendering needs python3 with numpy and pillow" >&2; exit 1; }

fr=$(find_font 'Liberation Sans' \
    /usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf \
    /usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf \
    /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf)
fb=$(find_font 'Liberation Sans:bold' \
    /usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf \
    /usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf \
    /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf)

if [ -n "${NS_SPLASH_WORKDIR:-}" ]; then
    w=$NS_SPLASH_WORKDIR; mkdir -p "$w"
else
    w=$(mktemp -d)
    trap 'rm -rf "$w"' EXIT
fi

S=3
W=$((940 * S)); H=$((320 * S))
HORIZON=$(python3 -c "print(int($H*0.680))")

SUNX=$(python3 -c "print(int($W*0.800))"); SUNY=$(python3 -c "print(int($H*0.360))")
POLARX=$(python3 -c "print(int($W*0.588))"); POLARY=$(python3 -c "print(int($H*0.172))")

python3 - "$W" "$H" "$HORIZON" "$SUNX" "$SUNY" "$POLARX" "$POLARY" "$S" "$w/sky.png" <<'PY'
import sys
import numpy as np
from PIL import Image

W, H, HZ = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
SUNX, SUNY = float(sys.argv[4]), float(sys.argv[5])
POLARX, POLARY = float(sys.argv[6]), float(sys.argv[7])
S = float(sys.argv[8])
out = sys.argv[9]

STOPS = [
    (0.00, (3, 26, 88)), (0.16, (8, 46, 126)), (0.34, (20, 82, 168)),
    (0.54, (54, 134, 208)), (0.74, (124, 188, 232)), (0.90, (196, 227, 248)),
    (1.00, (231, 246, 254)),
]
WARM = np.array([255.0, 246.0, 218.0], dtype=np.float32)
HAZE = np.array([233.0, 247.0, 255.0], dtype=np.float32)

yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
t = np.clip(yy/float(HZ), 0.0, 1.0)
t = t*t*(3.0 - 2.0*t)

xs = np.array([s[0] for s in STOPS], dtype=np.float32)
cs = np.array([s[1] for s in STOPS], dtype=np.float32)
sky = np.stack([np.interp(t, xs, cs[:, k]) for k in range(3)], axis=-1)

dsun = np.sqrt((xx - SUNX)**2 + (yy - SUNY)**2)
mie = 0.55*np.exp(-(dsun/(W*0.062))**1.25) + 0.17*np.exp(-(dsun/(W*0.20))**1.6)
sky += (WARM - sky)*np.clip(mie, 0.0, 1.0)[..., None]

band = np.exp(-((yy - HZ)/(H*0.20))**2)*np.clip(1.0 - t*0.15, 0.0, 1.0)
sky += (HAZE - sky)*(band*0.34)[..., None]

rng = np.random.default_rng(20260808)
dark = np.clip((0.46 - t)/0.46, 0.0, 1.0)**1.45


def splat(cx, cy, radius, colour, gain):
    x0 = max(int(cx - radius*3.5), 0)
    x1 = min(int(cx + radius*3.5) + 1, W)
    y0 = max(int(cy - radius*3.5), 0)
    y1 = min(int(cy + radius*3.5) + 1, H)
    if x1 <= x0 or y1 <= y0:
        return
    gy, gx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    d2 = (gx - cx)**2 + (gy - cy)**2
    a = np.exp(-d2/(2.0*radius*radius))*gain
    tile = sky[y0:y1, x0:x1]
    sky[y0:y1, x0:x1] = tile + (np.array(colour, dtype=np.float32) - tile)*np.clip(a, 0.0, 1.0)[..., None]


for _ in range(210):
    sx = rng.uniform(0, W)
    sy = rng.uniform(0, HZ*0.72)
    vis = float(np.interp(sy/float(HZ), [0.0, 0.46], [1.0, 0.0]))**1.45
    wash = min(1.0, float(np.hypot(sx - SUNX, sy - SUNY))/(W*0.20))
    mag = rng.random()**2.6
    gain = (0.20 + 0.62*mag)*vis*wash
    if gain < 0.035:
        continue
    tint = rng.random()
    colour = (255.0, 250.0 - 12.0*tint, 236.0 + 19.0*tint)
    splat(sx, sy, S*(0.42 + 0.62*mag), colour, gain)

splat(POLARX, POLARY, S*11.0, (188.0, 220.0, 255.0), 0.30)
splat(POLARX, POLARY, S*4.2, (232.0, 244.0, 255.0), 0.55)

grain = rng.normal(0.0, 0.70, size=(H, W)).astype(np.float32)
sky = np.clip(sky + grain[..., None]*(0.35 + 0.65*dark[..., None]), 0, 255)
Image.fromarray(sky.astype(np.uint8), "RGB").save(out)
PY

PLANET_CY=$(python3 -c "print(int($HORIZON + $H*2.05))")
PLANET_RX=$(python3 -c "print(int($W*1.28))")
PLANET_RY=$(python3 -c "print(int($H*2.05))")

python3 - "$w" <<'PY'
import sys
import numpy as np

wd = sys.argv[1]
TW = TH = 2048
SEA = 0.500
rng = np.random.default_rng(1997)


def vnoise(g):
    grid = rng.random((g, g)).astype(np.float32)
    ys = np.arange(TH, dtype=np.float32)*g/TH
    xs = np.arange(TW, dtype=np.float32)*g/TW
    y0 = np.floor(ys).astype(np.int64) % g
    x0 = np.floor(xs).astype(np.int64) % g
    fy = ys - np.floor(ys)
    fx = xs - np.floor(xs)
    fy = (fy*fy*(3 - 2*fy))[:, None]
    fx = (fx*fx*(3 - 2*fx))[None, :]
    y1 = (y0 + 1) % g
    x1 = (x0 + 1) % g
    a = grid[np.ix_(y0, x0)]
    b = grid[np.ix_(y0, x1)]
    c = grid[np.ix_(y1, x0)]
    e = grid[np.ix_(y1, x1)]
    return (a*(1 - fx) + b*fx)*(1 - fy) + (c*(1 - fx) + e*fx)*fy


def fbm(base, octaves, gain=0.5):
    total = np.zeros((TH, TW), dtype=np.float32)
    amp, norm, g = 1.0, 0.0, base
    for _ in range(octaves):
        total += amp*vnoise(g)
        norm += amp
        amp *= gain
        g *= 2
    return total/norm


def ridged(base, octaves, gain=0.5):
    total = np.zeros((TH, TW), dtype=np.float32)
    amp, norm, g = 1.0, 0.0, base
    for _ in range(octaves):
        total += amp*(1.0 - np.abs(2.0*vnoise(g) - 1.0))**2
        norm += amp
        amp *= gain
        g *= 2
    return total/norm


def unit(a):
    return (a - a.min())/max(np.ptp(a), 1e-9)


stops = [
    (0.000, (10, 38, 96)), (0.180, (14, 58, 132)), (0.320, (22, 92, 170)),
    (0.410, (34, 124, 196)), (0.462, (58, 158, 212)), (0.486, (104, 196, 222)),
    (0.496, (168, 226, 226)), (0.500, (226, 224, 184)), (0.508, (206, 202, 142)),
    (0.535, (150, 178, 96)), (0.590, (94, 150, 70)), (0.665, (56, 118, 58)),
    (0.740, (78, 122, 60)), (0.812, (124, 130, 84)), (0.878, (162, 152, 126)),
    (0.948, (198, 196, 190)), (0.978, (232, 238, 242)), (1.000, (253, 254, 255)),
]
xs = np.array([s[0] for s in stops], dtype=np.float32)
cols = np.array([s[1] for s in stops], dtype=np.float32)
grade = np.linspace(0.0, 1.0, 1024, dtype=np.float32)
terrain = np.stack([np.interp(grade, xs, cols[:, k]) for k in range(3)], axis=1)

continent = unit(fbm(3, 5))
coast = unit(fbm(10, 7))
height = np.clip(0.5 + (continent*0.74 + coast*0.26 - 0.5)*1.92, 0.0, 1.0)

land = np.clip((height - SEA)/0.055, 0.0, 1.0)
ranges = np.clip((unit(fbm(5, 3)) - 0.40)/0.34, 0.0, 1.0)
height = np.clip(height + ridged(9, 5)*land*ranges*0.25, 0.0, 1.0)
height = np.clip(height + (unit(fbm(40, 3)) - 0.5)*0.022*land, 0.0, 1.0)

surface = terrain[np.clip(height*1023, 0, 1023).astype(np.int64)]

moist = unit(fbm(6, 4))
arid = np.clip((0.36 - moist)/0.26, 0.0, 1.0)**1.25
surface[..., 0] += (198.0 - surface[..., 0])*(arid*0.40*land)
surface[..., 1] += (178.0 - surface[..., 1])*(arid*0.34*land)
surface[..., 2] += (116.0 - surface[..., 2])*(arid*0.44*land)
lush = np.clip((moist - 0.54)/0.30, 0.0, 1.0)
surface[..., 0] += (44.0 - surface[..., 0])*(lush*0.26*land)
surface[..., 1] += (104.0 - surface[..., 1])*(lush*0.22*land)
surface[..., 2] += (48.0 - surface[..., 2])*(lush*0.26*land)

LIGHT = np.array([0.62, -0.46, 0.64], dtype=np.float32)
LIGHT /= np.linalg.norm(LIGHT)
gy, gx = np.gradient(height.astype(np.float32))
relief = 190.0
nz = 1.0/np.sqrt(gx*gx*relief*relief + gy*gy*relief*relief + 1.0)
shade = np.clip((-gx*relief*LIGHT[0] - gy*relief*LIGHT[1] + LIGHT[2])*nz, 0.0, 1.0)
shade = 0.74 + 0.52*shade**1.15
surface *= np.where(land > 0.0, shade, 1.0)[..., None]

deep = np.clip((SEA - height)/0.34, 0.0, 1.0)
surface[..., 2] += (150.0 - surface[..., 2])*((1.0 - land)*deep*0.16)
surface = np.clip(surface, 0.0, 255.0)

cloudbase = unit(fbm(4, 7))
cloud = np.clip((cloudbase - 0.545)/0.185, 0.0, 1.0)**0.85
cloud *= np.clip((unit(fbm(14, 4)) - 0.20)/0.55, 0.0, 1.0)**0.45
ocean = np.clip((SEA - height)/0.045, 0.0, 1.0)

np.save(wd + "/tex_surface.npy", surface.astype(np.float32))
np.save(wd + "/tex_extra.npy", np.stack([cloud, ocean], axis=-1).astype(np.float32))
PY

cat > "$w/earth.py" <<'PY'
import sys
import numpy as np
from PIL import Image

wd = sys.argv[1]
outpath = sys.argv[2]
W, H, HZ = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
PCY, PRX, PRY = float(sys.argv[6]), float(sys.argv[7]), float(sys.argv[8])
SUNX = float(sys.argv[9])

TILE = 24.0
FOCAL = W*0.45
VSTRETCH = 1.8
LODBIAS = 1.55
HAZE = np.array([216.0, 239.0, 252.0], dtype=np.float32)
SUNLIT = np.array([255.0, 243.0, 214.0], dtype=np.float32)


def mips(img, levels):
    out = [img]
    cur = img
    for _ in range(levels):
        cur = 0.25*(cur[0::2, 0::2] + cur[1::2, 0::2] +
                    cur[0::2, 1::2] + cur[1::2, 1::2])
        out.append(cur)
    return out


SURF = mips(np.load(wd + "/tex_surface.npy"), 6)
EXTRA = mips(np.load(wd + "/tex_extra.npy"), 6)
TH, TW = SURF[0].shape[:2]


def fetch(stack, level, u, v):
    m = stack[level]
    h, w = m.shape[:2]
    fy = v*h - 0.5
    fx = u*w - 0.5
    y0 = np.floor(fy).astype(np.int64)
    x0 = np.floor(fx).astype(np.int64)
    ty = (fy - y0).astype(np.float32)[..., None]
    tx = (fx - x0).astype(np.float32)[..., None]
    y0 %= h
    x0 %= w
    y1 = (y0 + 1) % h
    x1 = (x0 + 1) % w
    a = m[y0, x0]
    b = m[y0, x1]
    c = m[y1, x0]
    d = m[y1, x1]
    return (a*(1 - tx) + b*tx)*(1 - ty) + (c*(1 - tx) + d*tx)*ty


def sample(stack, u, v, lod):
    lod = np.clip(lod, 0.0, len(stack) - 1.0)
    acc = np.zeros(u.shape + (stack[0].shape[2],), dtype=np.float32)
    for level in range(len(stack)):
        wgt = np.clip(1.0 - np.abs(lod - level), 0.0, 1.0).astype(np.float32)
        if not wgt.any():
            continue
        acc += fetch(stack, level, u, v)*wgt[..., None]
    return acc


Y0 = HZ - 2
yy, xx = np.mgrid[Y0:H, 0:W].astype(np.float32)
nx = np.clip((xx - W/2)/PRX, -1.0, 1.0)
limb = PCY - PRY*np.sqrt(np.clip(1.0 - nx*nx, 0.0, None))
t = np.maximum(yy - limb, 0.35)

depth = FOCAL/t
lateral = (xx - W/2)/t
u = lateral/TILE
v = depth/(TILE*VSTRETCH)
lod = np.maximum(np.log2(np.maximum((TW/TILE)*np.sqrt(FOCAL/VSTRETCH)/t**1.5, 1.0)) - LODBIAS, 0.0)

col = sample(SURF, u, v, lod)
extra = sample(EXTRA, u, v, lod)
ocean = extra[..., 1]
cover = sample(EXTRA, u/1.25, v/1.6, np.maximum(lod - 0.4, 0.0))[..., 0]
cover *= np.clip(1.0 - lod/6.5, 0.0, 1.0)
shadow = sample(EXTRA, u/1.25 - 0.004, v/1.6 - 0.002, np.maximum(lod, 1.0))[..., 0]
col *= (1.0 - 0.20*np.clip(shadow - cover*0.45, 0.0, 1.0))[..., None]

glint = np.exp(-((xx - SUNX)/(W*0.016 + t*0.36))**2)
glint *= np.clip((t - 16.0)/90.0, 0.0, 1.0)*np.clip(1.0 - lod/7.5, 0.0, 1.0)*ocean
sparkle = np.clip(extra[..., 0]*1.6 - 0.35, 0.0, 1.0)
col += (SUNLIT - col)*(glint*(0.58 + 0.40*sparkle))[..., None]
col += (np.float32(255.0) - col)*(cover*0.58)[..., None]
col *= (0.95 + 0.12*np.exp(-((xx - SUNX)/(W*0.52))**2))[..., None]

grey = col.mean(axis=-1, keepdims=True)
col = np.clip(grey + (col - grey)*1.16, 0, 255)
haze = np.clip(np.exp(-t/np.float32(68.0)), 0.0, 1.0)**0.95
tint = HAZE + (SUNLIT - HAZE)*np.clip(np.exp(-((xx - SUNX)/(W*0.30))**2), 0.0, 1.0)[..., None]
col += (tint - col)*haze[..., None]

out = np.zeros((H, W, 4), dtype=np.uint8)
out[Y0:, :, :3] = np.clip(col, 0, 255).astype(np.uint8)
out[Y0:, :, 3] = (np.clip(yy - limb + 0.5, 0.0, 1.0)*255).astype(np.uint8)
Image.fromarray(out, "RGBA").save(outpath)
PY

convert -size ${W}x${H} xc:none -fill none -stroke '#f2fbff' -strokewidth $((5*S)) \
    -draw "ellipse $((W/2)),${PLANET_CY} ${PLANET_RX},${PLANET_RY} 0,360" \
    -blur 0x$((3*S)) -background black -alpha remove -alpha off "$w/limb.png"
convert -size ${W}x${H} xc:none -fill none -stroke '#9ed3f4' -strokewidth $((16*S)) \
    -draw "ellipse $((W/2)),${PLANET_CY} ${PLANET_RX},${PLANET_RY} 0,360" \
    -blur 0x$((26*S)) -background black -alpha remove -alpha off "$w/limbglow.png"

python3 "$w/earth.py" "$w" "$w/ground.png" "$W" "$H" "$HORIZON" \
    "$PLANET_CY" "$PLANET_RX" "$PLANET_RY" "$SUNX"

convert "$w/sky.png" \
    "$w/ground.png" -compose over -composite \
    "$w/limbglow.png" -compose screen -composite \
    "$w/limb.png" -compose screen -composite "$w/bg.png"

SX=$SUNX; SY=$SUNY
convert -size ${W}x${H} xc:none -fill '#ffc65a' \
    -draw "ellipse ${SX},${SY} $((44*S)),$((44*S)) 0,360" -blur 0x$((34*S)) "$w/halo.png"
convert -size ${W}x${H} xc:none -fill '#fff3d2' \
    -draw "ellipse ${SX},${SY} $((18*S)),$((18*S)) 0,360" -blur 0x$((12*S)) "$w/halo2.png"
convert "$w/halo.png" "$w/halo2.png" -compose over -composite \
    -background black -alpha remove -alpha off "$w/sunhalo.png"

P() { echo $(( $1 * S )); }
convert -background none -font "$fb" -pointsize $(P 52) -kerning $((1*S)) -fill '#ffffff' label:'Northstar ' "$w/t1.png"
convert -background none -font "$fb" -pointsize $(P 52) -fill '#ffd23f' label:"$ver" "$w/t2.png"
convert -background none -font "$fb" -pointsize $(P 24) -fill '#d9eeff' label:'Northstar Web Browser' "$w/ts.png"
convert -background none -font "$fr" -pointsize $(P 20) -fill '#0a2b5c' -size $((700*S))x caption:"$codename" "$w/tc.png"

for n in t1 t2 ts; do
    convert "$w/$n.png" -fill '#04203f' -colorize 100 "$w/${n}k.png"
done
convert "$w/tc.png" -fill '#eaf5ff' -colorize 100 "$w/tck.png"
for n in t1 t2; do
    convert "$w/$n.png" -fill '#0d3f86' -colorize 100 "$w/${n}h.png"
done

w1=$(identify -format '%w' "$w/t1.png"); h1=$(identify -format '%h' "$w/t1.png")
hc=$(identify -format '%h' "$w/tc.png")
ty=$((42*S)); textleft=$((70*S))
sy=$((ty + h1 + 10*S)); cy=$(( H - hc - 106*S ))
sh=$((2*S))

convert -size ${W}x${H} xc:none \
    "$w/t1k.png" -gravity NorthWest -geometry +$((textleft + sh))+$((ty + sh)) -compose over -composite \
    "$w/t2k.png" -gravity NorthWest -geometry +$((textleft + w1 + sh))+$((ty + sh)) -compose over -composite \
    "$w/tsk.png" -gravity NorthWest -geometry +$((textleft + sh))+$((sy + sh)) -compose over -composite \
    "$w/tck.png" -gravity NorthWest -geometry +$((textleft + sh))+$((cy + sh)) -compose over -composite \
    "$w/t1h.png" -gravity NorthWest -geometry +$((textleft - S))+$((ty - S)) -compose over -composite \
    "$w/t2h.png" -gravity NorthWest -geometry +$((textleft + w1 - S))+$((ty - S)) -compose over -composite \
    "$w/t1.png"  -gravity NorthWest -geometry +${textleft}+${ty} -compose over -composite \
    "$w/t2.png"  -gravity NorthWest -geometry +$((textleft + w1))+${ty} -compose over -composite \
    "$w/ts.png"  -gravity NorthWest -geometry +${textleft}+${sy} -compose over -composite \
    "$w/tc.png"  -gravity NorthWest -geometry +${textleft}+${cy} -compose over -composite \
    "$w/textlayer.png"

python3 - "$W" "$H" "$S" > "$w/frame.mvg" <<'PY'
import sys
W, H, S = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
out = []
def band(i, col):
    out.append("fill none stroke %s stroke-width %d rectangle %.1f,%.1f %.1f,%.1f" % (
        col, S, i*S + S*0.5, i*S + S*0.5, W - i*S - S*0.5, H - i*S - S*0.5))
def edge(i, tl, br):
    o = i*S
    out.append("fill %s stroke none polygon %d,%d %d,%d %d,%d %d,%d %d,%d %d,%d" % (
        tl, o, o, W-o, o, W-o-S, o+S, o+S, o+S, o+S, H-o-S, o, H-o))
    out.append("fill %s stroke none polygon %d,%d %d,%d %d,%d %d,%d %d,%d %d,%d" % (
        br, W-o, o, W-o, H-o, o, H-o, o+S, H-o-S, W-o-S, H-o-S, W-o-S, o+S))
band(0, "#000000")
edge(1, "#f4f4f4", "#5a5a5a")
band(2, "#c6c6c6")
band(3, "#c6c6c6")
band(4, "#c6c6c6")
edge(5, "#5a5a5a", "#f4f4f4")
band(7, "#000000")
sys.stdout.write(" ".join(out))
PY

cat > "$w/anim.py" <<'PY'
import sys, math, random

W, H, S = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])
T = float(sys.argv[4]); wd = sys.argv[5]; idx = int(sys.argv[6])
SX, SY = float(sys.argv[7]), float(sys.argv[8])
PX, PY = float(sys.argv[9]), float(sys.argv[10])
TAU = 2*math.pi
SKY = (170, 208, 238)
out = []
clouds = []

def ell(col, cx, cy, rx, ry, dst=None):
    (out if dst is None else dst).append(
        "fill %s stroke none ellipse %.1f,%.1f %.1f,%.1f 0,360" % (col, cx, cy, rx, ry))

def poly(col, pts, dst=None):
    (out if dst is None else dst).append(
        "fill %s stroke none polygon %s" % (col, " ".join("%.1f,%.1f" % p for p in pts)))

def rell(col, cx, cy, rx, ry, ang, dst=None):
    a = math.radians(ang)
    ca, sa = math.cos(a), math.sin(a)
    pts = []
    for k in range(28):
        th = TAU*k/28
        x, y = rx*math.cos(th), ry*math.sin(th)
        pts.append((cx + x*ca - y*sa, cy + x*sa + y*ca))
    poly(col, pts, dst)

def hx(c):
    return "#%02x%02x%02x" % tuple(int(max(0, min(255, v))) for v in c)

def rgba(c, a):
    return "rgba(%d,%d,%d,%.3f)" % (c[0], c[1], c[2], max(0.0, min(1.0, a)))

def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i])*t for i in range(3))

CLOUDS = [
    (0.108, 0.505, 0.079, 0.00, 11),
    (0.302, 0.462, 0.050, 1.90, 23),
    (0.556, 0.500, 0.082, 3.40, 37),
    (0.735, 0.470, 0.036, 0.40, 29),
    (0.822, 0.536, 0.061, 2.30, 51),
    (0.976, 0.462, 0.047, 4.70, 67),
    (0.215, 0.577, 0.024, 2.70, 71),
    (0.668, 0.592, 0.029, 5.60, 83),
    (0.892, 0.618, 0.023, 0.90, 97),
    (0.404, 0.627, 0.019, 3.10, 113),
]

for fx, fy, sc, phase, seed in CLOUDS:
    rnd = random.Random(seed)
    R = H*sc
    cx = W*fx + math.sin(TAU*T + phase)*W*0.0045*(sc/0.05)
    cy = H*fy + math.sin(TAU*T + phase*1.7)*H*0.0035
    haze = max(0.0, min(0.85, (0.056 - sc)/0.042))
    under = mix((120, 162, 206), SKY, haze*0.80)
    body = mix((219, 236, 251), SKY, haze*0.90)
    lit = mix((255, 255, 255), SKY, haze*0.85)
    sunward = 1.0 if cx < SX else -1.0
    puffs = []
    n = rnd.randint(4, 6)
    for i in range(n):
        u = (i + 0.5)/n
        b = math.sin(math.pi*u)**0.62
        puffs.append((cx + (u - 0.5)*R*2.0 + rnd.uniform(-0.07, 0.07)*R,
                      cy - R*0.55*b - rnd.uniform(0.0, 0.10)*R,
                      R*(0.34 + 0.62*b)*rnd.uniform(0.86, 1.12)))
    for i in range(rnd.randint(1, 2)):
        u = rnd.uniform(0.25, 0.75)
        b = math.sin(math.pi*u)**0.62
        puffs.append((cx + (u - 0.5)*R*1.7,
                      cy - R*1.05*b*rnd.uniform(0.72, 1.00),
                      R*(0.24 + 0.34*b)*rnd.uniform(0.82, 1.10)))
    ell(hx(under), cx, cy + R*0.10, R*1.32, R*0.30, clouds)
    for px, py, pr in puffs:
        ell(hx(under), px, py + R*0.17, pr*1.05, pr*1.05, clouds)
    ell(hx(body), cx, cy - R*0.02, R*1.28, R*0.26, clouds)
    for px, py, pr in puffs:
        ell(hx(body), px, py, pr, pr, clouds)
    for px, py, pr in puffs:
        ell(hx(lit), px + sunward*pr*0.14, py - pr*0.22, pr*0.76, pr*0.76, clouds)

PIG = (0.428, 0.372, 0.066, 1.30)

def flying_pig():
    fx, fy, sc, phase = PIG
    R = H*sc
    cx = W*fx + math.sin(TAU*T + phase)*W*0.0090
    cy = H*fy + math.sin(TAU*2*T + phase*1.7)*H*0.0100
    flap = math.sin(TAU*3*T)
    skin, shade, lit = "#ffb3d1", "#ef8ab4", "#ffdaea"
    snout, hoof = "#f37ba9", "#e2749f"
    wing, wingshade = "#fff4f9", "#ffcfe4"

    rell(wingshade, cx - R*0.34, cy - R*0.46 + flap*R*0.10,
         R*0.58, R*0.19, -16 - flap*20, clouds)
    for k, (ox, oy, rr) in enumerate([(-1.06, -0.16, 0.15), (-1.22, -0.30, 0.12),
                                      (-1.16, -0.48, 0.09)]):
        ell(shade if k else skin, cx + R*ox, cy + R*oy, R*rr, R*rr, clouds)
    for ox in (-0.52, 0.36):
        ell(hoof, cx + R*ox, cy + R*0.66, R*0.15, R*0.21, clouds)

    ell(shade, cx, cy + R*0.06, R*1.03, R*0.74, clouds)
    ell(skin, cx, cy, R, R*0.70, clouds)
    ell(lit, cx - R*0.16, cy - R*0.28, R*0.62, R*0.28, clouds)
    for ox in (-0.24, 0.62):
        ell(hoof, cx + R*ox, cy + R*0.62, R*0.16, R*0.22, clouds)

    hx_, hy = cx + R*0.88, cy - R*0.14
    poly(shade, [(hx_ - R*0.30, hy - R*0.42), (hx_ - R*0.02, hy - R*0.30),
                 (hx_ - R*0.34, hy - R*0.08)], clouds)
    poly(shade, [(hx_ + R*0.16, hy - R*0.46), (hx_ + R*0.42, hy - R*0.20),
                 (hx_ + R*0.06, hy - R*0.22)], clouds)
    ell(skin, hx_, hy, R*0.56, R*0.52, clouds)
    ell(lit, hx_ - R*0.04, hy - R*0.22, R*0.34, R*0.18, clouds)
    ell("#ff9ec4", hx_ + R*0.04, hy + R*0.26, R*0.17, R*0.12, clouds)
    ell(snout, hx_ + R*0.48, hy + R*0.10, R*0.26, R*0.21, clouds)
    for ox in (-0.07, 0.07):
        ell("#c85f8b", hx_ + R*(0.48 + ox), hy + R*0.10, R*0.05, R*0.07, clouds)
    ell("#3d2331", hx_ + R*0.14, hy - R*0.14, R*0.11, R*0.13, clouds)
    ell("#ffffff", hx_ + R*0.17, hy - R*0.18, R*0.04, R*0.05, clouds)

    rell(wing, cx - R*0.06, cy - R*0.58 - flap*R*0.08,
         R*0.70, R*0.24, -26 - flap*24, clouds)
    rell("#ffffff", cx - R*0.16, cy - R*0.62 - flap*R*0.08,
         R*0.34, R*0.10, -26 - flap*24, clouds)

flying_pig()

pulse = 0.80 + 0.20*math.sin(TAU*T)

def ray(ang, ln, hw, col, a0, segs=6):
    ux, uy = math.cos(ang), math.sin(ang)
    px, py = -uy, ux
    for k in range(segs):
        t0 = k/segs; t1 = (k + 1)/segs
        a = a0*(1.0 - t0)**1.7
        w0 = hw*(1.0 - t0*0.85); w1 = hw*(1.0 - t1*0.85)
        ax, ay = SX + ux*ln*t0, SY + uy*ln*t0
        bx, by = SX + ux*ln*t1, SY + uy*ln*t1
        poly(rgba(col, a), [(ax + px*w0, ay + py*w0), (ax - px*w0, ay - py*w0),
                            (bx - px*w1, by - py*w1), (bx + px*w1, by + py*w1)])

RAYS = 16
for k in range(RAYS):
    q = (k - T)/RAYS
    m = 0.5 + 0.5*math.sin(TAU*3*q + 0.7)
    ray(TAU*q, H*(0.14 + 0.36*m)*pulse, S*(2.2 + 5.4*m),
        (255, 246, 214), 0.055 + 0.080*m)

fx, fy = W*0.5, H*0.5
for t, rad, col, a in [(0.42, 0.020, (255, 232, 170), 0.20), (0.70, 0.034, (255, 214, 150), 0.15),
                       (1.16, 0.026, (255, 208, 130), 0.18), (1.42, 0.052, (190, 226, 255), 0.12),
                       (1.78, 0.016, (255, 250, 210), 0.22)]:
    px = SX + (fx - SX)*t; py = SY + (fy - SY)*t
    ell(rgba(col, a*(0.6 + 0.4*pulse)), px, py, H*rad*1.05, H*rad)

def spike(ang, ln, hwid, col):
    a = math.radians(ang)
    tip = (SX + math.cos(a)*ln, SY + math.sin(a)*ln)
    px, py = math.cos(a + math.pi/2)*hwid, math.sin(a + math.pi/2)*hwid
    poly(col, [tip, (SX + px, SY + py), (SX - px, SY - py)])

for ang in (0, 90, 180, 270):
    spike(ang, H*0.200*pulse, S*3.8, "#ffe9b0")
for ang in (45, 135, 225, 315):
    spike(ang, H*0.082*pulse, S*2.8, "#ffd88a")
for ang in (0, 90, 180, 270):
    spike(ang, H*0.138*pulse, S*1.8, "#fffbee")
ell("#fff2c8", SX, SY, S*9.2*pulse, S*9.2*pulse)
ell("#ffffff", SX, SY, S*5.4*pulse, S*5.4*pulse)

twinkle = 0.86 + 0.14*math.sin(TAU*T + 1.9) + 0.05*math.sin(TAU*3*T + 0.4)

def polar_spike(ang, ln, hwid, col):
    a = math.radians(ang)
    tip = (PX + math.cos(a)*ln, PY + math.sin(a)*ln)
    px, py = math.cos(a + math.pi/2)*hwid, math.sin(a + math.pi/2)*hwid
    poly(col, [tip, (PX + px, PY + py), (PX - px, PY - py)])

for rad, col, a in [(0.086, (140, 186, 255), 0.15), (0.048, (186, 216, 255), 0.20),
                    (0.024, (226, 240, 255), 0.28)]:
    ell(rgba(col, a*twinkle), PX, PY, H*rad, H*rad)
for ang in (0, 90, 180, 270):
    polar_spike(ang, H*0.132*twinkle, S*2.0, "#cfe3ff")
for ang in (45, 135, 225, 315):
    polar_spike(ang, H*0.036*twinkle, S*1.5, "#a9c8f2")
for ang in (0, 90, 180, 270):
    polar_spike(ang, H*0.084*twinkle, S*1.0, "#ffffff")
ell("#e8f2ff", PX, PY, S*4.0*twinkle, S*4.0*twinkle)
ell("#ffffff", PX, PY, S*2.2*twinkle, S*2.2*twinkle)

open("%s/glow_%03d.mvg" % (wd, idx), "w").write(" ".join(out))
open("%s/clouds_%03d.mvg" % (wd, idx), "w").write(" ".join(clouds))
PY

render_frame() {
    local i=$1 t n out halo
    t=$(python3 -c "print(f'{$i/$FRAMES:.6f}')")
    n=$(printf '%03d' "$i")
    out="$w/frame_${n}.png"
    halo=$(python3 -c "import math;print('%.3f'%(0.80+0.20*math.sin(2*math.pi*$t)))")
    python3 "$w/anim.py" "$W" "$H" "$S" "$t" "$w" "$i" \
        "$SUNX" "$SUNY" "$POLARX" "$POLARY"
    convert -size ${W}x${H} xc:black -draw "@$w/glow_${n}.mvg" "$w/glow_${n}.png"
    convert "$w/bg.png" -draw "@$w/clouds_${n}.mvg" \
        \( "$w/sunhalo.png" -evaluate multiply "$halo" \) \
        -compose screen -composite \
        "$w/glow_${n}.png" -compose screen -composite \
        "$w/textlayer.png" -compose over -composite \
        -draw "@$w/frame.mvg" \
        -filter Lanczos -resize 940x320 \
        -ordered-dither o8x8,"$LEVELS" -strip "$out"
    rm -f "$w/glow_${n}.png"
}

echo "rendering $FRAMES frames for $ver ..."
maxjobs=$(nproc 2>/dev/null || echo 4)
for ((i=0; i<FRAMES; i++)); do
    render_frame "$i" &
    while [ "$(jobs -r | wc -l)" -ge "$maxjobs" ]; do wait -n; done
done
wait
frames=()
for ((i=0; i<FRAMES; i++)); do frames+=("$w/frame_$(printf '%03d' "$i").png"); done
echo "rendered ${#frames[@]} frames"

convert "${frames[@]}" -append -colors 256 -unique-colors "$w/pal.gif"
echo "palette: $(identify -format '%w' "$w/pal.gif") colours"
convert -delay "$DELAY" -loop 0 \
    $(for f in "${frames[@]}"; do printf ' ( %q -dither None -remap %q ) ' "$f" "$w/pal.gif"; done) \
    "$w/splash_pre.gif"
if command -v gifsicle >/dev/null 2>&1; then
    gifsicle -O3 --lossy="$LOSSY" --colors 256 "$w/splash_pre.gif" -o "$w/splash.gif"
else
    echo "gifsicle not found — falling back to ImageMagick optimisation" >&2
    convert "$w/splash_pre.gif" -layers optimize "$w/splash.gif"
fi
sz=$(stat -c%s "$w/splash.gif")
echo "assembled splash.gif ${FRAMES}f $(identify -format '%wx%h' "$w/splash.gif[0]") ($sz bytes)"

if [ -n "${OUTGIF:-}" ]; then cp "$w/splash.gif" "$OUTGIF"; fi

header="src/about_splash_gif.h"
python3 - "$w/splash.gif" "$header" <<'PY'
import base64, sys, textwrap
gif, header = sys.argv[1], sys.argv[2]
b64 = base64.b64encode(open(gif, "rb").read()).decode()
lines = textwrap.wrap(b64, 96)
out = ["/* about_splash_gif.h — the about:start release splash animation, embedded.",
       " * Copyright 2026 Andreas Røsdal",
       " * SPDX-License-Identifier: GPL-3.0-or-later",
       " */",
       "#ifndef NS_ABOUT_SPLASH_GIF_H", "#define NS_ABOUT_SPLASH_GIF_H", "",
       "static const char about_splash_gif_b64[] ="]
out += ['    "%s"%s' % (ln, ";" if i == len(lines) - 1 else "")
        for i, ln in enumerate(lines)]
out += ["", "#endif", ""]
open(header, "w", newline="\n").write("\n".join(out))
print("wrote %s (%d b64 chars)" % (header, len(b64)))
PY
