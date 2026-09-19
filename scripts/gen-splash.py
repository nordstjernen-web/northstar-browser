#!/usr/bin/env python3
# gen-splash.py — render the about:start splash animation and embed it as src/about_splash_gif.h.
import base64
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 940, 320
S = 3
FRAMES = int(os.environ.get("NS_SPLASH_FRAMES", "32"))
DELAY_MS = int(os.environ.get("NS_SPLASH_DELAY_MS", "80"))
LOSSY = int(os.environ.get("NS_SPLASH_LOSSY", "60"))
TAGLINE = "YET ANOTHER WEB BROWSER."
BUBBLE = ["TWO OF EACH.", "YES, EVEN BROWSERS."]

INK = (0, 0, 0)
PAPER = (255, 255, 255)
LINE = 2.2
HORIZON_Y = 190.0
GROUND_Y = 262.0
RAMP_FOOT = 560.0
NOAH_X = 584.0
QUEUE_LEN = 570.0


def version():
    text = open(os.path.join(ROOT, "meson.build")).read()
    m = re.search(r"^\s*version:\s*'([^']*)'", text, re.M)
    if not m:
        sys.exit("could not read version from meson.build")
    return m.group(1).split("-")[0]


def find_font(query, *paths):
    if shutil.which("fc-match"):
        f = subprocess.run(["fc-match", "-f", "%{file}", query],
                           capture_output=True, text=True).stdout.strip()
        if f and os.path.isfile(f):
            return f
    for p in paths:
        if os.path.isfile(p):
            return p
    sys.exit("missing font: " + query)


def sx(v):
    return v * S


def densify(pts, spacing=3.0):
    out = [pts[0]]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        d = math.hypot(x1 - x0, y1 - y0)
        n = max(1, int(d / spacing))
        for i in range(1, n + 1):
            u = i / n
            out.append((x0 + (x1 - x0) * u, y0 + (y1 - y0) * u))
    return out


def wobble(pts, seed, amp):
    n = len(pts)
    if n < 3 or amp <= 0:
        return pts
    rng = np.random.default_rng(seed)
    noise = rng.normal(0.0, 1.0, n + 8)
    kernel = np.ones(7) / 7.0
    noise = np.convolve(noise, kernel, mode="same")[4:4 + n] * amp * 1.9
    out = []
    for i, (x, y) in enumerate(pts):
        x0, y0 = pts[max(0, i - 1)]
        x1, y1 = pts[min(n - 1, i + 1)]
        tx, ty = x1 - x0, y1 - y0
        length = math.hypot(tx, ty) or 1.0
        nx, ny = -ty / length, tx / length
        out.append((x + nx * noise[i], y + ny * noise[i]))
    return out


