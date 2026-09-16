#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""主题化文字预览：验证"大字号渐变字"在 6 套主题下是否都清晰好看。

复刻 mainwindow.cpp 里新增的规则：
  QLabel#topTime / #topTemp  : 竖向 accent -> accent2 渐变
  QLabel#heroAccent          : 竖向渐变 + font-weight 800
  QLabel#heroTitle           : 对角 accent -> accent2 渐变
  QLabel#pageTitle           : 对角渐变 + 800

用法：python text_style_preview.py   ->  preview_bg/text_styles.png
"""
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 800, 480
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'preview_bg')

# (主题名, 深色?, accent, accent2, card0, card1, textMain, textSub, bg, sidebarBorder)
THEMES = [
    ("深海蓝", True,  (0x00, 0xd4, 0xff), (0x00, 0x77, 0xbe), (0x0f, 0x2a, 0x52), (0x08, 0x1a, 0x33), (0xff, 0xff, 0xff), (0x8a, 0xa4, 0xc8), (0x05, 0x0d, 0x1a), (0x13, 0x2b, 0x4d)),
    ("月光浅", False, (0x00, 0x77, 0xbe), (0x00, 0xd4, 0xff), (0xff, 0xff, 0xff), (0xe8, 0xee, 0xf5), (0x1a, 0x23, 0x32), (0x5a, 0x6b, 0x82), (0xee, 0xf2, 0xf6), (0xc8, 0xd2, 0xdd)),
    ("曜石金", True,  (0xf1, 0xc4, 0x0f), (0xb8, 0x86, 0x0b), (0x2a, 0x24, 0x16), (0x1a, 0x16, 0x08), (0xff, 0xff, 0xff), (0xc8, 0xb9, 0x8a), (0x0d, 0x0b, 0x04), (0x4a, 0x3d, 0x12)),
    ("墨玉绿", True,  (0x2e, 0xcc, 0x71), (0x1e, 0x84, 0x49), (0x10, 0x33, 0x1f), (0x08, 0x1a, 0x10), (0xff, 0xff, 0xff), (0x8a, 0xc8, 0xa4), (0x05, 0x10, 0x09), (0x12, 0x4a, 0x2a)),
    ("幻紫",   True,  (0xa5, 0x69, 0xbd), (0x6c, 0x34, 0x83), (0x2a, 0x1a, 0x45), (0x16, 0x0d, 0x24), (0xff, 0xff, 0xff), (0xb8, 0x9a, 0xc8), (0x10, 0x09, 0x19), (0x3a, 0x21, 0x54)),
    ("暖阳橙", False, (0xe6, 0x7e, 0x22), (0xd3, 0x54, 0x00), (0xff, 0xff, 0xff), (0xfd, 0xf0, 0xe2), (0x33, 0x23, 0x1a), (0x82, 0x62, 0x4a), (0xfd, 0xf6, 0xee), (0xe8, 0xcd, 0xb0)),
]


def font(size, bold=True):
    for cand in (r'C:\Windows\Fonts\msyhbd.ttc', r'C:\Windows\Fonts\msyh.ttc',
                 r'C:\Windows\Fonts\simhei.ttf'):
        if os.path.exists(cand):
            try:
                return ImageFont.truetype(cand, size)
            except Exception:
                pass
    return ImageFont.load_default()


def grad_text(tile, xy, text, fnt, c0, c1, diagonal=False):
    """按矩形范围内的渐变给文字上色（等价 QSS 的 color: qlineargradient）"""
    box = fnt.getbbox(text)
    tw, th = box[2] - box[0], box[3] - box[1]
    pad = 4
    mask = Image.new('L', (tw + pad * 2, th + pad * 2), 0)
    ImageDraw.Draw(mask).text((pad - box[0], pad - box[1]), text, font=fnt, fill=255)

    size = mask.size
    grad = Image.new('RGB', size)
    d = ImageDraw.Draw(grad)
    for y in range(size[1]):
        for_x = y / max(1, size[1] - 1)
        if diagonal:
            # 对角：用 (x+y) 的归一化近似
            for x in range(size[0]):
                t = (x / max(1, size[0] - 1) + for_x) / 2.0
                d.point((x, y), fill=tuple(int(c0[k] + (c1[k] - c0[k]) * t) for k in range(3)))
        else:
            col = tuple(int(c0[k] + (c1[k] - c0[k]) * for_x) for k in range(3))
            d.line([(0, y), (size[0], y)], fill=col)
    tile.paste(grad, xy, mask)
    return tw, th


def theme_tile(t):
    name, dark, accent, accent2, card0, card1, tmain, tsub, bg, sbb = t
    im = Image.new('RGB', (W, H), bg)
    d = ImageDraw.Draw(im)

    # 顶栏
    for x in range(W):
        f = x / float(W - 1)
        d.line([(x, 0), (x, 64)], fill=tuple(int(card0[k] + (card1[k] - card0[k]) * f) for k in range(3)))
    d.line([(0, 64), (W, 64)], fill=sbb)

    d.text((18, 14), '智能客房终端', font=font(15), fill=tmain)
    d.text((18, 38), '沈阳 · 晴 12°C', font=font(11), fill=tsub)

    # 顶栏右侧：温度 + 时钟（accent -> accent2，小面积最醒目）
    grad_text(im, (W - 190, 8), '25.6°C', font(20), accent, accent2)
    grad_text(im, (W - 110, 8), '08:26', font(22), accent, accent2)
    d.text((W - 110, 42), '2026-02-19', font=font(11), fill=tsub)

    # 卡片
    def card(x0, y0, x1, y1, title=None, title_grad=False, big=None):
        for y in range(y0, y1):
            f = (y - y0) / float(max(1, y1 - y0 - 1))
            d.line([(x0, y), (x1, y)], fill=tuple(int(card0[k] + (card1[k] - card0[k]) * f) for k in range(3)))
        d.rounded_rectangle([x0, y0, x1 - 1, y1 - 1], radius=10, outline=sbb)
        ty = y0 + 12
        if title:
            d.rectangle([x0 + 10, ty + 2, x0 + 12, ty + 16], fill=accent)   # 标题左侧竖条
            d.text((x0 + 20, ty), title, font=font(16), fill=tmain)
        if big:
            grad_text(im, (x0 + 20, ty + 26), big, font(22), accent, accent2)

    card(10, 76, 300, 210, '欢迎入住 · 西湖山庄', big=None)
    # heroAccent / heroTitle：textMain -> accent（白/黑 -> 主题色，两端都清晰）
    grad_text(im, (30, 118), '欢迎入住 · 西湖山庄', font(22), tmain, accent, diagonal=True)
    d.text((30, 150), '房间 8808 · 行政大床房', font=font(14), fill=tsub)
    d.text((30, 176), '入住 2 晚 · 含双早', font=font(12), fill=tsub)

    card(310, 76, 470, 210, '空调')
    grad_text(im, (330, 112), '26°C', font(22), tmain, accent)
    d.text((330, 146), '制冷 · 中风', font=font(12), fill=tsub)

    card(480, 76, W - 10, 210, '环境监测')
    d.text((500, 114), '温度 24.5°C', font=font(14), fill=accent)
    d.text((500, 140), '湿度 52%RH', font=font(14), fill=accent)
    d.text((500, 166), 'CO₂ 0.8kΩ', font=font(14), fill=accent)

    grad_text(im, (30, 240), '客房服务', font(20), tmain, accent, diagonal=True)
    d.text((30, 280), '大字号渐变字：pageTitle / heroTitle / heroAccent / topTime / topTemp',
           font=font(13), fill=tsub)

    # 主题名
    d.text((30, H - 60), '%s  accent %s -> accent2 %s' % (name, '#%02x%02x%02x' % accent, '#%02x%02x%02x' % accent2),
           font=font(12), fill=tsub)
    d.rectangle([0, 0, W - 1, H - 1], outline=sbb)
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    scale = 0.5
    tw, th = int(W * scale), int(H * scale)
    pad, gap = 14, 10
    cols = 2
    rows = 3
    montage = Image.new('RGB', (pad * 2 + tw * cols + gap * (cols - 1),
                                pad * 2 + th * rows + gap * (rows - 1)), (10, 12, 16))
    for i, t in enumerate(THEMES):
        tile = theme_tile(t).resize((tw, th), Image.LANCZOS)
        r, c = divmod(i, cols)
        montage.paste(tile, (pad + c * (tw + gap), pad + r * (th + gap)))
    p = os.path.join(OUT, 'text_styles.png')
    montage.save(p)
    print('wrote', p)


if __name__ == '__main__':
    main()
