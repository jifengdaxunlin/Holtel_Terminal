# -*- coding: utf-8 -*-
"""Generate 4 new line-art icons (flame/gas/fan/pump) matching existing style.
Dark theme: white stroke on transparent. Light theme: dark navy (27,37,52).
Canvas 512 supersampled, downscaled to 64x64 like existing PNGs.
"""
import math
from PIL import Image, ImageDraw

OUT = r"D:/code/Qt/project/HotelTerminal/HotelTerminal/icons"
S = 512          # supersample canvas
FINAL = 64       # final size
W = 34           # stroke width at 512 (~4px at 64)

WHITE = (255, 255, 255, 255)
NAVY = (27, 37, 52, 255)


def canvas():
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def polyline(d, pts, color):
    d.line(pts, fill=color, width=W, joint="curve")
    # round caps
    r = W / 2.0
    for (x, y) in (pts[0], pts[-1]):
        d.ellipse([x - r, y - r, x + r, y + r], fill=color)


def circle(d, cx, cy, r, color, width=W):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=color, width=width)


# ---------------------------------------------------------------- flame
def draw_flame(color):
    im, d = canvas()
    # outer flame contour: tip -> right bulge -> bottom -> left bulge -> tip
    pts = [
        (256, 56),
        (312, 128), (352, 196), (356, 264), (336, 330),
        (300, 392), (256, 424),
        (212, 392), (176, 330), (156, 264), (160, 196), (200, 128),
        (256, 56),
    ]
    # smooth via many interpolated points (quadratic-ish sampling)
    smooth = []
    for i in range(len(pts) - 1):
        x0, y0 = pts[i]
        x1, y1 = pts[i + 1]
        for t in range(12):
            smooth.append((x0 + (x1 - x0) * t / 12.0, y0 + (y1 - y0) * t / 12.0))
    polyline(d, smooth, color)
    # inner flame: small teardrop
    inner = [
        (256, 220), (290, 272), (300, 316), (286, 356),
        (256, 384), (226, 356), (212, 316), (222, 272), (256, 220),
    ]
    smooth2 = []
    for i in range(len(inner) - 1):
        x0, y0 = inner[i]
        x1, y1 = inner[i + 1]
        for t in range(10):
            smooth2.append((x0 + (x1 - x0) * t / 10.0, y0 + (y1 - y0) * t / 10.0))
    polyline(d, smooth2, color)
    return im.resize((FINAL, FINAL), Image.LANCZOS)


# ---------------------------------------------------------------- gas (steam waves)
def draw_gas(color):
    im, d = canvas()
    for cx in (140, 256, 372):
        pts = []
        for i in range(41):
            t = i / 40.0
            y = 88 + t * 336
            x = cx + math.sin(t * math.pi * 2.2 + 0.4) * 34
            pts.append((x, y))
        polyline(d, pts, color)
    return im.resize((FINAL, FINAL), Image.LANCZOS)


# ---------------------------------------------------------------- fan (3 blades + hub)
def draw_fan(color):
    im, d = canvas()
    # hub
    circle(d, 256, 256, 44, color)
    # blades: ellipse outlines rotated around center
    for k in range(3):
        ang = k * 120.0
        blade = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        bd = ImageDraw.Draw(blade)
        # blade: rounded ellipse at top area
        bd.ellipse([256 - 52, 76, 256 + 52, 196], outline=color, width=W)
        rotated = blade.rotate(ang, resample=Image.BICUBIC, center=(256, 256))
        im.alpha_composite(rotated)
    return im.resize((FINAL, FINAL), Image.LANCZOS)


# ---------------------------------------------------------------- pump (tap + drop)
def draw_pump(color):
    im, d = canvas()
    # top horizontal pipe (rounded rect outline)
    d.rounded_rectangle([96, 96, 416, 176], radius=40, outline=color, width=W)
    # left mounting stub
    d.line([(96, 136), (56, 136)], fill=color, width=W)
    d.ellipse([56 - W / 2, 136 - W / 2, 56 + W / 2, 136 + W / 2], fill=color)
    # spout going down on the right
    d.rounded_rectangle([296, 176, 376, 280], radius=28, outline=color, width=W)
    # water drop under spout
    drop = [
        (336, 312), (368, 360), (376, 392),
    ]
    for i in range(len(drop) - 1):
        d.line([drop[i], drop[i + 1]], fill=color, width=W, joint="curve")
    # bottom arc of the drop
    d.arc([296, 352, 376, 424], start=10, end=170, fill=color, width=W)
    d.line([(297, 393), (336, 312)], fill=color, width=W, joint="curve")
    return im.resize((FINAL, FINAL), Image.LANCZOS)


GENS = {
    "flame": draw_flame,
    "gas": draw_gas,
    "fan": draw_fan,
    "pump": draw_pump,
}

for name, fn in GENS.items():
    fn(WHITE).save(OUT + "/" + name + ".png")
    fn(NAVY).save(OUT + "/" + name + "-light.png")
    print("saved", name)
print("done")
