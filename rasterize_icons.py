#!/usr/bin/env python3
"""Offline SVG stroke rasterizer (PIL only) for the HotelTerminal icon set.

render_icons.py relies on reportlab's renderPM, which needs a native cairo
backend (rlPyCairo / cairosvg). That backend is not installed on this machine
and pulling in native cairo on Windows is fragile, so this script rasterizes
the stroke-based icons directly with PIL.

The icons are simple 24x24 stroke drawings (fill="none"), which is exactly the
subset implemented here: path (M/L/H/V/C/S/Q/T/A/Z, abs+rel), line, rect,
circle, ellipse, polyline, polygon.

Output matches the existing convention:
  - 64x64 PNG, supersampled 4x then downscaled with LANCZOS
  - xxx.png       : stroke #ffffff  (dark themes)
  - xxx-light.png : stroke #1a2332  (light themes)
"""
from __future__ import annotations

import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageDraw

ICON_DIR = Path(r"D:\虚拟机\share\Ubuntu_16.02ARM_Linux\HotelTerminal\HotelTerminal\icons")

OUT_SIZE = 64          # final PNG size, same as render_icons.py
SUPERSAMPLE = 4        # render at 4x then downscale for smooth edges
STROKE_COLOR_DARK = (255, 255, 255, 255)
STROKE_COLOR_LIGHT = (26, 35, 50, 255)   # #1a2332

CURVE_SAMPLES = 96     # samples per bezier / arc segment


# --------------------------------------------------------------------------
# path-data tokenizer + flattener
# --------------------------------------------------------------------------
NUM = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?")
CMD = re.compile(r"[MmLlHhVvCcSsQqTtAaZz]")


def _tokens(d: str):
    """Yield ('cmd', c) / ('num', v) tokens from an SVG path data string."""
    i, n = 0, len(d)
    while i < n:
        ch = d[i]
        if ch.isspace() or ch == ",":
            i += 1
            continue
        if CMD.match(ch):
            yield ("cmd", ch)
            i += 1
            continue
        m = NUM.match(d, i)
        if not m:
            raise ValueError(f"bad path data near {d[i:i+20]!r}")
        yield ("num", float(m.group()))
        i = m.end()


def _bezier(p0, p1, p2, p3, out):
    for k in range(1, CURVE_SAMPLES + 1):
        t = k / CURVE_SAMPLES
        u = 1 - t
        out.append((
            u**3 * p0[0] + 3 * u**2 * t * p1[0] + 3 * u * t**2 * p2[0] + t**3 * p3[0],
            u**3 * p0[1] + 3 * u**2 * t * p1[1] + 3 * u * t**2 * p2[1] + t**3 * p3[1],
        ))


def _quad(p0, p1, p2, out):
    for k in range(1, CURVE_SAMPLES + 1):
        t = k / CURVE_SAMPLES
        u = 1 - t
        out.append((
            u**2 * p0[0] + 2 * u * t * p1[0] + t**2 * p2[0],
            u**2 * p0[1] + 2 * u * t * p1[1] + t**2 * p2[1],
        ))


def _arc(p0, rx, ry, rot_deg, large, sweep, p1, out):
    """SVG elliptical arc -> sampled points (endpoint to center conversion)."""
    x1, y1 = p0
    x2, y2 = p1
    rx, ry = abs(rx), abs(ry)
    if rx == 0 or ry == 0:
        out.append((x2, y2))
        return
    phi = math.radians(rot_deg)
    cosphi, sinphi = math.cos(phi), math.sin(phi)

    dx, dy = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    x1p = cosphi * dx + sinphi * dy
    y1p = -sinphi * dx + cosphi * dy

    # radii correction (spec F.6.6)
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1.0:
        s = math.sqrt(lam)
        rx, ry = rx * s, ry * s

    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = 0.0 if den == 0 else math.sqrt(max(0.0, num / den))
    if bool(large) == bool(sweep):
        co = -co
    cxp = co * rx * y1p / ry
    cyp = -co * ry * x1p / rx

    cx = cosphi * cxp - sinphi * cyp + (x1 + x2) / 2.0
    cy = sinphi * cxp + cosphi * cyp + (y1 + y2) / 2.0

    def ang(ux, uy, vx, vy):
        d = ux * ux + uy * uy
        e = vx * vx + vy * vy
        if d == 0 or e == 0:
            return 0.0
        c = max(-1.0, min(1.0, (ux * vx + uy * vy) / math.sqrt(d * e)))
        a = math.acos(c)
        return -a if (ux * vy - uy * vx) < 0 else a

    th1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dth = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dth > 0:
        dth -= 2 * math.pi
    elif sweep and dth < 0:
        dth += 2 * math.pi

    for k in range(1, CURVE_SAMPLES + 1):
        th = th1 + dth * (k / CURVE_SAMPLES)
        rx_cos, ry_sin = rx * math.cos(th), ry * math.sin(th)
        out.append((
            cosphi * rx_cos - sinphi * ry_sin + cx,
            sinphi * rx_cos + cosphi * ry_sin + cy,
        ))


