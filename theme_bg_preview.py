#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""主题背景预览生成器（仅用于设计验证，不参与编译/部署）。

把 mainwindow.cpp 里 MainWindow::buildThemeBackground / ensureThemeBackground
的绘制指令用 PIL 复刻一遍，输出 800x480 的 6 张预览图 + 一张拼图，
方便在没有 ARM 板的情况下先确认配色和图案是否合适。

用法：  python theme_bg_preview.py
输出：  preview_bg/<主题ID>.png  、 preview_bg/all_themes.png
"""
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 800, 480
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'preview_bg')

THEMES = [
    # id, name, dark, accent, accent2, card0, card1, checked0, checked1, border,
    # sidebar0, sidebar1, sidebarBorder, textMain, textSub, bg
    ("deepsea", "深海蓝", True, (0x00, 0xd4, 0xff), (0x00, 0x77, 0xbe),
     (0x0f, 0x2a, 0x52), (0x08, 0x1a, 0x33), (0x0a, 0x22, 0x45), (0x06, 0x1a, 0x36),
     (0x1a, 0x3a, 0x6b), (0x0a, 0x1a, 0x33), (0x05, 0x10, 0x20), (0x13, 0x2b, 0x4d),
     (0xff, 0xff, 0xff), (0x8a, 0xa4, 0xc8), (0x05, 0x0d, 0x1a)),
    ("moonlight", "月光浅", False, (0x00, 0x77, 0xbe), (0x00, 0xd4, 0xff),
     (0xff, 0xff, 0xff), (0xe8, 0xee, 0xf5), (0xe0, 0xf6, 0xff), (0xc8, 0xec, 0xfc),
     (0xc8, 0xd2, 0xdd), (0xff, 0xff, 0xff), (0xe8, 0xee, 0xf5), (0xc8, 0xd2, 0xdd),
     (0x1a, 0x23, 0x32), (0x5a, 0x6b, 0x82), (0xee, 0xf2, 0xf6)),
    ("obsidian", "曜石金", True, (0xf1, 0xc4, 0x0f), (0xb8, 0x86, 0x0b),
     (0x2a, 0x24, 0x16), (0x1a, 0x16, 0x08), (0x3a, 0x31, 0x16), (0x24, 0x1d, 0x0a),
     (0x6b, 0x5a, 0x1a), (0x1a, 0x16, 0x08), (0x0d, 0x0b, 0x04), (0x4a, 0x3d, 0x12),
     (0xff, 0xff, 0xff), (0xc8, 0xb9, 0x8a), (0x0d, 0x0b, 0x04)),
    ("jade", "墨玉绿", True, (0x2e, 0xcc, 0x71), (0x1e, 0x84, 0x49),
     (0x10, 0x33, 0x1f), (0x08, 0x1a, 0x10), (0x14, 0x47, 0x2a), (0x0a, 0x24, 0x16),
     (0x1a, 0x6b, 0x3d), (0x0a, 0x24, 0x16), (0x05, 0x10, 0x09), (0x12, 0x4a, 0x2a),
     (0xff, 0xff, 0xff), (0x8a, 0xc8, 0xa4), (0x05, 0x10, 0x09)),
    ("violet", "幻紫", True, (0xa5, 0x69, 0xbd), (0x6c, 0x34, 0x83),
     (0x2a, 0x1a, 0x45), (0x16, 0x0d, 0x24), (0x38, 0x21, 0x5a), (0x1e, 0x12, 0x30),
     (0x4a, 0x2a, 0x6b), (0x1e, 0x12, 0x30), (0x10, 0x09, 0x19), (0x3a, 0x21, 0x54),
     (0xff, 0xff, 0xff), (0xb8, 0x9a, 0xc8), (0x10, 0x09, 0x19)),
    ("sunrise", "暖阳橙", False, (0xe6, 0x7e, 0x22), (0xd3, 0x54, 0x00),
     (0xff, 0xff, 0xff), (0xfd, 0xf0, 0xe2), (0xff, 0xe8, 0xd0), (0xff, 0xd6, 0xac),
     (0xe8, 0xcd, 0xb0), (0xff, 0xff, 0xff), (0xfd, 0xf0, 0xe2), (0xe8, 0xcd, 0xb0),
     (0x33, 0x23, 0x1a), (0x82, 0x62, 0x4a), (0xfd, 0xf6, 0xee)),
]

# 侧栏 80px、顶栏 64px（与 mainwindow.ui 的 maximumSize 一致），卡片留白处才看得到背景
SIDEBAR_W = 80
TOPBAR_H = 64


def byte_alpha(a):
    return max(0, min(255, int(a * 255.0 + 0.5)))


def pseudo(i, salt):
    h = (i * 1103515245 + salt * 12345 + 7919) & 0x7FFFFFFF
    return (h % 10007) / 10007.0


def mix(c1, c2, f):
    return tuple(int(c1[k] + (c2[k] - c1[k]) * f) for k in range(3))


# --- 加法混合（模拟 QPainter::CompositionMode_Plus）------------------------
def add_layer(base, layer):
    """base/layer 都是 RGBA 图，返回 base + layer（各自预乘后相加）"""
    import numpy as np
    b = np.asarray(base, dtype=np.uint16).copy()
    l = np.asarray(layer, dtype=np.uint16)
    la = l[:, :, 3:4]
    rgb = (l[:, :, :3] * la) // 255
    b[:, :, :3] = np.clip(b[:, :, :3] + rgb, 0, 255)
    return Image.fromarray(b.astype('uint8'), 'RGBA')


def over(base, layer):
    """base 叠上 layer（保留 base 的尺寸）。

    注意：不能用 Image.alpha_composite(base, layer) —— 它的尺寸由 layer 决定，
    而本脚本里经常把"裁剪出来的小图层"传给 over()，那会导致整层贴到左上角。
    这里统一用 paste(自身 alpha 当蒙版)，始终以 base 为画布。"""
    out = base.copy()
    out.paste(layer, (0, 0), layer)
    return out


def new_layer():
    return Image.new('RGBA', (W, H), (0, 0, 0, 0))


def mask_multiply(alpha, mask):
    """把圆角蒙版乘到已有 alpha 上。

    坑：putalpha(mask) 是【替换】alpha —— 渐变图层本来带着 200 左右的透明度，
    换成 fill=255 的圆角蒙版后就变成全不透明，卡片/占位块会亮得离谱
    （预览里"舒适客房示意图"曾因此渲染成一块刺眼的纯青色）。"""
    import numpy as np
    a = np.asarray(alpha, dtype=np.uint16)
    m = np.asarray(mask, dtype=np.uint16)
    return Image.fromarray(((a * m) // 255).astype('uint8'), 'L')


def radial_alpha(shape, center, radius, stops):
    """stops: [(pos, alpha0..1)] -> L 模式 alpha 图"""
    import numpy as np
    ys, xs = np.mgrid[0:shape[1], 0:shape[0]]
    d = np.sqrt((xs - center[0]) ** 2 + (ys - center[1]) ** 2) / max(radius, 1e-6)
    d = np.clip(d, 0, 1)
    pos = [s[0] for s in stops]
    a = np.zeros_like(d)
    for i in range(len(stops) - 1):
        p0, p1 = pos[i], pos[i + 1]
        a0, a1 = stops[i][1], stops[i + 1][1]
        m = (d >= p0) & (d <= p1)
        if p1 > p0:
            t = (d - p0) / (p1 - p0)
            a = np.where(m, a0 + (a1 - a0) * t, a)
    a = np.where(d >= pos[-1], stops[-1][1], a)
    return Image.fromarray((np.clip(a, 0, 1) * 255).astype('uint8'), 'L')


def add_glow(img, center, radius, color, peak, spread=1.0):
    if radius < 2:
        return
    alpha = radial_alpha((W, H), center, radius,
                         [(0.0, peak), (spread, peak * 0.45), (1.0, 0.0)])
    lay = Image.new('RGBA', (W, H), color + (0,))
    lay.putalpha(alpha)
    img.paste(over(img, lay), (0, 0))


def add_vignette(img, edge_alpha, start):
    alpha = radial_alpha((W, H), (W / 2.0, H / 2.0), max(W, H) * 0.75,
                         [(0.0, 0.0), (start, 0.0), (1.0, edge_alpha)])
    lay = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    lay.putalpha(alpha)
    img.paste(over(img, lay), (0, 0))


def add_beam(img, from_x, to_x, w, color, alpha):
    lay = new_layer()
    d = ImageDraw.Draw(lay)
    poly = [(from_x - w * 0.5, 0), (from_x + w * 0.5, 0),
            (to_x + w * 1.6, H), (to_x - w * 1.6, H)]
    # 竖直渐变：顶部 alpha -> 55% 处 -> 底部 0
    grad = Image.new('L', (1, H))
    for y in range(H):
        t = y / float(H - 1)
        if t <= 0.55:
            a = byte_alpha(alpha) * (1 - t / 0.55) + byte_alpha(alpha * 0.28) * (t / 0.55)
        else:
            a = byte_alpha(alpha * 0.28) * (1 - (t - 0.55) / 0.45)
        grad.putpixel((0, y), int(a))
    mask = Image.new('L', (W, H), 0)
    ImageDraw.Draw(mask).polygon(poly, fill=255)
    import numpy as np
    m = np.asarray(mask, dtype='uint16') * np.asarray(grad.resize((W, H)).transpose(Image.ROTATE_90).transpose(Image.ROTATE_90), dtype='uint16') // 255
    # 简化：直接用 polygon 裁剪 + 逐行 alpha
    rows = np.asarray(grad).reshape(H).astype('uint16')
    mm = (np.asarray(mask, dtype='uint16') * rows[:, None]) // 255
    lay.putalpha(Image.fromarray(mm.astype('uint8'), 'L'))
    lay = Image.new('RGBA', (W, H), color + (255,))
    lay.putalpha(Image.fromarray(mm.astype('uint8'), 'L'))
    img.paste(add_layer(img, lay), (0, 0))


def add_dots(img, spacing, dot, color, alpha):
    lay = new_layer()
    d = ImageDraw.Draw(lay)
    iy = 0
    y = -int(spacing)
    while y <= H + spacing:
        ix = 0
        x = -int(spacing)
        while x <= W + spacing:
            if not ((ix + iy) & 1):
                d.rectangle([x, y, x + dot, y + dot], fill=color + (byte_alpha(alpha),))
            x += int(spacing)
            ix += 1
        y += int(spacing)
        iy += 1
    img.paste(over(img, lay), (0, 0))


def add_cross_hatch(img, step, color, alpha):
    lay = new_layer()
    d = ImageDraw.Draw(lay)
    k = -H
    while k < W:
        d.line([(k, H), (k + H, 0)], fill=color + (byte_alpha(alpha),), width=1)
        d.line([(k, 0), (k + H, H)], fill=color + (byte_alpha(alpha),), width=1)
        k += step
    img.paste(over(img, lay), (0, 0))


def add_horizon(img, y_top, thickness, color, alpha):
    lay = new_layer()
    d = ImageDraw.Draw(lay)
    for y in range(max(0, int(y_top - thickness)), min(H, int(y_top + thickness))):
        t = abs(y - y_top) / thickness
        a = byte_alpha(alpha) * max(0.0, 1 - t)
        d.line([(0, y), (W, y)], fill=color + (int(a),))
    img.paste(add_layer(img, lay), (0, 0))


def add_edge_band(img, x0, x1, top, bottom, thickness, color, alpha):
    """页面内容区上下边缘的主题色光带（对应 addEdgeBand）"""
    if bottom - top < thickness * 3.0:
        return
    a = byte_alpha(alpha)
    lay = new_layer()
    d = ImageDraw.Draw(lay)
    for y in range(int(top), min(H, int(top + thickness))):
        t = (y - top) / thickness
        d.line([(x0, y), (x1, y)], fill=color + (int(a * (1 - t)),))
    for y in range(max(0, int(bottom - thickness)), int(bottom)):
        t = (bottom - y) / thickness
        d.line([(x0, y), (x1, y)], fill=color + (int(a * (1 - t)),))
    img.paste(over(img, lay), (0, 0))


# --- 底色 -----------------------------------------------------------------
def base_gradient(th):
    tid, name, dark, accent, accent2 = th[0], th[1], th[2], th[3], th[4]
    img = Image.new('RGBA', (W, H))
    d = ImageDraw.Draw(img)
    if tid == 'deepsea':
        top, bot = (15, 46, 88), (3, 11, 24)
    elif tid == 'moonlight':
        top, bot = (228, 240, 251), (247, 250, 254)
    elif tid == 'obsidian':
        top, bot = (46, 37, 15), (7, 6, 3)
    elif tid == 'jade':
        top, bot = (18, 54, 37), (4, 14, 9)
    elif tid == 'violet':
        top, bot = (47, 26, 68), (13, 8, 19)
    else:
        top, bot = mix((253, 231, 202), accent, 0.10), (255, 248, 236)
    for y in range(H):
        d.line([(0, y), (W, y)], fill=mix(top, bot, y / float(H - 1)) + (255,))
    # 主题色薄雾（加法）
    lay = new_layer()
    dl = ImageDraw.Draw(lay)
    for y in range(H):
        f = y / float(H - 1)
        c = mix(accent, accent2, f)
        dl.line([(0, y), (W, y)], fill=(c[0], c[1], c[2], int(16 + (6 - 16) * f)))
    img = add_layer(img, lay)
    # 左侧副色辉光
    lay = new_layer()
    dl = ImageDraw.Draw(lay)
    for x in range(int(W * 0.35)):
        a = int(16 * (1 - x / (W * 0.35)))
        dl.line([(x, 0), (x, H)], fill=th[14] + (a,))
    img.paste(over(img, lay), (0, 0))
    return img


def build_background(th):
    tid, name, dark, accent, accent2 = th[0], th[1], th[2], th[3], th[4]
    img = base_gradient(th)

    if tid == 'deepsea':
        add_beam(img, W * 0.46, W * 0.58, 62, accent, 0.15)
        add_beam(img, W * 0.78, W * 0.92, 92, accent2, 0.11)
        add_beam(img, W * 0.18, W * 0.12, 48, accent, 0.08)
        add_glow(img, (W * 0.5, -H * 0.10), W * 0.70, accent, 0.15, 0.30)
        add_glow(img, (W * 0.16, H * 1.02), W * 0.45, accent2, 0.12, 0.25)
        lay = new_layer()
        for i in range(26):
            x, y = pseudo(i, 11) * W, pseudo(i, 23) * H
            r = 1.0 + pseudo(i, 31) * 3.0
            a = byte_alpha(0.10 + pseudo(i, 41) * 0.22)
            ImageDraw.Draw(lay).ellipse([x - r, y - r, x + r, y + r],
                                        outline=accent + (a,), width=1)
        img = over(img, lay)
        add_dots(img, 30, 2, accent, 0.09)
        add_vignette(img, 0.34, 0.30)

    elif tid == 'moonlight':
        add_glow(img, (W * 0.82, H * 0.14), W * 0.46, accent, 0.22, 0.08)
        add_glow(img, (W * 0.10, H * 0.90), W * 0.52, accent2, 0.16, 0.05)
        add_glow(img, (W * 0.50, H * 0.50), W * 0.75, (255, 255, 255), 0.14, 0.15)
        lay = new_layer()
        d = ImageDraw.Draw(lay)
        r = W * 0.075
        d.ellipse([W * 0.82 - r, H * 0.14 - r, W * 0.82 + r, H * 0.14 + r],
                  outline=(255, 255, 255, 52), width=2)
        for i in range(30):
            x = pseudo(i, 13) * W
            y = pseudo(i, 27) * H * 0.62
            rr = 1.0 + pseudo(i, 37) * 1.8
            a = byte_alpha(0.12 + pseudo(i, 47) * 0.30)
            d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=(70, 110, 165, a))
        img = over(img, lay)
        add_vignette(img, 0.10, 0.45)

    elif tid == 'obsidian':
        add_cross_hatch(img, 42, accent, 0.055)
        add_dots(img, 56, 2, accent, 0.10)
        add_beam(img, W * 0.86, W * 0.60, 130, accent, 0.09)
        add_glow(img, (W * 0.88, H * 0.06), W * 0.50, accent, 0.17, 0.10)
        add_glow(img, (W * 0.18, H * 0.95), W * 0.42, accent2, 0.14, 0.15)
        add_vignette(img, 0.50, 0.28)

    elif tid == 'jade':
        add_glow(img, (W * 0.88, H * 0.16), W * 0.46, accent, 0.15, 0.10)
        add_glow(img, (W * 0.10, H * 0.88), W * 0.44, accent2, 0.13, 0.15)
        lay = new_layer()
        d = ImageDraw.Draw(lay)
        d.arc([-W * 0.10, -H * 0.30, -W * 0.10 + W * 0.62, -H * 0.30 + H * 0.95],
              start=270, end=30, fill=accent + (64,), width=2)
        d.arc([-W * 0.22, -H * 0.12, -W * 0.22 + W * 0.52, -H * 0.12 + H * 0.80],
              start=250, end=360, fill=accent + (44,), width=1)
        d.arc([W * 0.52, H * 0.34, W * 0.52 + W * 0.60, H * 0.34 + H * 0.90],
              start=250, end=360, fill=accent2 + (54,), width=2)
        d.line([(W * 0.045, 0), (W * 0.075, H)], fill=accent + (34,), width=2)
        for i in range(1, 6):
            y = H * i / 6.0
            d.line([(W * 0.045, y), (W * 0.075, y)], fill=accent + (24,), width=1)
        img = over(img, lay)
        add_dots(img, 34, 2, accent, 0.075)
        add_vignette(img, 0.42, 0.28)

    elif tid == 'violet':
        add_glow(img, (W * 0.26, H * 0.30), W * 0.40, accent, 0.20, 0.10)
        add_glow(img, (W * 0.72, H * 0.22), W * 0.34, accent2, 0.18, 0.10)
        add_glow(img, (W * 0.55, H * 0.88), W * 0.46, accent, 0.15, 0.12)
        add_beam(img, W * 0.50, W * 0.50, 56, accent, 0.07)
        lay = new_layer()
        d = ImageDraw.Draw(lay)
        for i in range(26):
            x, y = pseudo(i, 17) * W, pseudo(i, 29) * H
            r = 0.8 + pseudo(i, 43) * 1.7
            a = byte_alpha(0.15 + pseudo(i, 53) * 0.40)
            d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255, a))
        img = over(img, lay)
        add_dots(img, 66, 2, accent, 0.10)
        add_vignette(img, 0.46, 0.26)

    else:  # sunrise
        sun = (W * 0.84, H * 0.13)
        sun_color = (int(accent[0] * 0.78), int(accent[1] * 0.66), int(accent[2] * 0.55))
        ring = (int(accent[0] * 0.72), int(accent[1] * 0.60), int(accent[2] * 0.50))
        add_glow(img, sun, W * 0.54, sun_color, 0.30, 0.05)
        add_glow(img, (W * 0.12, H * 0.86), W * 0.50, accent2, 0.16, 0.10)
        lay = new_layer()
        d = ImageDraw.Draw(lay)
        for i in range(4):
            r = W * (0.09 + i * 0.055)
            d.ellipse([sun[0] - r, sun[1] - r, sun[0] + r, sun[1] + r],
                      outline=ring + (70 - i * 14,), width=1)
        img = over(img, lay)
        add_horizon(img, H * 0.80, H * 0.13, accent, 0.10)
        lay = new_layer()
        r = W * 0.030
        ImageDraw.Draw(lay).ellipse([sun[0] - r, sun[1] - r, sun[0] + r, sun[1] + r],
                                    fill=ring + (40,))
        img = over(img, lay)
        add_dots(img, 30, 2, accent2, 0.085)
        add_vignette(img, 0.14, 0.42)

    add_edge_band(img, SIDEBAR_W, W, TOPBAR_H, H, 26, accent, 0.16)
    return img


# --- 示意 UI（侧栏/顶栏/卡片，只为看清背景留白处的效果）--------------------
def draw_ui(img, th):
    """按板端真实版面重画首页 + 入住页（对照现场照片的几何）：
    侧栏 80px、顶栏 64px、内容区四周 10px、卡片间距 10px。"""
    tid, name, dark = th[0], th[1], th[2]
    accent, accent2 = th[3], th[4]
    card0, card1, border = th[5], th[6], th[9]
    sb0, sb1, sbborder = th[10], th[11], th[12]
    text_main, text_sub = th[13], th[14]
    a0, a1 = (235, 209) if dark else (230, 204)
    d = ImageDraw.Draw(img, 'RGBA')

    # 侧栏（渐变）
    sb = new_layer()
    ds = ImageDraw.Draw(sb)
    for y in range(H):
        ds.line([(0, y), (SIDEBAR_W, y)], fill=mix(sb0, sb1, y / float(H - 1)) + (240,))
    ds.rectangle([0, 0, SIDEBAR_W - 1, H], outline=sbborder + (255,))
    img.paste(over(img, sb), (0, 0))

    # 顶栏
    tb = new_layer()
    dt = ImageDraw.Draw(tb)
    for x in range(SIDEBAR_W, W):
        f = (x - SIDEBAR_W) / float(W - SIDEBAR_W)
        dt.line([(x, 0), (x, TOPBAR_H)], fill=mix(card0, card1, f) + (242,))
    dt.line([(SIDEBAR_W, TOPBAR_H), (W, TOPBAR_H)], fill=sbborder + (255,))
    img.paste(over(img, tb), (0, 0))

    font_path = None
    for cand in (r'C:\Windows\Fonts\msyh.ttc', r'C:\Windows\Fonts\msyh.ttf',
                 r'C:\Windows\Fonts\simhei.ttf'):
        if os.path.exists(cand):
            font_path = cand
            break

    def font(size, bold=False):
        if font_path:
            try:
                return ImageFont.truetype(font_path, size)
            except Exception:
                pass
        return ImageFont.load_default()

    def panel(x0, y0, x1, y1, radius=10, aa0=None, aa1=None, outl=None):
        """panelCard：card0 -> card1 斜向渐变（带透明度，背景透出来一点）"""
        cw, ch = x1 - x0, y1 - y0
        l0 = a0 if aa0 is None else aa0
        l1 = a1 if aa1 is None else aa1
        lay = new_layer()
        dc = ImageDraw.Draw(lay)
        for y in range(y0, y1):
            f = (y - y0) / float(max(ch - 1, 1))
            dc.line([(x0, y), (x1, y)], fill=mix(card0, card1, f) + (int(l0 + (l1 - l0) * f),))
        mask = Image.new('L', (cw, ch), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, cw - 1, ch - 1], radius=radius, fill=255)
        cropped = lay.crop((x0, y0, x1, y1))
        cropped.putalpha(mask_multiply(cropped.getchannel('A'), mask))
        img.paste(over(img.crop((x0, y0, x1, y1)), cropped), (x0, y0))
        ImageDraw.Draw(img, 'RGBA').rounded_rectangle(
            [x0, y0, x1 - 1, y1 - 1], radius=radius,
            outline=(outl if outl else border) + (255,))

    def box(x0, y0, x1, y1, label, value, radius=8):
        """sensorBox / infoBox：主题色微渐变 + accent 描边"""
        cw, ch = x1 - x0, y1 - y0
        lay = new_layer()
        dc = ImageDraw.Draw(lay)
        for y in range(y0, y1):
            f = (y - y0) / float(max(ch - 1, 1))
            dc.line([(x0, y), (x1, y)], fill=mix(card0, card1, f) + (int(87 + (36 - 87) * f),))
        mask = Image.new('L', (cw, ch), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, cw - 1, ch - 1], radius=radius, fill=255)
        cropped = lay.crop((x0, y0, x1, y1))
        cropped.putalpha(mask_multiply(cropped.getchannel('A'), mask))
        img.paste(over(img.crop((x0, y0, x1, y1)), cropped), (x0, y0))
        ImageDraw.Draw(img, 'RGBA').rounded_rectangle(
            [x0, y0, x1 - 1, y1 - 1], radius=radius,
            outline=tuple(int(c * 0.24 + 255 * 0) for c in accent) + (255,))
        d.text(((x0 + x1) / 2, y0 + 6), label, font=font(11), fill=text_sub + (255,), anchor='ma')
        d.text(((x0 + x1) / 2, y0 + 22), value, font=font(13), fill=accent + (255,), anchor='ma')

    def title(x, y, text):
        """cardTitle：左侧主题色竖条"""
        d.rectangle([x, y + 2, x + 2, y + 16], fill=accent + (255,))
        d.text((x + 10, y), text, font=font(16), fill=text_main + (255,))

    # ================= 首页 =================
    CX0, CX1 = SIDEBAR_W + 10, W - 10
    # 欢迎卡
    panel(CX0, TOPBAR_H + 10, CX1, 200)
    d.text((CX0 + 14, TOPBAR_H + 24), '欢迎入住 · 沈阳盛京酒店', font=font(20), fill=accent + (255,))
    d.text((CX0 + 14, TOPBAR_H + 56), '尊敬的先生/女士，您的房间已准备就绪', font=font(12), fill=text_sub + (255,))
    # 5 个信息小格
    bw = int((CX1 - CX0 - 14 * 4 - 20) / 5.0)
    for i, (lab, val) in enumerate([('房号', '812'), ('离店', '次日 12:00'), ('早餐', '06:30-10:00'),
                                    ('WiFi', 'ShengJingHotel'), ('三日预报', '晴27°')]):
        bx = CX0 + 14 + i * (bw + 14)
        box(bx, TOPBAR_H + 84, bx + bw, TOPBAR_H + 128, lab, val)

    # 客房环境卡（大块留白区）
    panel(CX0, 210, 470, H - 10)
    title(CX0 + 14, 222, '客房环境')
    d.text((CX0 + 14, 254), '实时监测 · 健康入住', font=font(12), fill=text_sub + (255,))
    # 舒适客房示意图：主题色斜向渐变 + 描边（原来是一整块纯色）
    ph = new_layer()
    dph = ImageDraw.Draw(ph)
    for y in range(300, H - 26):
        f = (y - 300) / float(H - 26 - 300)
        dph.line([(CX0 + 14, y), (460, y)], fill=mix(accent, accent2, 1 - f) + (int(33 + (13 - 33) * f),))
    mask = Image.new('L', (460 - CX0 - 14, H - 26 - 300), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, 460 - CX0 - 15, H - 27 - 300], radius=10, fill=255)
    cr = ph.crop((CX0 + 14, 300, 460, H - 26))
    cr.putalpha(mask_multiply(cr.getchannel('A'), mask))
    img.paste(over(img.crop((CX0 + 14, 300, 460, H - 26)), cr), (CX0 + 14, 300))
    ImageDraw.Draw(img, 'RGBA').rounded_rectangle([CX0 + 14, 300, 459, H - 27], radius=10,
                                                  outline=tuple(int(c * 1.0) for c in accent) + (71,))
    # 大图标底衬（对应 roomPlaceholderIcon：64x64 圆角底 + 56px 图标）
    icx, icy = (CX0 + 14 + 460) // 2, 340
    plate = new_layer()
    ImageDraw.Draw(plate).rounded_rectangle([icx - 32, icy - 32, icx + 32, icy + 32],
                                            radius=12, fill=accent + (51,))
    img.paste(over(img.crop((icx - 32, icy - 32, icx + 32, icy + 32)), plate.crop((icx - 32, icy - 32, icx + 32, icy + 32))), (icx - 32, icy - 32))
    d.text((icx, icy), '床', font=font(28), fill=text_main + (255,), anchor='mm')
    d.text((icx, 400), '舒适客房示意图', font=font(13), fill=text_main + (255,), anchor='mm')

    # 右侧 4 个快捷磁贴
    tx0, ty0 = 480, 210
    tw2, th2 = int((W - 10 - tx0 - 10) / 2.0), 74
    for i, nm in enumerate(['客房控制', '酒店服务', '入住办理', '系统设置']):
        r, c = divmod(i, 2)
        x0 = tx0 + c * (tw2 + 10)
        y0 = ty0 + r * (th2 + 10)
        panel(x0, y0, x0 + tw2, y0 + th2, radius=12, aa0=228, aa1=202)
        d.text((x0 + 14, y0 + th2 / 2 - 8), nm, font=font(14), fill=text_main + (255,))

    # ================= 侧栏 + 顶栏文字 =================
    d.rectangle([12, 12, 42, 42], fill=accent + (255,))
    d.text((27, 27), '西', font=font(16), fill=(255, 255, 255, 255), anchor='mm')
    for i, nm in enumerate(('首页', '客房', '入住', '服务', '设置')):
        y = 80 + i * 72
        col = accent + (255,) if i == 0 else text_sub + (255,)
        d.text((SIDEBAR_W / 2, y + 14), nm, font=font(12), fill=col, anchor='mm')
        if i == 0:
            d.rectangle([0, y - 6, 2, y + 26], fill=accent + (255,))
    d.text((SIDEBAR_W + 16, 12), '欢迎入住 · 沈阳盛京酒店', font=font(14), fill=text_main + (255,))
    d.text((SIDEBAR_W + 16, 36), '用心服务 · 让旅途更美好', font=font(11), fill=text_sub + (255,))
    d.text((W - 150, 12), '25.6°C', font=font(18), fill=accent + (255,))
    d.text((W - 70, 12), '08:26', font=font(20), fill=accent + (255,))
    d.text((W - 200, 40), '2026年9月15日 周二', font=font(11), fill=text_sub + (255,))
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    shots = []
    for th in THEMES:
        bg = build_background(th)
        full = draw_ui(bg.copy(), th)
        path = os.path.join(OUT, '%s.png' % th[0])
        full.convert('RGB').save(path)
        shots.append((th, full))
        print('wrote', path)

    # 2 列 x 3 行拼图
    scale = 0.66
    tw, thh = int(W * scale), int(H * scale)
    pad, gap = 16, 12
    montage = Image.new('RGB', (pad * 2 + tw * 2 + gap, pad * 2 + thh * 3 + gap * 2),
                        (10, 12, 16))
    for i, (t, im) in enumerate(shots):
        r, c = divmod(i, 2)
        montage.paste(im.convert('RGB').resize((tw, thh), Image.LANCZOS),
                      (pad + c * (tw + gap), pad + r * (thh + gap)))
    mpath = os.path.join(OUT, 'all_themes.png')
    montage.save(mpath)
    print('wrote', mpath)


if __name__ == '__main__':
    main()
