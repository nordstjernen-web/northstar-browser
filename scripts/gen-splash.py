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

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 940, 320
S = 2
FRAMES = int(os.environ.get("NS_SPLASH_FRAMES", "24"))
DELAY_MS = int(os.environ.get("NS_SPLASH_DELAY_MS", "110"))
LOSSY = int(os.environ.get("NS_SPLASH_LOSSY", "60"))
TAGLINE = "YET ANOTHER WEB BROWSER."
BUBBLE = ["TWO OF EACH.", "YES, EVEN BROWSERS."]

INK = (0, 0, 0)
PAPER = (255, 255, 255)
LINE = 2.3
GROUND_Y = 262.0
WATER_X = 610.0
NOAH_X = 582.0
ANIMAL_SCALE = 0.9


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

    def stroke(self, pts, width=LINE, amp=1.0, closed=False, fill=None, seed=None):
        if closed:
            pts = list(pts) + [pts[0]]
        pts = densify(pts)
        seed = self.seed if seed is None else seed
        self.seed += 1
        pts = wobble(pts, seed, amp)
        sp = [(sx(x), sx(y)) for x, y in pts]
        if fill is not None:
            self.d.polygon(sp, fill=fill)
        self.d.line(sp, fill=INK, width=int(sx(width)), joint="curve")
        r = sx(width) / 2.0
        for x, y in (sp[0], sp[-1]):
            self.d.ellipse([x - r, y - r, x + r, y + r], fill=INK)

    def dot(self, x, y, r=1.6):
        self.d.ellipse([sx(x - r), sx(y - r), sx(x + r), sx(y + r)], fill=INK)

    def text(self, x, y, s, size, bold=True, anchor="la"):
        font = ImageFont.truetype(self.fonts[0] if bold else self.fonts[1], int(sx(size)))
        self.d.text((sx(x), sx(y)), s, font=font, fill=INK, anchor=anchor)

    def text_width(self, s, size, bold=True):
        font = ImageFont.truetype(self.fonts[0] if bold else self.fonts[1], int(sx(size)))
        return self.d.textlength(s, font=font) / S


class Place:
    def __init__(self, ox, oy, scale=1.0, flip=False):
        self.ox, self.oy, self.scale, self.flip = ox, oy, scale, flip

    def __call__(self, pts):
        f = -1.0 if self.flip else 1.0
        return [(self.ox + f * x * self.scale, self.oy + y * self.scale) for x, y in pts]

    def p(self, x, y):
        return self(((x, y),))[0]


def legs(ink, t, xs, top, phase, swing=4.0, knee=False):
    for i, x in enumerate(xs):
        s = swing * math.sin(2 * math.pi * phase * 2 + (0 if i % 2 == 0 else math.pi))
        if knee:
            ink.stroke(t([(x, top), (x + s * 0.4, top / 2), (x + s, 0)]), width=LINE * t.scale ** 0.5)
        else:
            ink.stroke(t([(x, top), (x + s, 0)]), width=LINE * t.scale ** 0.5)


def body(ink, t, cx, cy, rx, ry, rot=0.0):
    pts = circle(0, 0, rx, 44, ry=ry)
    c, s = math.cos(rot), math.sin(rot)
    pts = [(cx + x * c - y * s, cy + x * s + y * c) for x, y in pts]
    ink.stroke(t(pts), fill=PAPER, closed=False, width=LINE * t.scale ** 0.5)


def head(ink, t, cx, cy, r, eye=True):
    ink.stroke(t(circle(cx, cy, r, 32)), fill=PAPER, width=LINE * t.scale ** 0.5)
    if eye:
        ex, ey = t.p(cx + r * 0.35, cy - r * 0.25)
        ink.dot(ex, ey, 1.3 * t.scale)


def elephant(ink, t, phase):
    bob = 1.0 * math.sin(2 * math.pi * phase * 2)
    legs(ink, t, [12, 22, 44, 54], -28, phase, 3.0)
    body(ink, t, 33, -46 + bob, 33, 20)
    ink.stroke(t(circle(52, -52 + bob, 13, 28, math.radians(120), math.radians(400))), fill=PAPER)
    ink.stroke(t([(61, -46 + bob), (70, -36 + bob), (72, -22 + bob), (67, -8 + bob),
                  (70 + 2 * bob, -2)]), width=LINE * 1.2)
    ink.stroke(t(circle(46, -50 + bob, 9, 24, ry=11)))
    ink.dot(*t.p(58, -55 + bob), 1.4)
    ink.stroke(t([(63, -40 + bob), (74, -37 + bob)]))
    ink.stroke(t([(0, -50 + bob), (-8, -40 + bob), (-6, -30 + bob)]))


