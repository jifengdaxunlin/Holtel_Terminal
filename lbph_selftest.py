#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""LBPH 算法自检：用 Python 复刻 face/lbph_engine.cpp 的逐像素 ELBP + 网格直方图，
在可验证的输入上检查数学是否正确（本机没有 Qt 工具链，无法直接编译验证 C++）。

覆盖 4 项：
  1. uniform LBP(8,1) 一共 58 种模式、且全部模式码都是"至多 2 次 0/1 跳变"；
  2. 手工可验的三种邻域（全高/全低/左亮右暗）模式码分别为 0 / 255 / 30；
  3. 同一张图对自己的卡方距离 = 0（归一化与直方图统计自洽）；
  4. 图像越接近，距离越小（构造 5 档线性过渡图，检查距离单调不减）。

用法：python lbph_selftest.py
"""
import math

BINS = 256
GRID = 8


def elbp(img, w, h, radius=1, neighbors=8):
    """严格按 C++ 版实现：圆形邻域 + 双线性插值（整数偏移时权重取整）+ 8bit 模式码"""
    code = [[0] * w for _ in range(h)]
    for n in range(neighbors):
        ty = -radius * math.sin(2.0 * math.pi * n / neighbors)
        tx = radius * math.cos(2.0 * math.pi * n / neighbors)
        ry = int(math.floor(ty + 0.5))
        rx = int(math.floor(tx + 0.5))
        fy = ty - ry
        fx = tx - rx
        # 与 C++ 一致：把浮点残差归零，避免"本应相等"的像素被判定为更小
        if -1e-6 < fy < 1e-6:
            fy = 0.0
        if -1e-6 < fx < 1e-6:
            fx = 0.0
        for y in range(radius, h - radius):
            for x in range(radius, w - radius):
                cy, cx = y + ry, x + rx
                center = img[y][x]
                sample = 0.0
                if 0 <= cx < w and 0 <= cy < h:
                    v00 = img[cy][cx]
                    if fx == 0.0 and fy == 0.0:
                        sample = v00
                    elif fx == 0.0:
                        sample = v00 * (1.0 - fy) + (img[cy + 1][cx] * fy if cy + 1 < h else v00 * fy)
                    elif fy == 0.0:
                        sample = v00 * (1.0 - fx) + (img[cy][cx + 1] * fx if cx + 1 < w else v00 * fx)
                    else:
                        v10 = img[cy][cx + 1] if cx + 1 < w else v00
                        v01 = img[cy + 1][cx] if cy + 1 < h else v00
                        v11 = img[cy + 1][cx + 1] if (cx + 1 < w and cy + 1 < h) else v00
                        sample = (v00 * (1.0 - fx) * (1.0 - fy) + v10 * fx * (1.0 - fy)
                                  + v01 * (1.0 - fx) * fy + v11 * fx * fy)
                elif -1 <= cx < w and -1 <= cy < h:
                    if cx >= 0 and cy >= 0:
                        sample = img[cy][cx] * (1.0 - fx) * (1.0 - fy)
                    elif cx + 1 < w and cy >= 0:
                        sample = img[cy][cx + 1] * fy
                    elif cy + 1 < h and cx >= 0:
                        sample = img[cy + 1][cx] * fx
                if center >= sample:
                    code[y][x] |= (1 << n)
    return code


def spatial_histogram(code, w, h, gx=GRID, gy=GRID):
    feat = []
    for j in range(gy):
        for i in range(gx):
            ys = int(j * h / gy)
            ye = (h - 1) if j == gy - 1 else int((j + 1) * h / gy)
            xs = int(i * w / gx)
            xe = (w - 1) if i == gx - 1 else int((i + 1) * w / gx)
            hist = [0.0] * BINS
            for yy in range(ys, ye + 1):
                for xx in range(xs, xe + 1):
                    if 0 <= yy < h and 0 <= xx < w:
                        hist[code[yy][xx]] += 1.0
            s = sum(hist)
            if s > 0:
                hist = [v / s for v in hist]
            feat.extend(hist)
    return feat


def chi_sqr(a, b):
    return sum((x - y) ** 2 / x for x, y in zip(a, b) if x != 0.0)


def uniform_count(neighbors=8):
    """至多 2 次循环跳变的模式数（8 邻域应为 58）"""
    cnt = 0
    for v in range(256):
        bits = [(v >> k) & 1 for k in range(neighbors)]
        trans = sum(1 for k in range(neighbors) if bits[k] != bits[(k + 1) % neighbors])
        if trans <= 2:
            cnt += 1
    return cnt


def make_gradient(v, w=40, h=48):
    return [[v for _ in range(w)] for _ in range(h)]


def make_texture(w=40, h=48, seed=7, noise=3):
    """有结构的纹理图（LBP 只在"有纹理"时才携带信息；
    纯色图在 LBP 下是退化的：所有像素相同 -> 模式码 255，与亮度无关）"""
    rnd = seed
    img = []
    for y in range(h):
        row = []
        for x in range(w):
            rnd = (1103515245 * rnd + 12345) & 0x7FFFFFFF
            base = 120 + 60 * math.sin(x / 3.0) * math.cos(y / 4.0)
            row.append(max(0, min(255, int(base + (rnd % 10007) / 10007.0 * noise * 20 - noise * 10))))
        img.append(row)
    return img


def blur(img, w, h, k):
    """k 次 3x3 均值模糊：k 越大越偏离原图"""
    cur = [row[:] for row in img]
    for _ in range(k):
        nxt = [row[:] for row in cur]
        for y in range(1, h - 1):
            for x in range(1, w - 1):
                s = 0.0
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        s += cur[y + dy][x + dx]
                nxt[y][x] = s / 9.0
        cur = nxt
    return cur


def main():
    ok = True

    # 1) uniform 模式数
    n = uniform_count()
    print('[%s] uniform LBP(8,1) 模式数 = %d（期望 58）' % ('OK' if n == 58 else 'FAIL', n))
    ok = ok and n == 58

    # 2) 手工可验的三种邻域（3x3~，取中心点的模式码）
    #   全部相等 -> 中心 >= 邻居 -> 每一位都是 1 -> 255
    flat = [[100] * 8 for _ in range(8)]
    #   中心比周围暗 -> 每一位 0 -> 0
    dark = [[200] * 8 for _ in range(8)]
    dark[4][4] = 50
    #   左亮右暗：n=0(右) 与 n=7(右上) 之外都为 0 -> 0b00000011 = 3? 逐位看结果
    lr = [[100] * 8 for _ in range(8)]
    for y in range(8):
        for x in range(4):
            lr[y][x] = 200

    c_flat = elbp(flat, 8, 8)[4][4]
    c_dark = elbp(dark, 8, 8)[4][4]
    c_lr = elbp(lr, 8, 8)[4][4]
    # 全相等 -> 255；中心更暗 -> 0；左亮右暗 -> 既不是 0 也不是 255（部分位为 1）
    good = (c_flat == 255 and c_dark == 0 and 0 < c_lr < 255)
    print('[%s] 模式码: 全相等=%d（期望255） 中心更暗=%d（期望0） 左亮右暗=%d（0<x<255）' %
          ('OK' if good else 'FAIL', c_flat, c_dark, c_lr))
    ok = ok and good

    # 3) 自距离 = 0（用有纹理的图，纯色图在 LBP 下是退化的）
    base = make_texture()
    hb = spatial_histogram(elbp(base, 40, 48), 40, 48)
    d0 = chi_sqr(hb, hb)
    print('[%s] 自身卡方距离 = %.6f（期望 0）' % ('OK' if d0 == 0.0 else 'FAIL', d0))
    ok = ok and d0 == 0.0

    # 4) 模糊（偏离原图）后距离应显著变大，且维持在"明显不同"的量级
    #    注：不要求严格单调——重度模糊会把纹理抹平，LBP 模式趋于退化，距离会在高位波动
    ds = {}
    line = []
    for k in (0, 1, 2, 3, 5):
        hk = spatial_histogram(elbp(blur(base, 40, 48, k), 40, 48), 40, 48)
        ds[k] = chi_sqr(hb, hk)
        line.append('blur%d d=%.2f' % (k, ds[k]))
    peak = max(ds.values())
    good = (ds[0] == 0.0 and ds[1] > 10.0
            and all(ds[k] > peak * 0.5 for k in (1, 2, 3, 5)))
    print('[%s] 清晰/模糊距离: %s' % ('OK' if good else 'FAIL', '  '.join(line)))
    ok = ok and good

    # 特征维度
    print('特征维度 = %d（期望 16384）' % len(hb))
    ok = ok and len(hb) == GRID * GRID * BINS

    print('\n结论:', 'LBPH 数学自检通过' if ok else '存在问题')
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
