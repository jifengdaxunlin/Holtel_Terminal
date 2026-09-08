#!/usr/bin/env python3
"""Render all SVG icons in icons/ to PNG for embedded resource use.

ARM boards (Qt 5.4.1 buildroot) often lack a working QtSvg runtime, which
produces "Cannot open file ':/icons/xxx.svg'" at launch. Pre-rendering the
icons to PNG and loading the PNG resources avoids this dependency entirely.

Navigation icons are rendered twice:
  - xxx.png        : white stroke for the dark theme
  - xxx-light.png  : dark (#1a2332) stroke for the light theme
"""

import os
import sys
import tempfile
from pathlib import Path

from svglib.svglib import svg2rlg
from reportlab.graphics import renderPM
from PIL import Image

ICON_DIR = Path(__file__).parent / "icons"
OUT_DIR = ICON_DIR  # write png next to svg
PNG_SIZE = 64  # high enough for all current icon sizes (28..44px)

# Buttons that need a light-theme variant with a dark stroke.
NAV_ICONS = {"home", "bed", "key", "bell", "gear", "moon", "sun"}


def render_drawing(drawing, out_path: Path, size: int) -> bool:
    """Render a ReportLab drawing to a square PNG of the requested size."""
    tmp_path = out_path.with_suffix(".tmp.png")
    try:
        renderPM.drawToFile(drawing, str(tmp_path), fmt="PNG", dpi=96)
        with Image.open(tmp_path) as img:
            img = img.convert("RGBA")
            img = img.resize((size, size), Image.LANCZOS)
            img.save(out_path, format="PNG")
        tmp_path.unlink(missing_ok=True)
        return True
    except Exception as e:
        print(f"ERR: {out_path} -> {e}")
        return False


def render_svg(svg_path: Path) -> bool:
    png_path = svg_path.with_suffix(".png")
    try:
        drawing = svg2rlg(str(svg_path))
        if drawing is None:
            print(f"WARN: could not parse {svg_path}")
            return False
        if not render_drawing(drawing, png_path, PNG_SIZE):
            return False
        print(f"OK: {png_path.name}")

        # Light-theme variant for navigation icons.
        base = svg_path.stem
        if base in NAV_ICONS:
            light_path = ICON_DIR / f"{base}-light.png"
            try:
                with open(svg_path, "r", encoding="utf-8") as f:
                    svg_text = f.read()
                # Recolor white strokes to a dark colour visible on light backgrounds.
                light_svg = svg_text.replace('stroke="#ffffff"', 'stroke="#1a2332"')
                # Also recolour any fill="#ffffff" if present.
                light_svg = light_svg.replace('fill="#ffffff"', 'fill="#1a2332"')
                with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as tf:
                    tf.write(light_svg)
                    tmp_svg = Path(tf.name)
                light_drawing = svg2rlg(str(tmp_svg))
                tmp_svg.unlink(missing_ok=True)
                if light_drawing is None:
                    print(f"WARN: could not parse light variant of {svg_path}")
                    return False
                if render_drawing(light_drawing, light_path, PNG_SIZE):
                    print(f"OK: {light_path.name}")
                else:
                    return False
            except Exception as e:
                print(f"ERR: light variant of {svg_path} -> {e}")
                return False
        return True
    except Exception as e:
        print(f"ERR: {svg_path} -> {e}")
        return False


def main():
    svg_files = sorted(ICON_DIR.glob("*.svg"))
    if not svg_files:
        print(f"No SVG files found in {ICON_DIR}")
        sys.exit(1)

    ok = 0
    fail = 0
    for svg in svg_files:
        if render_svg(svg):
            ok += 1
        else:
            fail += 1

    print(f"\nRendered {ok}/{len(svg_files)} icons to PNG ({PNG_SIZE}x{PNG_SIZE})")
    if fail:
        sys.exit(1)


if __name__ == "__main__":
    main()