def giraffe(ink, t, phase, deck=False):
    legs(ink, t, [8, 14, 26, 32], -40, phase, 3.5 if not deck else 0.0)
    body(ink, t, 20, -50, 20, 11)
    sway = 2.0 * math.sin(2 * math.pi * phase + 1.0)
    ink.stroke(t([(34, -56), (46 + sway, -84), (52 + sway, -116)]), width=LINE * 1.6)
    head(ink, t, 56 + sway, -122, 7)
    ink.stroke(t([(52 + sway, -128), (50 + sway, -136)]))
    ink.stroke(t([(58 + sway, -128), (60 + sway, -136)]))
    ink.dot(*t.p(50 + sway, -136), 1.6)
    ink.dot(*t.p(60 + sway, -136), 1.6)
    for x, y in ((12, -52), (24, -46), (18, -56), (30, -54), (42 + sway * 0.6, -92), (47 + sway * 0.8, -106)):
        ink.stroke(t(circle(x, y, 2.6, 10)), amp=0.3)


def lion(ink, t, phase):
    legs(ink, t, [8, 16, 30, 38], -24, phase)
    body(ink, t, 24, -34, 24, 12)
    ink.stroke(t([(0, -36), (-10, -46), (-14, -38)]))
    ink.dot(*t.p(-15, -37), 2.2)
    for k in range(14):
        a = 2 * math.pi * k / 14
        ink.stroke(t([(46 + 13 * math.cos(a), -46 + 13 * math.sin(a)),
                      (46 + 19 * math.cos(a + 0.2), -46 + 19 * math.sin(a + 0.2))]), amp=0.4)
    head(ink, t, 46, -46, 11)
    ink.stroke(t([(52, -44), (55, -41), (58, -44)]), amp=0.3)


def zebra(ink, t, phase, stripes=True):
    legs(ink, t, [8, 14, 30, 36], -26, phase)
    body(ink, t, 22, -36, 22, 11)
    ink.stroke(t([(38, -42), (48, -58), (54, -66)]), width=LINE * 1.5)
    ink.stroke(t(circle(56, -68, 7, 24, ry=5)), fill=PAPER)
    ink.dot(*t.p(58, -70), 1.3)
    ink.stroke(t([(54, -74), (56, -80)]))
    ink.stroke(t([(0, -36), (-8, -28)]))
    if stripes:
        for x in (10, 17, 24, 31):
            ink.stroke(t([(x, -46), (x - 2, -28)]), amp=0.4)
        for x in (44, 48):
            ink.stroke(t([(x, -54), (x + 3, -50)]), amp=0.3)
    else:
        ink.stroke(t([(40, -46), (52, -66)]), amp=0.3)
        ink.stroke(t([(0, -36), (-7, -26), (-3, -20)]), amp=0.6)


def camel(ink, t, phase):
    legs(ink, t, [8, 14, 32, 40], -30, phase, 3.5)
    body(ink, t, 24, -40, 24, 11)
    ink.stroke(t([(8, -48), (16, -60), (24, -50), (32, -60), (40, -48)]), fill=PAPER)
    ink.stroke(t([(44, -44), (52, -60), (54, -74)]), width=LINE * 1.4)
    ink.stroke(t(circle(56, -76, 6, 20, ry=4.5)), fill=PAPER)
    ink.dot(*t.p(57, -78), 1.2)
    ink.stroke(t([(0, -40), (-6, -30)]))


def cow(ink, t, phase):
    legs(ink, t, [8, 14, 30, 36], -22, phase)
    body(ink, t, 22, -34, 23, 13)
    head(ink, t, 46, -40, 8)
    ink.stroke(t([(42, -48), (40, -55)]))
    ink.stroke(t([(50, -48), (52, -55)]))
    ink.stroke(t(circle(24, -26, 5, 16, ry=3.5)), amp=0.3)
    ink.stroke(t(circle(14, -36, 5, 12, ry=4)), amp=0.4, fill=INK)
    ink.stroke(t([(0, -36), (-6, -22)]))