def flatten_path(d: str):
    """Flatten path data into a list of subpaths; each is a point list."""
    toks = list(_tokens(d))
    subpaths, cur = [], []
    x = y = 0.0
    sx = sy = 0.0                      # subpath start
    last_cmd = None
    prev_ctrl = None                   # for S/T reflection
    i = 0

    def start_sub(nx, ny):
        nonlocal cur, sx, sy
        if cur:
            subpaths.append(cur)
        cur = [(nx, ny)]
        sx, sy = nx, ny

    while i < len(toks):
        kind, val = toks[i]
        if kind == "cmd":
            cmd = val
            i += 1
        else:
            # Implicit repetition: a command with no leading letter reuses the
            # previous one. Per spec, coordinates following M/m are treated as
            # L/l (keeping the absolute/relative flavour), not as another move.
            if last_cmd is None:
                raise ValueError("path data starts with a number")
            if last_cmd == "M":
                cmd = "L"
            elif last_cmd == "m":
                cmd = "l"
            else:
                cmd = last_cmd
        rel = cmd.islower()
        C = cmd.upper()

        def nx_(v):
            return x + v if rel else v

        def ny_(v):
            return y + v if rel else v

        def num():
            nonlocal i
            k, v = toks[i]
            if k != "num":
                raise ValueError(f"expected number for {cmd}, got {k}")
            i += 1
            return v

        if C == "Z":
            if cur:
                cur.append((sx, sy))
                subpaths.append(cur)
                cur = []
            x, y = sx, sy
            prev_ctrl = None
        elif C == "M":
            px, py = nx_(num()), ny_(num())
            start_sub(px, py)
            x, y = px, py
            last_cmd = cmd
            prev_ctrl = None
            continue                      # following pairs are implicit L
        elif C == "L":
            px, py = nx_(num()), ny_(num())
            cur.append((px, py))
            x, y = px, py
            prev_ctrl = None
        elif C == "H":
            px = nx_(num())
            cur.append((px, y))
            x = px
            prev_ctrl = None
        elif C == "V":
            py = ny_(num())
            cur.append((x, py))
            y = py
            prev_ctrl = None
        elif C == "C":
            x1, y1 = nx_(num()), ny_(num())
            x2, y2 = nx_(num()), ny_(num())
            px, py = nx_(num()), ny_(num())
            _bezier((x, y), (x1, y1), (x2, y2), (px, py), cur)
            prev_ctrl = (x2, y2)
            x, y = px, py
        elif C == "S":
            x2, y2 = nx_(num()), ny_(num())
            px, py = nx_(num()), ny_(num())
            x1 = 2 * x - prev_ctrl[0] if (prev_ctrl and last_cmd and last_cmd.upper() in "CS") else x
            y1 = 2 * y - prev_ctrl[1] if (prev_ctrl and last_cmd and last_cmd.upper() in "CS") else y
            _bezier((x, y), (x1, y1), (x2, y2), (px, py), cur)
            prev_ctrl = (x2, y2)
            x, y = px, py
        elif C == "Q":
            x1, y1 = nx_(num()), ny_(num())
            px, py = nx_(num()), ny_(num())
            _quad((x, y), (x1, y1), (px, py), cur)
            prev_ctrl = (x1, y1)
            x, y = px, py
        elif C == "T":
            px, py = nx_(num()), ny_(num())
            x1 = 2 * x - prev_ctrl[0] if (prev_ctrl and last_cmd and last_cmd.upper() in "QT") else x
            y1 = 2 * y - prev_ctrl[1] if (prev_ctrl and last_cmd and last_cmd.upper() in "QT") else y
            _quad((x, y), (x1, y1), (px, py), cur)
            prev_ctrl = (x1, y1)
            x, y = px, py
        elif C == "A":
            rx, ry = num(), num()
            rot = num()
            large, sweep = num(), num()
            px, py = nx_(num()), ny_(num())
            _arc((x, y), rx, ry, rot, large, sweep, (px, py), cur)
            x, y = px, py
            prev_ctrl = None
        else:
            raise ValueError(f"unsupported command {cmd}")
        last_cmd = cmd

    if cur:
        subpaths.append(cur)
    return subpaths


def _f(v, default=0.0):
    return float(v) if v is not None else default


