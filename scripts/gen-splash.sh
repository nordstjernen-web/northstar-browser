#!/bin/sh
# gen-splash.sh — regenerate src/about_splash_png.h from the Nordstjernen splash animation.
set -eu

SRC="${1:-../nordstjernen/src/about_splash_gif.h}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/src/about_splash_png.h"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

case "$SRC" in
  /*) : ;;
  *) SRC="$REPO/$SRC" ;;
esac

if [ "${SRC##*.}" = "h" ]; then
  python3 - "$SRC" <<'EOF'
import re, sys, base64
src = open(sys.argv[1]).read()
b64 = ''.join(re.findall(r'"([A-Za-z0-9+/=]+)"', src))
open('source.img', 'wb').write(base64.b64decode(b64))
EOF
else
  cp "$SRC" source.img
fi

magick 'source.img[0]' -coalesce frame.png

magick frame.png -fx "(b-r)<0.047?1:0" -colorspace gray notwater.png
magick -size 940x320 xc:black -fill white \
  -draw "rectangle 68,234 596,266 rectangle 68,264 520,296" \
  -fill black -draw "rectangle 205,282 236,300" rects.png
magick -size 940x320 xc:white -fill black -draw "rectangle 66,233 604,297" \
  -fill white -draw "rectangle 204,286 237,300" \
  -draw "rectangle 593,246 940,320" keepmask.png
magick frame.png keepmask.png -alpha off -compose CopyOpacity -composite hole.png
magick hole.png -crop '940x100+0+220' +repage band.png
magick band.png -channel RGBA -filter box -resize '47x5!' -filter triangle -resize '940x100!' fA.png
magick band.png -channel RGBA -filter box -resize '12x2!' -filter triangle -resize '940x100!' fB.png
magick band.png -channel RGBA -filter box -resize '3x1!' -filter triangle -resize '940x100!' fC.png
magick fC.png fB.png -compose over -composite f1.png
magick f1.png fA.png -compose over -composite bandfield.png
magick -size 940x320 xc:black \( bandfield.png -alpha off \) -geometry +0+220 -composite field.png

magick frame.png -crop 190x66+660+232 +repage tile.png
magick tile.png -blur 0x10 tile_low.png
magick tile.png tile_low.png -fx "u-v+0.5" tile_hp.png
magick tile_hp.png \( tile_hp.png -flop \) \( tile_hp.png \) +append \
  -crop '541x66+0+0' +repage hp_strip.png
magick -size 940x320 xc:'gray(50%)' hp_strip.png -geometry +66+233 \
  -compose over -composite hp_full.png
magick field.png hp_full.png -fx "u+v-0.5" textured.png
magick keepmask.png -negate -blur 0x1.5 holemask.png
magick frame.png textured.png holemask.png -composite nocaption.png

magick -size 940x320 xc:white -fill black \
  -draw "rectangle 72,51 655,103" \
  -draw "rectangle 75,118 388,158" \
  -fill white \
  -draw "rectangle 290,40 385,52" \
  -draw "rectangle 233,96 292,103" \
  -draw "rectangle 418,98 472,103" \
  -draw "rectangle 233,118 292,122" \
  -draw "rectangle 545,40 600,61" \
  -draw "rectangle 600,86 605,103" \
  -draw "rectangle 642,42 662,58" tkeep.png
magick nocaption.png tkeep.png -alpha off -compose CopyOpacity -composite thole.png
magick thole.png -crop '940x130+0+40' +repage tband.png
magick tband.png -channel RGBA -filter box -resize '47x7!' -filter triangle -resize '940x130!' tfA.png
magick tband.png -channel RGBA -filter box -resize '12x3!' -filter triangle -resize '940x130!' tfB.png
magick tband.png -channel RGBA -filter box -resize '3x1!' -filter triangle -resize '940x130!' tfC.png
magick tfC.png tfB.png -compose over -composite tf1.png
magick tf1.png tfA.png -compose over -composite tbandfield.png
magick -size 940x320 xc:black \( tbandfield.png -alpha off \) -geometry +0+40 -composite tfield.png
magick tkeep.png -negate -blur 0x1.5 tholemask.png
magick nocaption.png tfield.png tholemask.png -composite stripped.png

magick stripped.png -fuzz 22% -fill white -opaque 'srgb(10,40,64)' \
  -fill black +opaque white -colorspace gray navyhits.png
magick -size 940x320 xc:black -fill white \
  -draw "rectangle 264,90 306,118 rectangle 386,118 420,158" tailboxes.png
magick navyhits.png tailboxes.png -compose multiply -composite \
  -morphology Dilate Disk:2 tailmask.png
for pass in 1 2 3; do
  magick stripped.png -statistic median 11x11 tmed.png
  magick stripped.png tmed.png tailmask.png -composite stripped.png
done

HW=$(magick -font DejaVu-Sans -pointsize 56 label:"Northstar" -format "%w" info:)
VX=$((80 + HW + 18))
magick stripped.png \
  \( -size 940x320 xc:none -font DejaVu-Sans -pointsize 56 -fill white \
     -annotate +80+98 "Northstar" -annotate +$VX+98 "1.0.2" \
     -blur 0x2 -channel A -evaluate multiply 1.2 +channel \) \
  -compose over -composite \
  \( -size 940x320 xc:none -font DejaVu-Sans -pointsize 56 \
     -fill 'rgb(14,39,57)' -annotate +80+98 "Northstar" \) \
  -compose over -composite \
  \( -size 940x320 xc:none -font DejaVu-Sans -pointsize 56 \
     -fill 'rgb(196,104,8)' -annotate +$VX+98 "1.0.2" \) \
  -compose over -composite \
  \( -size 940x320 xc:none -font DejaVu-Sans -pointsize 25 -fill white \
     -annotate +80+147 "Northstar Web Browser" \
     -blur 0x2 -channel A -evaluate multiply 1.4 +channel \) \
  -compose over -composite \
  \( -size 940x320 xc:none -font DejaVu-Sans -pointsize 25 \
     -fill 'rgb(7,41,72)' -annotate +80+147 "Northstar Web Browser" \) \
  -compose over -composite versioned.png

magick versioned.png \
  \( -size 940x320 xc:none -font DejaVu-Sans -pointsize 20 -fill white \
     -annotate +78+272 "Open source edition" -blur 0x2.2 \
     -channel A -evaluate multiply 2 +channel \) \
  -compose over -composite \
  \( -size 940x320 xc:none -font DejaVu-Sans -pointsize 20 -fill black \
     -annotate +78+272 "Open source edition" \) \
  -compose over -composite splash.png

magick splash.png -dither FloydSteinberg -colors 256 png8:splash_final.png

python3 - splash_final.png "$OUT" <<'EOF'
import sys, base64
data = base64.b64encode(open(sys.argv[1], 'rb').read()).decode()
lines = [data[i:i+100] for i in range(0, len(data), 100)]
body = '\n'.join('    "%s"' % l for l in lines)
open(sys.argv[2], 'w').write(
"""/* about_splash_png.h — the about:start splash image, embedded.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NS_ABOUT_SPLASH_PNG_H
#define NS_ABOUT_SPLASH_PNG_H

static const char about_splash_png_b64[] =
""" + body + """;

#endif
""")
EOF

echo "wrote $OUT"