def bear(ink, t, phase):
    legs(ink, t, [10, 16, 28, 34], -24, phase, 3.0)
    body(ink, t, 22, -36, 22, 15)
    head(ink, t, 44, -44, 10)
    ink.stroke(t(circle(38, -53, 3, 10)))
    ink.stroke(t(circle(48, -53, 3, 10)))
    ink.dot(*t.p(53, -42), 2.0)


def kangaroo(ink, t, phase):
    hop = abs(math.sin(2 * math.pi * phase * 2)) * 7.0
    tk = Place(t.ox, t.oy - hop * t.scale, t.scale, t.flip)
    ink.stroke(tk([(14, -8), (22, -2), (30, 0)]), width=LINE * 1.2)
    ink.stroke(tk([(8, -30), (-4, -20), (-14, -6)]), width=LINE * 1.3)
    body(ink, tk, 18, -30, 13, 18, math.radians(-30))
    ink.stroke(tk([(22, -50), (30, -66)]), width=LINE * 1.2)
    head(ink, tk, 34, -70, 6)
    ink.stroke(tk([(31, -76), (30, -84)]))
    ink.stroke(tk([(37, -76), (39, -84)]))
    ink.stroke(tk([(26, -36), (34, -32)]))
    ink.stroke(tk([(12, -26), (16, -16), (18, -6)]), amp=0.5)


def pig(ink, t, phase):
    legs(ink, t, [8, 12, 24, 28], -12, phase, 2.0)
    body(ink, t, 18, -22, 18, 11)
    head(ink, t, 34, -26, 8)
    ink.stroke(t(circle(41, -25, 3, 12, ry=2.5)), fill=PAPER)
    ink.stroke(t([(30, -33), (28, -39), (34, -36)]), amp=0.3)
    ink.stroke(t([(0, -24), (-4, -28), (-2, -32), (-6, -33)]), amp=0.3)


def sheep(ink, t, phase):
    legs(ink, t, [8, 12, 22, 26], -12, phase, 2.0)
    pts = [(16 + (15 + 2.5 * math.sin(k * 1.1)) * math.cos(2 * math.pi * k / 22),
            -24 + (10 + 2.0 * math.sin(k * 1.3)) * math.sin(2 * math.pi * k / 22)) for k in range(23)]
    ink.stroke(t(pts), fill=PAPER, amp=0.6)
    ink.stroke(t(circle(31, -28, 5, 16, ry=4)), fill=INK)
    ink.stroke(t([(28, -32), (26, -36)]), width=LINE * 1.3)


def dog(ink, t, phase):
    legs(ink, t, [6, 10, 22, 26], -14, phase, 3.0)
    body(ink, t, 16, -22, 16, 8)
    head(ink, t, 32, -30, 6.5)
    ink.stroke(t([(28, -35), (24, -28)]))
    ink.stroke(t([(37, -28), (41, -27)]))
    wag = 3.0 * math.sin(2 * math.pi * phase * 4)
    ink.stroke(t([(0, -24), (-6 + wag, -34)]))


def turtle(ink, t, phase):
    crawl = 1.5 * math.sin(2 * math.pi * phase * 2)
    ink.stroke(t(circle(14, -6, 14, 24, math.pi, 2 * math.pi, ry=11)), fill=PAPER)
    ink.stroke(t([(0, -6), (28, -6)]))
    for x in (6, 14, 22):
        ink.stroke(t([(x, -6), (x + 2, -14)]), amp=0.3)
    ink.stroke(t([(4, -6), (2 - crawl, 0)]))
    ink.stroke(t([(24, -6), (26 + crawl, 0)]))
    head(ink, t, 32, -9, 4)


def duck(ink, t, phase):
    waddle = 2.0 * math.sin(2 * math.pi * phase * 4)
    ink.stroke(t([(6, -8), (5 + waddle, 0), (2 + waddle, 0)]))
    ink.stroke(t([(10, -8), (10 - waddle, 0), (14 - waddle, 0)]))
    body(ink, t, 8, -14, 9, 6)
    ink.stroke(t([(14, -18), (16, -26)]), width=LINE * 1.2)
    head(ink, t, 17, -29, 4)
    ink.stroke(t([(20, -29), (26, -28), (20, -27)]), amp=0.3)


