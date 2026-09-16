#!/usr/bin/env python3
"""Regenerate icons.qrc so every PNG (default + light variant) is listed."""

from pathlib import Path

ICON_DIR = Path(__file__).parent / "icons"
QRC_PATH = Path(__file__).parent / "icons.qrc"

pngs = sorted(p for p in ICON_DIR.glob("*.png") if p.stem != "app-icon")
svgs = sorted(ICON_DIR.glob("*.svg"))

lines = [
    "<RCC>",
    '  <qresource prefix="/icons">',
    "    <!-- App branding: keep vector source and Windows .ico -->",
    "    <file>icons/app-icon.svg</file>",
    "    <file>icons/app-icon.ico</file>",
    "",
    "    <!-- PNG render of every SVG icon. QtSvg is unreliable on the ARM board,",
    "         so the application loads these PNG resources instead. -->",
    "    <file>icons/app-icon.png</file>",
]

for png in pngs:
    lines.append(f"    <file>icons/{png.name}</file>")

lines.append("")
lines.append("    <!-- Original SVG files are kept as editable source. -->")
for svg in svgs:
    if svg.name == "app-icon.svg":
        continue
    lines.append(f"    <file>icons/{svg.name}</file>")

lines.extend([
    "  </qresource>",
    "</RCC>",
    "",
])

QRC_PATH.write_text("\n".join(lines), encoding="utf-8")
print(f"Wrote {len(pngs)} PNG entries and {len(svgs)} SVG entries to {QRC_PATH}")
