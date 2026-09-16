#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验 C++ 版 LbphEngine 的 YAML 解析逻辑（用同一套规则在 Python 里再跑一遍）。

重点验证：
  1. 能拿到 2 个 16384 维直方图 + 2 个 label；
  2. 每个 256-bin 网格直方图的和 == 1（证明切片/偏移没错位）；
  3. 标签对之间的卡方距离量级（用于给 verify_distance 一个合理初值）。

用法： python validate_lbph_model.py <trainer.yml>
"""
import re
import sys
import math

WANT_GRID = 8
WANT_BINS = 256


def numerics(text):
    """等价于 C++ 里的 numericsIn()：抽出所有数值 token"""
    out = []
    for m in re.finditer(r'[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?', text):
        try:
            out.append(float(m.group(0)))
        except ValueError:
            pass
    return out


def value_after(block, key, fallback):
    k = block.find(key)
    if k < 0:
        return fallback
    nl = block.find('\n', k)
    if nl < 0:
        nl = len(block)
    v = numerics(block[k + len(key):nl])
    return v[0] if v else fallback


def parse(text):
    radius = int(value_after(text, 'radius:', 1))
    neighbors = int(value_after(text, 'neighbors:', 8))
    grid_x = int(value_after(text, 'grid_x:', 8))
    grid_y = int(value_after(text, 'grid_y:', 8))
    threshold = value_after(text, 'threshold:', float('inf'))

    want = grid_x * grid_y * WANT_BINS
    hists = []
    search = 0
    while True:
        k = text.find('- !!opencv-matrix', search)
        if k < 0:
            break
        nxt = text.find('- !!opencv-matrix', k + 1)
        block = text[k: nxt if nxt >= 0 else len(text)]
        d = block.find('data:')
        if d < 0:
            break
        op = block.find('[', d)
        cl = block.find(']', d)
        if op < 0:
            break
        if cl < 0:
            cl = len(block)
        # 只看 data 数组内部：labels 段出现在最后一个直方图之后，不能拿整块判断
        body = block[op + 1:cl]
        if 'labels:' in body:
            break
        raw = numerics(body)
        hists.append(raw)
        search = nxt if nxt >= 0 else len(text)

    labels = []
    lk = text.find('labels:')
    if lk >= 0:
        m = text.find('!!opencv-matrix', lk)
        if m >= 0:
            d = text.find('data:', m)
            op = text.find('[', d)
            cl = text.find(']', d)
            if op >= 0:
                labels = [int(v) for v in numerics(text[op + 1:cl if cl >= 0 else len(text)])]

    info = {}
    ik = text.find('labelsInfo:')
    if ik >= 0:
        after = text[ik + 11:]
        ob = after.find('{')
        if ob >= 0:
            cb = after.find('}', ob)
            body = after[ob + 1: cb if cb >= 0 else len(after)]
            for p in body.split(','):
                if ':' not in p:
                    continue
                k2, v2 = p.split(':', 1)
                try:
                    info[int(k2.strip())] = v2.strip().strip('"\'')
                except ValueError:
                    pass
        else:
            for line in after.split('\n')[1:]:
                line = line.strip()
                if ':' not in line:
                    continue
                k2, v2 = line.split(':', 1)
                try:
                    info[int(k2.strip())] = v2.strip().strip('"\'')
                except ValueError:
                    pass
    return dict(radius=radius, neighbors=neighbors, grid_x=grid_x, grid_y=grid_y,
                threshold=threshold, want=want), hists, labels, info


def chi_sqr(a, b):
    """OpenCV compareHist(HISTCMP_CHISQR)"""
    s = 0.0
    for x, y in zip(a, b):
        if x == 0.0:
            continue
        s += (x - y) ** 2 / x
    return s


def main(path):
    text = open(path, encoding='utf-8', errors='replace').read()
    params, hists, labels, info = parse(text)

    print('参数: radius=%d neighbors=%d grid=%dx%d threshold=%g'
          % (params['radius'], params['neighbors'], params['grid_x'], params['grid_y'],
             params['threshold']))
    print('期望特征维度: %d' % params['want'])
    print('直方图数量: %d   标签: %s   名字映射: %s'
          % (len(hists), labels, info or '(空 —— 需要 faces_model.json)'))

    ok = True
    for i, h in enumerate(hists):
        if len(h) != params['want']:
            print('  [FAIL] 第 %d 个直方图维度 %d != %d' % (i, len(h), params['want']))
            ok = False
            continue
        # 每个网格直方图归一化后和应为 1
        sums = [sum(h[c * WANT_BINS:(c + 1) * WANT_BINS])
                for c in range(params['grid_x'] * params['grid_y'])]
        bad = [s for s in sums if abs(s - 1.0) > 1e-4]
        nonzero = sum(1 for v in h if v > 0)
        print('  [%s] 直方图 %d: 维度 %d, 非零 bin %d, 每格和 min=%.6f max=%.6f %s'
              % ('OK' if not bad else 'FAIL', i, len(h), nonzero,
                 min(sums), max(sums), '' if not bad else '有 %d 格不等于 1' % len(bad)))
        if bad:
            ok = False

    if len(hists) >= 2:
        d = chi_sqr(hists[0], hists[1])
        print('模板 0 与 1 的卡方距离: %.4f   -> 置信度 1/(1+d) = %.6f' % (d, 1.0 / (1.0 + d)))
        print('提示: 模型内两模板距离偏小说明可能是同一人；verify_distance 应取略小于该值。')

    print('\n结论:', '解析与结构校验通过' if ok else '存在问题，请检查')
    return 0 if ok else 1


if __name__ == '__main__':
    p = sys.argv[1] if len(sys.argv) > 1 else \
        r'C:\Users\jifen\.dsh\attachments\v1\files\90\909feadcaf1439a5807f315e7b11931639da804a5c46edcc3c2533bd56889596\trainer.yml'
    sys.exit(main(p))