def penguin(ink, t, phase):
    waddle = 1.5 * math.sin(2 * math.pi * phase * 4)
    body(ink, t, 8, -14, 6, 13)
    ink.stroke(t(circle(8, -12, 3.5, 20, ry=8)), amp=0.3)
    ink.stroke(t([(2, -18), (-2 + waddle, -8)]))
    ink.stroke(t([(11, -26), (16, -25), (11, -24)]), amp=0.2)
    ink.dot(*t.p(9, -28), 1.2)
    ink.stroke(t([(5, -1), (1, 0)]))
    ink.stroke(t([(10, -1), (14, 0)]))


def rabbit(ink, t, phase):
    hop = abs(math.sin(2 * math.pi * phase * 2 + 1.0)) * 6.0
    tr = Place(t.ox, t.oy - hop * t.scale, t.scale, t.flip)
    body(ink, tr, 10, -9, 10, 7)
    head(ink, tr, 20, -15, 5)
    ink.stroke(tr(circle(17, -25, 2, 14, ry=6)), amp=0.3)
    ink.stroke(tr(circle(22, -26, 2, 14, ry=6)), amp=0.3)
    ink.stroke(tr(circle(0, -8, 2.5, 10)), amp=0.3)
    ink.stroke(tr([(6, -3), (2, 0)]))
    ink.stroke(tr([(14, -3), (17, 0)]))


def mouse(ink, t, phase):
    body(ink, t, 7, -5, 7, 4.5)
    head(ink, t, 14, -6, 3, eye=False)
    ink.dot(*t.p(15, -7), 0.9)
    ink.stroke(t(circle(12, -10, 2, 10)), amp=0.2)
    ink.stroke(t([(0, -4), (-6, -2), (-12, -6)]), amp=0.4)


def snake(ink, t, phase):
    pts = [(x, -3 - 3.5 * math.sin(x / 5.0 + 2 * math.pi * phase * 2)) for x in range(0, 41, 2)]
    ink.stroke(t(pts), width=LINE * 1.3)
    ink.stroke(t([(40, pts[-1][1]), (47, pts[-1][1] - 1), (40, pts[-1][1] + 2)]), amp=0.2)
    ink.stroke(t([(47, pts[-1][1] - 1), (52, pts[-1][1] - 2)]), width=LINE * 0.7)


def monkey(ink, t, phase):
    body(ink, t, 8, -12, 7, 8)
    head(ink, t, 12, -24, 5)
    ink.stroke(t(circle(7, -24, 2, 10)), amp=0.2)
    ink.stroke(t([(2, -10), (-8, -16), (-12, -8)]))
    ink.stroke(t([(14, -10), (22, -4)]))
    ink.stroke(t([(0, -8), (-10, -2), (-12, 6)]), amp=0.6)


def dove(ink, t, phase):
    flap = 5.0 * math.sin(2 * math.pi * phase * 3)
    body(ink, t, 0, 0, 7, 3.5)
    ink.stroke(t([(-2, -2), (-4, -10 - flap), (6, -12 - flap)]), amp=0.5)
    head(ink, t, 8, -2, 2.5, eye=False)
    ink.stroke(t([(10, -2), (14, -1)]), width=LINE * 0.8)
    ink.stroke(t([(-7, 0), (-13, 2), (-12, -3)]), amp=0.3)


def whale(ink, t, phase):
    ink.stroke(t([(0, 0), (10, -6), (24, -8), (40, -4), (48, 0)]), width=LINE * 1.2)
    ink.stroke(t([(48, 0), (54, -8), (60, -2)]), amp=0.4)
    spout = 6.0 + 4.0 * math.sin(2 * math.pi * phase)
    ink.stroke(t([(18, -8), (14, -8 - spout)]), amp=0.4)
    ink.stroke(t([(18, -8), (22, -8 - spout)]), amp=0.4)