def flatten_element(tag: str, attr: dict):
    """Return subpaths for a basic SVG shape element."""
    if tag == "path":
        return flatten_path(attr.get("d", ""))
    if tag == "line":
        return [[(_f(attr.get("x1")), _f(attr.get("y1"))),
                 (_f(attr.get("x2")), _f(attr.get("y2")))]]
    if tag == "polyline":
        nums = [float(v) for v in NUM.findall(attr.get("points", ""))]
        return [list(zip(nums[0::2], nums[1::2]))]
    if tag == "polygon":
        nums = [float(v) for v in NUM.findall(attr.get("points", ""))]
        pts = list(zip(nums[0::2], nums[1::2]))
        if len(pts) > 2:
            pts = pts + [pts[0]]
        return [pts]
    if tag == "rect":
        x, y = _f(attr.get("x")), _f(attr.get("y"))
        w, h = _f(attr.get("width")), _f(attr.get("height"))
        rx = _f(attr.get("rx"))
        ry = _f(attr.get("ry", attr.get("rx")))
        rx = min(rx, w / 2.0)
        ry = min(ry, h / 2.0)
        if rx <= 0 or ry <= 0:
            return [[(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)]]
        d = (f"M{x+rx} {y} H{x+w-rx} A{rx} {ry} 0 0 1 {x+w} {y+ry} "
             f"V{y+h-ry} A{rx} {ry} 0 0 1 {x+w-rx} {y+h} H{x+rx} "
             f"A{rx} {ry} 0 0 1 {x} {y+h-ry} V{y+ry} A{rx} {ry} 0 0 1 {x+rx} {y} Z")
        return flatten_path(d)
    if tag in ("circle", "ellipse"):
        cx, cy = _f(attr.get("cx")), _f(attr.get("cy"))
        r = _f(attr.get("r"))
        rx = r if tag == "circle" else _f(attr.get("rx"))
        ry = r if tag == "circle" else _f(attr.get("ry"))
        return flatten_path(f"M{cx-rx} {cy}a{rx} {ry} 0 1 0 {2*rx} 0a{rx} {ry} 0 1 0 {-2*rx} 0")
    return []


# --------------------------------------------------------------------------
# rasterize
# --------------------------------------------------------------------------
def rasterize(svg_path: Path, stroke_rgb, out_path: Path):
    root = ET.parse(str(svg_path)).getroot()

    # viewBox -> canvas transform (only the uniform square case is supported,
    # which is what every icon in this project uses)
    vb = (root.get("viewBox") or "0 0 24 24").replace(",", " ").split()
    vbx, vby, vbw, vbh = (float(v) for v in vb[:4])
    canvas = OUT_SIZE * SUPERSAMPLE
    scale = canvas / max(vbw, vbh)
    ox = (canvas - vbw * scale) / 2.0 - vbx * scale
    oy = (canvas - vbh * scale) / 2.0 - vby * scale

    base_w = _f(root.get("stroke-width"), 1.0)

    img = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    dr = ImageDraw.Draw(img)

    def walk(node, inherit_stroke, inherit_w, inherit_fill):
        # stroke / stroke-width / fill are declared once on the root <svg> in
        # this icon set and inherited by every shape, so they must be carried
        # down the tree rather than read from each element.
        for child in node:
            tag = child.tag.split("}")[-1]
            attr = child.attrib
            stroke = attr.get("stroke", inherit_stroke)
            sw = _f(attr.get("stroke-width"), inherit_w)
            fill = attr.get("fill", inherit_fill)

            if tag in ("g", "svg"):
                walk(child, stroke, sw, fill)
                continue
            if stroke is None or stroke == "none":
                continue

            subpaths = flatten_element(tag, attr)
            w = max(1, int(round(sw * scale)))
            for pts in subpaths:
                if len(pts) < 2:
                    continue
                scaled = [(px * scale + ox, py * scale + oy) for px, py in pts]
                dr.line(scaled, fill=stroke_rgb, width=w, joint="curve")
                # round caps (stroke-linecap="round" on every icon)
                r = w / 2.0
                for cx, cy in (scaled[0], scaled[-1]):
                    dr.ellipse([cx - r, cy - r, cx + r, cy + r], fill=stroke_rgb)
            if fill not in (None, "none"):
                for pts in subpaths:
                    if len(pts) >= 3:
                        dr.polygon(
                            [(px * scale + ox, py * scale + oy) for px, py in pts],
                            fill=stroke_rgb,
                        )

    root_stroke = root.get("stroke")
    root_w = _f(root.get("stroke-width"), base_w)
    root_fill = root.get("fill", "none")
    walk(root, root_stroke, root_w, root_fill)

    img = img.resize((OUT_SIZE, OUT_SIZE), Image.LANCZOS)
    img.save(out_path, format="PNG")


def main():
    names = sys.argv[1:]
    if not names:
        raise SystemExit("usage: rasterize_icons.py <name> [<name> ...]")
    for name in names:
        svg = ICON_DIR / f"{name}.svg"
        if not svg.exists():
            raise SystemExit(f"missing {svg}")
        rasterize(svg, STROKE_COLOR_DARK, ICON_DIR / f"{name}.png")
        rasterize(svg, STROKE_COLOR_LIGHT, ICON_DIR / f"{name}-light.png")
        print(f"OK {name}.png + {name}-light.png")
    print(f"rendered {len(names)} icon(s)")


if __name__ == "__main__":
    main()
