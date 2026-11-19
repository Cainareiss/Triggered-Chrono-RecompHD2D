#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 ct-recomp contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""port: the PS5 tile icon for CT5 (an original design: a clock face
inside a swirling time gate, in the console's black, white and blue; no
game art, logos or console symbols).

usage: make_icon.py OUT.png [SUBTITLE]   (512x512 RGBA; SUBTITLE e.g. HD-2D)"""
import math
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 512
SS = 4  # supersampling
W = S * SS
out = sys.argv[1]
subtitle = sys.argv[2] if len(sys.argv) > 2 else ""
FONT = "/usr/share/fonts/truetype/google-fonts/ofl/roboto/Roboto[wdth,wght].ttf"


def font(px, weight=800):
    f = ImageFont.truetype(FONT, px)
    try:
        f.set_variation_by_axes([weight, 100])
    except Exception:
        pass
    return f


img = Image.new("RGBA", (W, W))
# background: deep navy to black, lit from the upper left
bg = Image.new("RGB", (W, W))
px = bg.load()
for y in range(W):
    for x in range(W):
        d = math.hypot(x - W * 0.35, y - W * 0.30) / (W * 0.95)
        t = max(0.0, min(1.0, d))
        px[x, y] = (int(10 + 14 * (1 - t)), int(14 + 30 * (1 - t)), int(30 + 70 * (1 - t)))
img.paste(bg)

cx, cy, R = W * 0.5, W * 0.40, W * 0.28
glow = Image.new("RGBA", (W, W), (0, 0, 0, 0))
g = ImageDraw.Draw(glow)
# the time gate: spiral arms of light
for arm in range(3):
    for k in range(240):
        t = k / 240
        a = arm * 2 * math.pi / 3 + t * 2.6 * math.pi
        r = R * (0.15 + 1.05 * t)
        x, y = cx + r * math.cos(a), cy + r * math.sin(a) * 0.92
        w = W * 0.010 * (1 - t) + W * 0.004
        c = (int(80 + 120 * (1 - t)), int(170 + 70 * (1 - t)), 255, int(220 * (1 - t) + 20))
        g.ellipse([x - w, y - w, x + w, y + w], fill=c)
glow = glow.filter(ImageFilter.GaussianBlur(W * 0.006))
img.alpha_composite(glow)
halo = Image.new("RGBA", (W, W), (0, 0, 0, 0))
ImageDraw.Draw(halo).ellipse([cx - R * 1.05, cy - R * 1.05, cx + R * 1.05, cy + R * 1.05],
                             outline=(90, 180, 255, 200), width=int(W * 0.018))
img.alpha_composite(halo.filter(ImageFilter.GaussianBlur(W * 0.02)))

d = ImageDraw.Draw(img)
# the clock face: a white ring, twelve ticks, two hands
d.ellipse([cx - R, cy - R, cx + R, cy + R], outline=(240, 246, 255, 255), width=int(W * 0.012))
d.ellipse([cx - R * 0.9, cy - R * 0.9, cx + R * 0.9, cy + R * 0.9],
          outline=(150, 200, 255, 160), width=int(W * 0.004))
for h in range(12):
    a = h * math.pi / 6 - math.pi / 2
    r0 = R * (0.74 if h % 3 == 0 else 0.80)
    d.line([cx + r0 * math.cos(a), cy + r0 * math.sin(a),
            cx + R * 0.88 * math.cos(a), cy + R * 0.88 * math.sin(a)],
           fill=(235, 242, 255, 255), width=int(W * (0.012 if h % 3 == 0 else 0.006)))
for ang, length, width in ((-math.pi / 2 + math.radians(300), 0.48, 0.016),   # hour
                           (-math.pi / 2 + math.radians(60), 0.70, 0.010)):    # minute
    d.line([cx, cy, cx + R * length * math.cos(ang), cy + R * length * math.sin(ang)],
           fill=(255, 255, 255, 255), width=int(W * width))
d.ellipse([cx - W * 0.016, cy - W * 0.016, cx + W * 0.016, cy + W * 0.016], fill=(120, 200, 255, 255))

# the name
title = font(int(W * 0.17), 900)
tw = d.textlength("CT5", font=title)
ty = W * 0.765 if not subtitle else W * 0.785
shadow = Image.new("RGBA", (W, W), (0, 0, 0, 0))
ImageDraw.Draw(shadow).text((cx - tw / 2, ty), "CT5", font=title, fill=(60, 150, 255, 200))
img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(W * 0.012)))
d.text((cx - tw / 2, ty), "CT5", font=title, fill=(255, 255, 255, 255))
if subtitle:
    sf = font(int(W * 0.055), 700)
    sw = d.textlength(subtitle, font=sf)
    bx0, by0 = cx - sw / 2 - W * 0.02, W * 0.715
    d.rounded_rectangle([bx0, by0, cx + sw / 2 + W * 0.02, by0 + W * 0.07], radius=W * 0.015,
                        fill=(40, 110, 230, 255))
    d.text((cx - sw / 2, by0 + W * 0.004), subtitle, font=sf, fill=(255, 255, 255, 255))

img.resize((S, S), Image.LANCZOS).save(out)
print(out)