def stick_person(ink, t, phase, woman=False, beard=False, staff=False, wave=False, umbrella=False):
    head(ink, t, 0, -46, 8, eye=False)
    ink.stroke(t([(0, -38), (0, -14)]), width=LINE * 1.2)
    ink.stroke(t([(0, -14), (-8, 0)]), width=LINE * 1.2)
    ink.stroke(t([(0, -14), (8, 0)]), width=LINE * 1.2)
    if beard:
        for x in (-5, -2, 1, 4):
            ink.stroke(t([(x, -39), (x + 0.5, -33)]), amp=0.3, width=LINE * 0.8)
    if woman:
        ink.stroke(t([(-7, -50), (-9, -34)]), amp=0.6)
        ink.stroke(t([(7, -50), (9, -34)]), amp=0.6)
        ink.stroke(t([(-4, -54), (0, -56), (4, -54)]), amp=0.3)
    if staff:
        ink.stroke(t([(0, -32), (-14, -28)]), width=LINE * 1.1)
        ink.stroke(t([(-14, -44), (-14, 0)]), width=LINE * 1.1)
    if wave:
        a = 0.5 * math.sin(2 * math.pi * phase * 2)
        ink.stroke(t([(0, -32), (12, -40 + 6 * a), (16, -52 + 6 * a)]), width=LINE * 1.1)
    elif not staff:
        ink.stroke(t([(0, -32), (-10, -20)]), width=LINE * 1.1)
    if umbrella:
        ink.stroke(t([(0, -32), (10, -40), (10, -62)]), width=LINE * 1.1)
        ink.stroke(t(circle(10, -62, 22, 24, math.pi, 2 * math.pi, ry=12)), fill=PAPER)
        ink.stroke(t([(-12, -62), (32, -62)]))
    else:
        ink.stroke(t([(0, -32), (10, -20)]), width=LINE * 1.1)


def cloud(ink, cx, cy, w):
    pts = []
    for k in range(48):
        a = 2 * math.pi * k / 48
        r = 1.0 + 0.16 * math.sin(a * 5 + 0.4) + 0.08 * math.sin(a * 9)
        pts.append((cx + w * 0.5 * r * math.cos(a), cy + w * 0.22 * r * math.sin(a)))
    ink.stroke(pts, closed=True, fill=PAPER, amp=0.5)


def rain(ink, phase):
    rng = np.random.default_rng(11)
    for _ in range(90):
        x = rng.uniform(340, 940)
        y0 = rng.uniform(0, 200)
        y = (y0 + phase * 60.0) % 205.0
        if y < 18:
            continue
        ink.stroke([(x, y), (x - 2.0, y + 9)], width=LINE * 0.6, amp=0.0)


def waves(ink, phase):
    for row, y in enumerate((284.0, 298.0, 312.0)):
        pts = [(x, y + 3.0 * math.sin(x / 9.0 + 2 * math.pi * phase + row * 1.3))
               for x in range(int(WATER_X) + row * 14, W + 10, 3)]
        ink.stroke(pts, width=LINE * 0.9, amp=0.2)


