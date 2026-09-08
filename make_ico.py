from PIL import Image, ImageDraw
import io
import os
import struct
import sys


def make_icon(size):
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    margin = max(1, int(size * 2 / 64))
    radius = int(size * 14 / 64)
    stroke = max(1, int(size * 2 / 64))

    draw.rounded_rectangle(
        [margin, margin, size - margin, size - margin],
        radius=radius,
        fill='#0d1b2a',
        outline='#1b3a4b',
        width=stroke,
    )

    s = size / 64.0

    def rect(x0, y0, x1, y1):
        draw.rectangle([x0 * s, y0 * s, x1 * s, y1 * s], fill='#0ea5e9')

    # Draw a thick "H" in the center
    rect(12, 16, 20, 48)
    rect(44, 16, 52, 48)
    rect(20, 16, 44, 28)
    rect(20, 36, 44, 48)

    return img


def build_ico(images, out_path):
    png_datas = []
    for img in images:
        buf = io.BytesIO()
        img.save(buf, format='PNG')
        png_datas.append(buf.getvalue())

    ico = io.BytesIO()
    num = len(images)
    ico.write(struct.pack('<HHH', 0, 1, num))

    header_size = 6 + 16 * num
    offset = header_size
    entries = []
    for img, data in zip(images, png_datas):
        w, h = img.size
        entries.append((w if w < 256 else 0, h if h < 256 else 0, len(data), offset))
        offset += len(data)

    for w, h, size, off in entries:
        ico.write(struct.pack('<BBBBHHII', w, h, 0, 0, 1, 32, size, off))

    for data in png_datas:
        ico.write(data)

    with open(out_path, 'wb') as f:
        f.write(ico.getvalue())


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [make_icon(s) for s in sizes]
    out_path = os.path.join(os.path.dirname(__file__), 'icons', 'app-icon.ico')
    build_ico(images, out_path)
    print(f'generated: {out_path} ({len(sizes)} sizes)')


if __name__ == '__main__':
    main()