def circle(cx, cy, r, n=40, a0=0.0, a1=2 * math.pi, ry=None):
    ry = r if ry is None else ry
    return [(cx + r * math.cos(a0 + (a1 - a0) * i / n),
             cy + ry * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]


class Ink:
    def __init__(self, fonts):
        self.img = Image.new("RGB", (W * S, H * S), PAPER)
        self.d = ImageDraw.Draw(self.img)
        self.fonts = fonts
        self.seed = 1

    def start(self, name):
        self.seed = zlib.crc32(name.encode()) & 0xffff

    def stroke(self, pts, width=LINE, amp=1.0, closed=False, fill=None):
        if closed:
            pts = list(pts) + [pts[0]]
        pts = densify(pts)
        self.seed += 1
        pts = wobble(pts, self.seed, amp)
        sp = [(sx(x), sx(y)) for x, y in pts]
        if fill is not None:
            self.d.polygon(sp, fill=fill)
        w = max(1, int(round(sx(width))))
        self.d.line(sp, fill=INK, width=w, joint="curve")
        r = w / 2.0
        for x, y in (sp[0], sp[-1]):
            self.d.ellipse([x - r, y - r, x + r, y + r], fill=INK)

    def hatch(self, pts, spacing=5.0, angle=45.0, width=LINE * 0.55):
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        mask = Image.new("L", (W * S, H * S), 0)
        ImageDraw.Draw(mask).polygon([(sx(x), sx(y)) for x, y in pts], fill=255)
        layer = Image.new("L", (W * S, H * S), 0)
        ld = ImageDraw.Draw(layer)
        a = math.radians(angle)
        dx, dy = math.cos(a), math.sin(a)
        nx, ny = -dy, dx
        cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        span = math.hypot(max(xs) - min(xs), max(ys) - min(ys))
        k = -span
        while k <= span:
            px, py = cx + nx * k, cy + ny * k
            ld.line([(sx(px - dx * span), sx(py - dy * span)), (sx(px + dx * span), sx(py + dy * span))],
                    fill=255, width=max(1, int(round(sx(width)))))
            k += spacing
        layer = Image.composite(layer, Image.new("L", layer.size, 0), mask)
        self.img.paste(Image.new("RGB", layer.size, INK), (0, 0), layer)

    def dot(self, x, y, r=1.6):
        self.d.ellipse([sx(x - r), sx(y - r), sx(x + r), sx(y + r)], fill=INK)

    def text(self, x, y, s, size, bold=True, anchor="la"):
        font = ImageFont.truetype(self.fonts[0] if bold else self.fonts[1], int(sx(size)))
        self.d.text((sx(x), sx(y)), s, font=font, fill=INK, anchor=anchor)

    def text_width(self, s, size, bold=True):
        font = ImageFont.truetype(self.fonts[0] if bold else self.fonts[1], int(sx(size)))
        return self.d.textlength(s, font=font) / S


class Place:
    def __init__(self, ox, oy, scale=1.0, flip=False, rot=0.0):
        self.ox, self.oy, self.scale, self.flip, self.rot = ox, oy, scale, flip, rot

    def __call__(self, pts):
        f = -1.0 if self.flip else 1.0
        c, s = math.cos(self.rot), math.sin(self.rot)
        out = []
        for x, y in pts:
            x, y = f * x * self.scale, y * self.scale
            out.append((self.ox + x * c - y * s, self.oy + x * s + y * c))
        return out

    def p(self, x, y):
        return self(((x, y),))[0]

    def lw(self, k=1.0):
        return LINE * k * (0.55 + 0.45 * self.scale)


def legs(ink, t, xs, top, phase, swing=4.0):
    for i, x in enumerate(xs):
        s = swing * math.sin(2 * math.pi * phase * 2 + (0 if i % 2 == 0 else math.pi))
        ink.stroke(t([(x, top), (x + s * 0.5, top * 0.5), (x + s, 0)]), width=t.lw())


def body(ink, t, cx, cy, rx, ry, rot=0.0):
    pts = circle(0, 0, rx, 44, ry=ry)
    c, s = math.cos(rot), math.sin(rot)
    pts = [(cx + x * c - y * s, cy + x * s + y * c) for x, y in pts]
    ink.stroke(t(pts), fill=PAPER, width=t.lw())


def head(ink, t, cx, cy, r, eye=True):
    ink.stroke(t(circle(cx, cy, r, 32)), fill=PAPER, width=t.lw())
    if eye:
        ex, ey = t.p(cx + r * 0.35, cy - r * 0.25)
        ink.dot(ex, ey, 1.3 * t.scale)


def gait(phase, k=2):
    return math.sin(2 * math.pi * phase * k)


def elephant(ink, t, phase):
    bob = 1.0 * gait(phase)
    legs(ink, t, [12, 22, 44, 54], -28, phase, 3.0)
    body(ink, t, 33, -46 + bob, 33, 20)
    flap = 3.0 * gait(phase, 1)
    ink.stroke(t(circle(46, -50 + bob, 10 + flap * 0.4, 24, ry=12)), fill=PAPER, width=t.lw())
    ink.stroke(t(circle(52, -52 + bob, 13, 28, math.radians(120), math.radians(400))), fill=PAPER, width=t.lw())
    swing = 4.0 * gait(phase, 1)
    ink.stroke(t([(61, -46 + bob), (70, -36 + bob), (72 + swing, -22 + bob), (67 + swing, -8 + bob),
                  (70 + swing * 1.5, -2)]), width=t.lw(1.2))
    ink.dot(*t.p(58, -55 + bob), 1.4 * t.scale)
    ink.stroke(t([(63, -40 + bob), (74, -37 + bob)]), width=t.lw())
    ink.stroke(t([(0, -50 + bob), (-8, -40 + bob), (-6 + swing * 0.5, -30 + bob)]), width=t.lw())


def giraffe(ink, t, phase, deck=False):
    legs(ink, t, [8, 14, 26, 32], -40, phase, 3.5 if not deck else 0.0)
    body(ink, t, 20, -50, 20, 11)
    sway = 3.0 * gait(phase, 1)
    ink.stroke(t([(34, -56), (46 + sway, -84), (52 + sway * 1.5, -116)]), width=t.lw(1.6))
    head(ink, t, 56 + sway * 1.5, -122, 7)
    ink.stroke(t([(52 + sway * 1.5, -128), (50 + sway * 1.5, -136)]), width=t.lw())
    ink.stroke(t([(58 + sway * 1.5, -128), (60 + sway * 1.5, -136)]), width=t.lw())
    ink.dot(*t.p(50 + sway * 1.5, -136), 1.6 * t.scale)
    ink.dot(*t.p(60 + sway * 1.5, -136), 1.6 * t.scale)
    for x, y in ((12, -52), (24, -46), (18, -56), (30, -54), (42 + sway * 0.6, -92), (47 + sway * 0.9, -106)):
        ink.stroke(t(circle(x, y, 2.6, 10)), amp=0.3, width=t.lw(0.8))


def lion(ink, t, phase):
    legs(ink, t, [8, 16, 30, 38], -24, phase)
    body(ink, t, 24, -34, 24, 12)
    wag = 3.0 * gait(phase, 1)
    ink.stroke(t([(0, -36), (-10, -46 + wag), (-14, -38 + wag)]), width=t.lw())
    ink.dot(*t.p(-15, -37 + wag), 2.2 * t.scale)
    for k in range(14):
        a = 2 * math.pi * k / 14
        ink.stroke(t([(46 + 13 * math.cos(a), -46 + 13 * math.sin(a)),
                      (46 + 19 * math.cos(a + 0.2), -46 + 19 * math.sin(a + 0.2))]), amp=0.4, width=t.lw(0.9))
    head(ink, t, 46, -46, 11)
    ink.stroke(t([(52, -44), (55, -41), (58, -44)]), amp=0.3, width=t.lw(0.8))


def zebra(ink, t, phase, stripes=True):
    nod = 1.5 * gait(phase)
    legs(ink, t, [8, 14, 30, 36], -26, phase)
    body(ink, t, 22, -36, 22, 11)
    ink.stroke(t([(38, -42), (48, -58 + nod), (54, -66 + nod)]), width=t.lw(1.5))
    ink.stroke(t(circle(56, -68 + nod, 7, 24, ry=5)), fill=PAPER, width=t.lw())
    ink.dot(*t.p(58, -70 + nod), 1.3 * t.scale)
    ink.stroke(t([(54, -74 + nod), (56, -80 + nod)]), width=t.lw())
    ink.stroke(t([(0, -36), (-8, -28)]), width=t.lw())
    if stripes:
        for x in (10, 17, 24, 31):
            ink.stroke(t([(x, -46), (x - 2, -28)]), amp=0.4, width=t.lw(0.9))
        for x in (44, 48):
            ink.stroke(t([(x, -54 + nod), (x + 3, -50 + nod)]), amp=0.3, width=t.lw(0.8))
    else:
        ink.stroke(t([(40, -46), (52, -66 + nod)]), amp=0.3, width=t.lw(0.8))
        ink.stroke(t([(0, -36), (-7, -26), (-3, -20)]), amp=0.6, width=t.lw(0.9))


def camel(ink, t, phase):
    nod = 2.0 * gait(phase)
    legs(ink, t, [8, 14, 32, 40], -30, phase, 3.5)
    body(ink, t, 24, -40, 24, 11)
    ink.stroke(t([(8, -48), (16, -60), (24, -50), (32, -60), (40, -48)]), fill=PAPER, width=t.lw())
    ink.stroke(t([(44, -44), (52, -60 + nod), (54, -74 + nod)]), width=t.lw(1.4))
    ink.stroke(t(circle(56, -76 + nod, 6, 20, ry=4.5)), fill=PAPER, width=t.lw())
    ink.dot(*t.p(57, -78 + nod), 1.2 * t.scale)
    ink.stroke(t([(0, -40), (-6, -30)]), width=t.lw())


def cow(ink, t, phase):
    legs(ink, t, [8, 14, 30, 36], -22, phase)
    body(ink, t, 22, -34, 23, 13)
    nod = 1.5 * gait(phase)
    head(ink, t, 46, -40 + nod, 8)
    ink.stroke(t([(42, -48 + nod), (40, -55 + nod)]), width=t.lw())
    ink.stroke(t([(50, -48 + nod), (52, -55 + nod)]), width=t.lw())
    ink.stroke(t(circle(24, -26, 5, 16, ry=3.5)), amp=0.3, width=t.lw(0.8))
    ink.stroke(t(circle(14, -36, 5, 12, ry=4)), amp=0.4, fill=INK, width=t.lw(0.8))
    ink.stroke(t([(0, -36), (-6, -22)]), width=t.lw())


def bear(ink, t, phase):
    legs(ink, t, [10, 16, 28, 34], -24, phase, 3.0)
    body(ink, t, 22, -36, 22, 15)
    head(ink, t, 44, -44, 10)
    ink.stroke(t(circle(38, -53, 3, 10)), width=t.lw(0.8))
    ink.stroke(t(circle(48, -53, 3, 10)), width=t.lw(0.8))
    ink.dot(*t.p(53, -42), 2.0 * t.scale)


def kangaroo(ink, t, phase):
    hop = abs(math.sin(2 * math.pi * phase * 2)) * 7.0
    tk = Place(t.ox, t.oy - hop * t.scale, t.scale, t.flip, t.rot)
    ink.stroke(tk([(14, -8), (22, -2), (30, 0)]), width=t.lw(1.2))
    ink.stroke(tk([(8, -30), (-4, -20), (-14, -6)]), width=t.lw(1.3))
    body(ink, tk, 18, -30, 13, 18, math.radians(-30))
    ink.stroke(tk([(22, -50), (30, -66)]), width=t.lw(1.2))
    head(ink, tk, 34, -70, 6)
    ink.stroke(tk([(31, -76), (30, -84)]), width=t.lw())
    ink.stroke(tk([(37, -76), (39, -84)]), width=t.lw())
    ink.stroke(tk([(26, -36), (34, -32)]), width=t.lw())
    ink.stroke(tk([(12, -26), (16, -16), (18, -6)]), amp=0.5, width=t.lw())


def pig(ink, t, phase):
    legs(ink, t, [8, 12, 24, 28], -12, phase, 2.0)
    body(ink, t, 18, -22, 18, 11)
    head(ink, t, 34, -26, 8)
    ink.stroke(t(circle(41, -25, 3, 12, ry=2.5)), fill=PAPER, width=t.lw(0.8))
    ink.stroke(t([(30, -33), (28, -39), (34, -36)]), amp=0.3, width=t.lw(0.8))
    curl = 1.0 * gait(phase, 1)
    ink.stroke(t([(0, -24), (-4, -28 + curl), (-2, -32 + curl), (-6, -33 + curl)]), amp=0.3, width=t.lw(0.8))


def sheep(ink, t, phase):
    legs(ink, t, [8, 12, 22, 26], -12, phase, 2.0)
    pts = [(16 + (15 + 2.5 * math.sin(k * 1.1)) * math.cos(2 * math.pi * k / 22),
            -24 + (10 + 2.0 * math.sin(k * 1.3)) * math.sin(2 * math.pi * k / 22)) for k in range(23)]
    ink.stroke(t(pts), fill=PAPER, amp=0.6, width=t.lw())
    ink.stroke(t(circle(31, -28, 5, 16, ry=4)), fill=INK, width=t.lw())
    ink.stroke(t([(28, -32), (26, -36)]), width=t.lw(1.3))


def dog(ink, t, phase):
    legs(ink, t, [6, 10, 22, 26], -14, phase, 3.0)
    body(ink, t, 16, -22, 16, 8)
    head(ink, t, 32, -30, 6.5)
    ink.stroke(t([(28, -35), (24, -28)]), width=t.lw())
    ink.stroke(t([(37, -28), (41, -27)]), width=t.lw())
    wag = 3.0 * math.sin(2 * math.pi * phase * 4)
    ink.stroke(t([(0, -24), (-6 + wag, -34)]), width=t.lw())


def cat(ink, t, phase):
    body(ink, t, 12, -10, 12, 7)
    head(ink, t, 24, -16, 5)
    ink.stroke(t([(20, -20), (19, -25), (23, -21)]), amp=0.2, width=t.lw(0.8))
    ink.stroke(t([(26, -21), (28, -25), (29, -20)]), amp=0.2, width=t.lw(0.8))
    swish = 3.0 * gait(phase, 1)
    ink.stroke(t([(0, -10), (-8, -16 + swish), (-6, -24 + swish)]), amp=0.5, width=t.lw(0.9))


def turtle(ink, t, phase):
    crawl = 1.5 * gait(phase)
    ink.stroke(t(circle(14, -6, 14, 24, math.pi, 2 * math.pi, ry=11)), fill=PAPER, width=t.lw())
    ink.stroke(t([(0, -6), (28, -6)]), width=t.lw())
    for x in (6, 14, 22):
        ink.stroke(t([(x, -6), (x + 2, -14)]), amp=0.3, width=t.lw(0.8))
    ink.stroke(t([(4, -6), (2 - crawl, 0)]), width=t.lw())
    ink.stroke(t([(24, -6), (26 + crawl, 0)]), width=t.lw())
    head(ink, t, 32 + crawl * 0.5, -9, 4)


def duck(ink, t, phase):
    waddle = 2.0 * math.sin(2 * math.pi * phase * 4)
    body(ink, t, 8, -14, 9, 6)
    ink.stroke(t([(14, -18), (16, -26)]), width=t.lw(1.2))
    head(ink, t, 17, -29, 4)
    ink.stroke(t([(20, -29), (26, -28 + waddle * 0.3), (20, -27)]), amp=0.3, width=t.lw(0.8))


def penguin(ink, t, phase):
    waddle = 1.5 * math.sin(2 * math.pi * phase * 4)
    body(ink, t, 8, -14, 6, 13)
    ink.stroke(t(circle(8, -12, 3.5, 20, ry=8)), amp=0.3, width=t.lw(0.8))
    ink.stroke(t([(2, -18), (-2 + waddle, -8)]), width=t.lw())
    ink.stroke(t([(11, -26), (16, -25), (11, -24)]), amp=0.2, width=t.lw(0.8))
    ink.dot(*t.p(9, -28), 1.2 * t.scale)
    ink.stroke(t([(5, -1), (1, 0)]), width=t.lw())
    ink.stroke(t([(10, -1), (14, 0)]), width=t.lw())


def rabbit(ink, t, phase):
    hop = abs(math.sin(2 * math.pi * phase * 2 + 1.0)) * 6.0
    tr = Place(t.ox, t.oy - hop * t.scale, t.scale, t.flip, t.rot)
    body(ink, tr, 10, -9, 10, 7)
    head(ink, tr, 20, -15, 5)
    ink.stroke(tr(circle(17, -25, 2, 14, ry=6)), amp=0.3, width=t.lw(0.8))
    ink.stroke(tr(circle(22, -26, 2, 14, ry=6)), amp=0.3, width=t.lw(0.8))
    ink.stroke(tr(circle(0, -8, 2.5, 10)), amp=0.3, width=t.lw(0.8))
    ink.stroke(tr([(6, -3), (2, 0)]), width=t.lw())
    ink.stroke(tr([(14, -3), (17, 0)]), width=t.lw())


def mouse(ink, t, phase):
    body(ink, t, 7, -5, 7, 4.5)
    head(ink, t, 14, -6, 3, eye=False)
    ink.dot(*t.p(15, -7), 0.9 * t.scale)
    ink.stroke(t(circle(12, -10, 2, 10)), amp=0.2, width=t.lw(0.8))
    flick = 2.0 * gait(phase, 1)
    ink.stroke(t([(0, -4), (-6, -2 + flick), (-12, -6 + flick)]), amp=0.4, width=t.lw(0.8))


def snake(ink, t, phase):
    pts = [(x, -3 - 3.5 * math.sin(x / 5.0 + 2 * math.pi * phase * 2)) for x in range(0, 41, 2)]
    ink.stroke(t(pts), width=t.lw(1.3))
    ink.stroke(t([(40, pts[-1][1]), (47, pts[-1][1] - 1), (40, pts[-1][1] + 2)]), amp=0.2, width=t.lw())
    ink.stroke(t([(47, pts[-1][1] - 1), (52, pts[-1][1] - 2)]), width=t.lw(0.7))


def monkey(ink, t, phase):
    body(ink, t, 8, -12, 7, 8)
    head(ink, t, 12, -24, 5)
    ink.stroke(t(circle(7, -24, 2, 10)), amp=0.2, width=t.lw(0.8))
    wave = 4.0 * gait(phase, 1)
    ink.stroke(t([(2, -10), (-8, -16 - wave), (-12, -8 - wave * 2)]), width=t.lw())
    ink.stroke(t([(14, -10), (22, -4)]), width=t.lw())
    ink.stroke(t([(0, -8), (-10, -2), (-12, 6)]), amp=0.6, width=t.lw())


def dove(ink, t, phase):
    flap = 6.0 * math.sin(2 * math.pi * phase * 4)
    body(ink, t, 0, 0, 7, 3.5)
    ink.stroke(t([(-2, -2), (-4, -10 - flap), (6, -12 - flap)]), amp=0.5, width=t.lw())
    head(ink, t, 8, -2, 2.5, eye=False)
    ink.stroke(t([(10, -2), (14, -1)]), width=t.lw(0.8))
    ink.stroke(t([(-7, 0), (-13, 2), (-12, -3)]), amp=0.3, width=t.lw(0.8))


def whale(ink, t, phase):
    rise = 0.5 - 0.5 * math.cos(2 * math.pi * phase)
    tw = Place(t.ox, t.oy + 10.0 * (1.0 - rise) * t.scale, t.scale, t.flip, t.rot)
    ink.stroke(tw([(0, 0), (10, -6), (24, -8), (40, -4), (48, 0)]), width=t.lw(1.2), fill=PAPER)
    ink.stroke(tw([(48, 0), (54, -8 - 4 * rise), (60, -2)]), amp=0.4, width=t.lw())
    if rise > 0.35:
        spout = 4.0 + 8.0 * rise
        ink.stroke(tw([(18, -8), (14, -8 - spout)]), amp=0.4, width=t.lw(0.8))
        ink.stroke(tw([(18, -8), (22, -8 - spout)]), amp=0.4, width=t.lw(0.8))
    ink.dot(*tw.p(28, -5), 1.0 * t.scale)


def stick_person(ink, t, phase, woman=False, beard=False, staff=False, wave=False, umbrella=False):
    head(ink, t, 0, -46, 8, eye=False)
    ink.stroke(t([(0, -38), (0, -14)]), width=t.lw(1.2))
    ink.stroke(t([(0, -14), (-8, 0)]), width=t.lw(1.2))
    ink.stroke(t([(0, -14), (8, 0)]), width=t.lw(1.2))
    gust = 2.0 * gait(phase, 1)
    if beard:
        for x in (-5, -2, 1, 4):
            ink.stroke(t([(x, -39), (x + 0.5 + gust * 0.3, -33)]), amp=0.3, width=t.lw(0.8))
    if woman:
        ink.stroke(t([(-7, -50), (-9 - gust, -34)]), amp=0.6, width=t.lw())
        ink.stroke(t([(7, -50), (9 - gust, -34)]), amp=0.6, width=t.lw())
        ink.stroke(t([(-4, -54), (0, -56), (4, -54)]), amp=0.3, width=t.lw(0.8))
    if staff:
        ink.stroke(t([(0, -32), (-14, -28)]), width=t.lw(1.1))
        ink.stroke(t([(-14, -44), (-14, 0)]), width=t.lw(1.1))
    if wave:
        a = 0.5 * math.sin(2 * math.pi * phase * 2)
        ink.stroke(t([(0, -32), (12, -40 + 6 * a), (16, -52 + 6 * a)]), width=t.lw(1.1))
    elif not staff:
        ink.stroke(t([(0, -32), (-10, -20)]), width=t.lw(1.1))
    if umbrella:
        tilt = math.radians(6.0 * gait(phase, 1))
        tu = Place(*t.p(10, -40), t.scale, t.flip, t.rot + tilt)
        ink.stroke(t([(0, -32), (10, -40)]), width=t.lw(1.1))
        ink.stroke(tu([(0, 0), (0, -22)]), width=t.lw(1.1))
        ink.stroke(tu(circle(0, -22, 22, 24, math.pi, 2 * math.pi, ry=12)), fill=PAPER, width=t.lw())
        ink.stroke(tu([(-22, -22), (22, -22)]), width=t.lw())
    else:
        ink.stroke(t([(0, -32), (10, -20)]), width=t.lw(1.1))


def cloud(ink, cx, cy, w, far=False):
    pts = []
    for k in range(48):
        a = 2 * math.pi * k / 48
        r = 1.0 + 0.16 * math.sin(a * 5 + 0.4) + 0.08 * math.sin(a * 9)
        pts.append((cx + w * 0.5 * r * math.cos(a), cy + w * 0.22 * r * math.sin(a)))
    ink.stroke(pts, closed=True, fill=PAPER, amp=0.5, width=LINE * (0.7 if far else 1.0))


def lightning(ink, phase):
    if not (0.47 <= phase < 0.53):
        return
    ink.stroke([(704, 44), (690, 78), (708, 74), (692, 112)], amp=0.2, width=LINE * 1.4)
    ink.stroke([(700, 78), (696, 88)], amp=0.2, width=LINE * 0.8)


def rain(ink, phase):
    rng = np.random.default_rng(11)
    wind = 2.0 + 1.5 * gait(phase, 1)
    for _ in range(110):
        depth = rng.uniform(0.25, 1.0)
        x = rng.uniform(330, 940)
        y0 = rng.uniform(0, 200)
        y = (y0 + phase * 70.0 * (0.4 + 0.6 * depth)) % 205.0
        if y < 16 or y > 200 - 60 * (1 - depth):
            continue
        length = 4.0 + 8.0 * depth
        ink.stroke([(x, y), (x - wind * depth, y + length)], width=LINE * (0.35 + 0.35 * depth), amp=0.0)


def hills(ink):
    ink.stroke([(0, HORIZON_Y), (940, HORIZON_Y)], width=LINE * 0.6, amp=0.3)
    for cx, w, h in ((90, 260, 26), (300, 320, 34), (480, 220, 18)):
        pts = [(cx - w / 2 + w * k / 30, HORIZON_Y - h * math.sin(math.pi * k / 30) ** 1.4) for k in range(31)]
        ink.stroke(pts, fill=PAPER, width=LINE * 0.6, amp=0.4)


def road_point(d):
    u = min(1.0, max(0.0, d / QUEUE_LEN))
    x = RAMP_FOOT - d * 0.99
    y = GROUND_Y - (GROUND_Y - HORIZON_Y - 4) * u ** 1.12
    scale = 1.0 - 0.56 * u
    return x, y, scale


def road(ink):
    near = [road_point(d) for d in np.arange(-6, QUEUE_LEN + 60, 6)]
    edge = [(x, y + 1.5 * math.sin(x / 31.0)) for x, y, sc in near]
    ink.stroke(edge, width=LINE * 1.0, amp=0.5)
    far = [(x + 6 * sc, y - 14 * sc) for x, y, sc in near]
    ink.stroke(far, width=LINE * 0.7, amp=0.5)
    rng = np.random.default_rng(5)
    for _ in range(46):
        d = rng.uniform(0, QUEUE_LEN + 40)
        x, y, sc = road_point(d)
        side = rng.uniform(-18, 8) * sc
        ink.stroke([(x, y + side), (x + rng.uniform(-1.5, 1.5), y + side - rng.uniform(4, 8) * sc)],
                   width=LINE * 0.6 * sc, amp=0.2)
    shore = [(RAMP_FOOT + 8, GROUND_Y + 2), (600, 268), (640, 276), (700, 282)]
    ink.stroke(shore, width=LINE * 0.9, amp=0.6)


def waves(ink, phase):
    rows = ((200, 1.0, 0.45), (210, 1.5, 0.55), (224, 2.0, 0.65), (242, 2.6, 0.8), (266, 3.2, 0.9),
            (292, 3.8, 1.0), (316, 4.0, 1.0))
    for i, (y, amp, wt) in enumerate(rows):
        x0 = 600 + max(0, 40 - i * 8) if y < 260 else 600
        pts = [(x, y + amp * math.sin(x / (7.0 + i * 1.2) + 2 * math.pi * phase * (1 if i % 2 else -1) + i))
               for x in range(int(x0), W + 12, 3)]
        ink.stroke(pts, width=LINE * 0.8 * wt, amp=0.2)


def ark(ink, phase):
    rock = math.radians(0.9 * gait(phase, 1))
    heave = 1.5 * math.sin(2 * math.pi * phase + 0.8)
    t = Place(790, 250 + heave, 1.0, False, rock)
    t.ox -= 790
    t.oy -= 250
    hull = [(646, 218), (662, 258), (708, 294), (878, 294), (924, 258), (936, 218)]
    ink.stroke(t(hull), width=LINE * 1.3, fill=PAPER)
    ink.hatch(t([(662, 258), (708, 294), (878, 294), (924, 258), (930, 246), (656, 246)]), spacing=6.0, angle=-35)
    ink.stroke(t([(640, 218), (664, 206), (924, 206), (942, 218)]), width=LINE * 0.9, fill=PAPER, closed=True)
    ink.stroke(t([(640, 218), (942, 218)]), width=LINE * 1.3)
    for y in (234, 250):
        ink.stroke(t([(656 + (y - 234) * 0.8, y), (928 - (y - 234) * 0.5, y)]), width=LINE * 0.8, amp=0.6)
    ink.stroke(t([(706, 218), (706, 150), (866, 150), (866, 218)]), fill=PAPER)
    ink.stroke(t([(866, 150), (890, 140), (890, 208), (866, 218)]), fill=PAPER, width=LINE * 1.0)
    ink.hatch(t([(866, 150), (890, 140), (890, 208), (866, 218)]), spacing=5.0, angle=60, width=LINE * 0.45)
    ink.stroke(t([(694, 152), (786, 116), (880, 152)]), width=LINE * 1.2, fill=PAPER)
    ink.stroke(t([(786, 116), (812, 106), (904, 142), (880, 152)]), width=LINE * 1.0, fill=PAPER)
    ink.hatch(t([(786, 116), (812, 106), (904, 142), (880, 152)]), spacing=6.0, angle=20, width=LINE * 0.45)
    ink.stroke(t([(696, 152), (878, 152)]), width=LINE * 0.9)
    ink.stroke(t([(748, 218), (748, 178), (770, 178), (770, 218)]))
    ink.dot(*t.p(766, 196), 1.4)
    for x in (722, 806, 840):
        ink.stroke(t(circle(x, 174, 8, 20)), fill=PAPER)
        ink.stroke(t([(x - 8, 174), (x + 8, 174)]), width=LINE * 0.8, amp=0.2)
        ink.stroke(t([(x, 166), (x, 182)]), width=LINE * 0.8, amp=0.2)
    ink.stroke(t([(RAMP_FOOT - 2, GROUND_Y - 2), (648, 216)]), width=LINE * 1.2)
    ink.stroke(t([(RAMP_FOOT + 4, GROUND_Y + 2), (654, 220)]), width=LINE * 1.2)
    for i in range(1, 8):
        u = i / 8.0
        ink.stroke(t([(RAMP_FOOT - 2 + 90 * u, GROUND_Y - 2 - 46 * u), (RAMP_FOOT + 4 + 90 * u, GROUND_Y + 2 - 46 * u)]),
                   width=LINE * 0.8, amp=0.3)
    return t


def speech_bubble(ink, x, y, lines, size, tail_to):
    widths = [ink.text_width(s, size) for s in lines]
    w = max(widths) + 26
    h = len(lines) * (size + 6) + 14
    pts = circle(x + w / 2, y + h / 2, w / 2, 48, ry=h / 2)
    ink.stroke(pts, fill=PAPER, amp=0.8)
    bx, by = x + w * 0.72, y + h - 2
    ink.stroke([(bx - 8, by), (tail_to[0], tail_to[1]), (bx + 10, by - 3)], fill=PAPER, amp=0.4)
    for i, s in enumerate(lines):
        ink.text(x + w / 2, y + 12 + i * (size + 6), s, size, anchor="ma")


def title(ink, ver):
    ink.text(30, 18, "NORTHSTAR " + ver, 52)
    ink.text(32, 84, "NORTHSTAR WEB BROWSER", 21)
    ink.text(32, 114, TAGLINE, 17)


PROCESSION = [
    (dog, 34), (sheep, 36), (pig, 42), (elephant, 72), (lion, 52), (cow, 52), (zebra, 50),
    (kangaroo, 40), (bear, 50), (camel, 56), (cat, 30), (turtle, 36), (duck, 24), (rabbit, 22),
    (mouse, 16), (penguin, 20),
]


def procession(ink, phase):
    slots = []
    d = 30.0
    for k, (animal, width) in enumerate(PROCESSION):
        x, y, sc = road_point(d)
        slots.append((k, animal, d, x, y, sc))
        d += (width + 16) * sc
    for k, animal, d, x, y, sc in reversed(slots):
        ink.start("pair-%d-back" % k)
        animal(ink, Place(x + 14 * sc, y - 12 * sc, sc * 0.92), phase + 0.37)
        ink.start("pair-%d-front" % k)
        animal(ink, Place(x, y, sc), phase)
        if animal is elephant:
            ink.start("monkeys")
            monkey(ink, Place(x + 26 * sc, y - 64 * sc, sc * 0.8), phase)
            monkey(ink, Place(x + 46 * sc, y - 64 * sc, sc * 0.8, True), phase + 0.5)


def render_frame(i, fonts, ver):
    phase = i / float(FRAMES)
    ink = Ink(fonts)
    ink.start("hills")
    hills(ink)
    ink.start("clouds")
    cloud(ink, 430, 30, 150, far=True)
    cloud(ink, 560, 40, 200)
    cloud(ink, 720, 24, 160)
    cloud(ink, 870, 48, 180)
    ink.start("lightning")
    lightning(ink, phase)
    ink.start("rain")
    rain(ink, phase)
    ink.start("waves")
    waves(ink, phase)
    ink.start("road")
    road(ink)
    ink.start("whale")
    whale(ink, Place(652, 314, 0.55), phase)
    ink.start("ark")
    t = ark(ink, phase)
    ink.start("deck")
    giraffe(ink, Place(*t.p(776, 216), 0.62, False, t.rot), phase, deck=True)
    giraffe(ink, Place(*t.p(812, 216), 0.62, True, t.rot), phase + 0.5, deck=True)
    zebra(ink, Place(*t.p(900, 216), 0.42, False, t.rot), phase, stripes=False)
    penguin(ink, Place(*t.p(924, 216), 0.55, False, t.rot), phase)
    penguin(ink, Place(*t.p(936, 216), 0.55, True, t.rot), phase + 0.5)
    snake(ink, Place(*t.p(806, 148), 0.55, False, t.rot), phase)
    snake(ink, Place(*t.p(752, 152), 0.5, True, t.rot), phase + 0.3)
    cat(ink, Place(*t.p(842, 128), 0.6, False, t.rot), phase)
    cat(ink, Place(*t.p(872, 124), 0.55, True, t.rot), phase + 0.4)
    mouse(ink, Place(*t.p(792, 112), 0.7, False, t.rot), phase)
    mouse(ink, Place(*t.p(812, 108), 0.7, True, t.rot), phase)
    rabbit(ink, Place(*t.p(664, 218), 0.65, False, t.rot), phase)
    rabbit(ink, Place(*t.p(682, 218), 0.65, True, t.rot), phase + 0.5)
    turtle(ink, Place(*t.p(606, 240), 0.7, False, t.rot), phase)
    turtle(ink, Place(*t.p(626, 230), 0.6, False, t.rot), phase + 0.5)
    stick_person(ink, Place(*t.p(728, 218), 0.72, False, t.rot), phase, woman=True, umbrella=True)
    ink.start("ducks")
    duck(ink, Place(600, 290 + 1.5 * gait(phase, 1), 0.9), phase)
    duck(ink, Place(622, 296 - 1.5 * gait(phase, 1), 0.8), phase + 0.5)
    procession(ink, phase)
    ink.start("noah")
    stick_person(ink, Place(NOAH_X, GROUND_Y, 0.85), phase, beard=True, staff=True, wave=True)
    ink.start("doves")
    for k, (cx, cy, rx, ry, sc) in enumerate(((560, 96, 90, 22, 1.0), (610, 78, 70, 18, 0.8))):
        a = 2 * math.pi * (phase + 0.3 * k)
        flip = math.sin(a) < 0
        dove(ink, Place(cx + rx * math.cos(a), cy + ry * math.sin(2 * a), sc, flip), phase + 0.2 * k)
    ink.start("bubble")
    speech_bubble(ink, 380, 126, BUBBLE, 15, (NOAH_X - 4, GROUND_Y - 50))
    ink.start("title")
    title(ink, ver)
    return ink.img.resize((W, H), Image.LANCZOS)


def assemble(frames, out_gif):
    sheet = Image.new("RGB", (W, H * len(frames)))
    for i, f in enumerate(frames):
        sheet.paste(f, (0, i * H))
    palette = sheet.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    quantized = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
    quantized[0].save(out_gif, save_all=True, append_images=quantized[1:], loop=0,
                      duration=DELAY_MS, disposal=1, optimize=False)
    if shutil.which("gifsicle"):
        subprocess.run(["gifsicle", "-O3", "--lossy=%d" % LOSSY, "--colors", "256",
                        out_gif, "-o", out_gif], check=True)
    return os.path.getsize(out_gif)


def write_header(gif, header):
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
    open(header, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print("wrote %s (%d b64 chars)" % (header, len(b64)))


def main():
    ver = version()
    fonts = (find_font("Comic Neue:bold",
                       "/usr/share/fonts/opentype/comic-neue/ComicNeue-Bold.otf",
                       "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
                       "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
             find_font("Comic Neue",
                       "/usr/share/fonts/opentype/comic-neue/ComicNeue-Regular.otf",
                       "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                       "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"))
    print("rendering %d frames for %s ..." % (FRAMES, ver))
    frames = [render_frame(i, fonts, ver) for i in range(FRAMES)]
    work = os.environ.get("NS_SPLASH_WORKDIR") or tempfile.mkdtemp()
    os.makedirs(work, exist_ok=True)
    gif = os.path.join(work, "splash.gif")
    size = assemble(frames, gif)
    print("assembled splash.gif %df %dx%d (%d bytes)" % (FRAMES, W, H, size))
    if os.environ.get("OUTGIF"):
        shutil.copy(gif, os.environ["OUTGIF"])
    write_header(gif, os.path.join(ROOT, "src", "about_splash_gif.h"))


if __name__ == "__main__":
    main()