def ark(ink, phase):
    rock = 1.5 * math.sin(2 * math.pi * phase)
    t = Place(0, rock)
    hull = [(646, 214), (660, 256), (706, 292), (880, 292), (926, 254), (936, 214)]
    ink.stroke(t(hull), width=LINE * 1.3, fill=PAPER)
    ink.stroke(t([(640, 214), (942, 214)]), width=LINE * 1.3)
    for y in (234, 256, 276):
        ink.stroke(t([(656 + (y - 234) * 0.8, y), (928 - (y - 234) * 0.5, y)]), width=LINE * 0.8, amp=0.6)
    ink.stroke(t([(706, 210), (706, 150), (868, 150), (868, 210)]), fill=PAPER)
    ink.stroke(t([(694, 152), (787, 116), (880, 152)]), width=LINE * 1.2)
    ink.stroke(t([(696, 152), (878, 152)]), width=LINE * 0.9)
    ink.stroke(t([(748, 210), (748, 176), (770, 176), (770, 210)]))
    ink.dot(*t.p(766, 194), 1.4)
    for x in (722, 804, 838):
        ink.stroke(t(circle(x, 172, 8, 20)), fill=PAPER)
        ink.stroke(t([(x - 8, 172), (x + 8, 172)]), width=LINE * 0.8, amp=0.2)
        ink.stroke(t([(x, 164), (x, 180)]), width=LINE * 0.8, amp=0.2)
    ink.stroke(t([(598, GROUND_Y - 2), (648, 212)]), width=LINE * 1.2)
    ink.stroke(t([(604, GROUND_Y + 2), (654, 216)]), width=LINE * 1.2)
    for i in range(1, 7):
        u = i / 7.0
        ink.stroke(t([(598 + 50 * u, GROUND_Y - 2 - 48 * u), (604 + 50 * u, GROUND_Y + 2 - 48 * u)]),
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


def ground(ink):
    pts = [(x, GROUND_Y + 1.5 * math.sin(x / 37.0)) for x in range(-10, int(WATER_X) + 20, 4)]
    ink.stroke(pts, width=LINE * 1.1, amp=0.4)
    rng = np.random.default_rng(5)
    for _ in range(40):
        x = rng.uniform(0, WATER_X - 10)
        ink.stroke([(x, GROUND_Y - 1), (x + rng.uniform(-2, 2), GROUND_Y - rng.uniform(4, 8))],
                   width=LINE * 0.7, amp=0.2)


PROCESSION = [
    (dog, 34), (sheep, 36), (pig, 42), (elephant, 72), (lion, 52), (cow, 52), (zebra, 50),
    (kangaroo, 40), (bear, 50), (camel, 56),
]


def procession(ink, phase):
    x = NOAH_X - 30.0
    for animal, width in PROCESSION:
        w = width * ANIMAL_SCALE
        x -= w
        back = Place(x + 14, GROUND_Y - 6, ANIMAL_SCALE * 0.9)
        animal(ink, back, phase + 0.37)
        front = Place(x, GROUND_Y, ANIMAL_SCALE)
        animal(ink, front, phase)
        if animal is elephant:
            monkey(ink, Place(x + 26 * ANIMAL_SCALE, GROUND_Y - 64 * ANIMAL_SCALE, ANIMAL_SCALE * 0.8), phase)
            monkey(ink, Place(x + 46 * ANIMAL_SCALE, GROUND_Y - 64 * ANIMAL_SCALE, ANIMAL_SCALE * 0.8, True), phase)
        x -= 14.0


def render_frame(i, fonts, ver):
    phase = i / float(FRAMES)
    ink = Ink(fonts)
    cloud(ink, 520, 34, 190)
    cloud(ink, 700, 22, 150)
    cloud(ink, 860, 40, 170)
    rain(ink, phase)
    ground(ink)
    waves(ink, phase)
    t = ark(ink, phase)
    giraffe(ink, Place(776, 210 + t.oy, 0.62), phase, deck=True)
    giraffe(ink, Place(812, 210 + t.oy, 0.62, True), phase + 0.5, deck=True)
    zebra(ink, Place(862, 212 + t.oy, 0.45), phase, stripes=False)
    penguin(ink, Place(900, 212 + t.oy, 0.6), phase)
    penguin(ink, Place(916, 212 + t.oy, 0.6, True), phase + 0.5)
    snake(ink, Place(812, 152 + t.oy, 0.6), phase)
    snake(ink, Place(750, 152 + t.oy, 0.5, True), phase + 0.3)
    mouse(ink, Place(770, 132 + t.oy, 0.7), phase)
    mouse(ink, Place(800, 129 + t.oy, 0.7, True), phase)
    rabbit(ink, Place(880, 212 + t.oy, 0.7), phase)
    rabbit(ink, Place(896, 212 + t.oy, 0.7, True), phase + 0.5)
    turtle(ink, Place(618, GROUND_Y - 18, 0.7), phase)
    turtle(ink, Place(638, GROUND_Y - 36, 0.6), phase + 0.5)
    duck(ink, Place(640, 288, 0.9), phase)
    duck(ink, Place(662, 292, 0.8), phase + 0.5)
    stick_person(ink, Place(728, 210 + t.oy, 0.72), phase, woman=True, umbrella=True)
    procession(ink, phase)
    stick_person(ink, Place(NOAH_X, GROUND_Y, 0.85), phase, beard=True, staff=True, wave=True)
    dove(ink, Place(560 + 30 * math.sin(2 * math.pi * phase), 96 + 6 * math.sin(4 * math.pi * phase), 1.0), phase)
    dove(ink, Place(600 + 30 * math.sin(2 * math.pi * phase + 0.6), 82 + 6 * math.sin(4 * math.pi * phase + 1), 0.85), phase + 0.4)
    whale(ink, Place(660, 302 + 2 * math.sin(2 * math.pi * phase), 0.5), phase)
    speech_bubble(ink, 380, 126, BUBBLE, 15, (NOAH_X - 4, GROUND_Y - 50))
    title(ink, ver)
    frame = ink.img.resize((W, H), Image.LANCZOS)
    return frame


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
